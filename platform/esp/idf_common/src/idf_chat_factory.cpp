/**
 * @file idf_chat_factory.cpp
 * @brief Portable chat / contact / mesh service factory for the minimal
 *        ESP-IDF LoRa-chat facade.
 *
 * De-Serial'd, team/GPS-free extraction of the chat + contact + mesh factory
 * from platform/esp/arduino_common/src/app_context_platform_bindings.cpp
 * (~132-214).
 *
 * Differences from the Arduino factory:
 *   - No Serial / logging.
 *   - Chat store is always RamStore (no SD LogStore branch). The minimal chat
 *     scope keeps storage dependency-light; SD persistence can be layered later.
 *   - The mesh runtime IS a concrete platform::esp::radio::MeshtasticRadioAdapter
 *     built directly from the board's LoraBoard. The Arduino path wrapped a
 *     MeshAdapterRouter around a backend and relied on a background radio task;
 *     the MeshtasticRadioAdapter self-pumps (RX + TX) inside processSendQueue(),
 *     so no router and no task are needed for single-protocol chat.
 */

#include "platform/esp/idf_common/idf_chat_factory.h"

#include <cstddef>
#include <cstdint>
#include <utility> // std::move
#include <vector>

#include "esp_log.h"

#include "board/LoraBoard.h"
#include "chat/domain/chat_model.h"
#include "chat/domain/chat_types.h" // chat::MeshProtocol
#include "chat/infra/contact_store_core.h"
#include "chat/infra/node_store_core.h"
#include "chat/infra/store/ram_store.h"
#include "chat/ports/i_contact_blob_store.h"
#include "chat/ports/i_node_blob_store.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "platform/esp/arduino_common/chat/infra/chat_event_bus_bridge.h"
#include "platform/esp/arduino_common/chat/infra/meshcore/meshcore_adapter.h"
#include "platform/ui/settings_store.h"

namespace platform::esp::idf_common
{
namespace
{

// ---------------------------------------------------------------------------
// Blob stores for the minimal chat scope.
//
// NodeStoreCore / ContactStoreCore persist via an injected blob-store port.
// The Arduino shells (meshtastic::NodeStore / contacts::ContactStore) back that
// port with SD/NVS through storage/sd_card_runtime.h, which hard-includes
// <SPI.h> / <Arduino.h> / <Preferences.h> and so cannot build in the pure
// ESP-IDF target. The IDF target therefore provides its own blob stores (the
// IDF analogue of the Linux*BlobStore classes in
// platform/linux/common/src/app/linux_app_services.cpp):
//   - RamNodeBlobStore: volatile node roster. load() returns the bytes last
//     saved this boot (initially empty), save() retains them, clear() drops
//     them. The node roster is ephemeral RF telemetry that repopulates from
//     live packets and is re-synthesized for each persisted contact on boot, so
//     it intentionally does NOT persist across reboots.
//   - NvsContactBlobStore (below): durable contacts in NVS.
// ---------------------------------------------------------------------------
class RamNodeBlobStore final : public chat::contacts::INodeBlobStore
{
  public:
    bool loadBlob(std::vector<uint8_t>& out) override
    {
        out = blob_;
        return !blob_.empty();
    }

    bool saveBlob(const uint8_t* data, size_t len) override
    {
        if (data != nullptr && len > 0)
        {
            blob_.assign(data, data + len);
        }
        else
        {
            blob_.clear();
        }
        return true;
    }

    void clearBlob() override
    {
        blob_.clear();
    }

  private:
    std::vector<uint8_t> blob_;
};

// ---------------------------------------------------------------------------
// NVS-backed contact blob store: the ONE durable piece of chat state.
//
// Mirrors the Linux precedent (LinuxContactBlobStore in
// platform/linux/common/src/app/linux_app_services.cpp): persist only the
// CONTACT blob (id + nickname, user-authored) to NVS so contacts survive
// reboot / reflash. ContactStoreCore::saveEntries fires only on add/edit/remove
// (infrequent, NVS-friendly). The node roster stays in RAM (RamNodeBlobStore):
// NodeStoreCore saves every ~5s under mesh traffic, so NVS-backing it would wear
// the flash, and it is ephemeral RF telemetry that repopulates from live
// packets. On boot, ContactService::begin() re-synthesizes each loaded contact's
// node record via ensureNodeExistsForContact, so the node store needs no
// persistence and no on-load hook is required here.
//
// Storage isolation + forward-compat:
//   - Dedicated NVS namespace/key ("tm_contacts" / "contacts_v1", both <=15
//     chars) so contact data never touches the license-bearing "settings"
//     namespace. Only this single key is ever written; clear_namespace is never
//     called.
//   - A 4-byte file-level header {'T','M','C',0x01} is prepended on save and
//     stripped on load. The header is transparent to the core: ContactStoreCore
//     decodes a RAW Entry[] array (len must be a multiple of sizeof(Entry)), so
//     the header must NOT reach the core. loadBlob returns only the raw payload.
//     The version byte lets a future format reject older blobs cleanly.
// ---------------------------------------------------------------------------
class NvsContactBlobStore final : public chat::IContactBlobStore
{
  public:
    bool loadBlob(std::vector<uint8_t>& out) override
    {
        out.clear();
        std::vector<uint8_t> raw;
        if (!platform::ui::settings_store::get_blob(kNamespace, kKey, raw))
        {
            return false;
        }
        // Reject empty / truncated / wrong-magic blobs: hand the core nothing
        // rather than a malformed (or headered) buffer.
        if (raw.size() < kHeaderSize || raw[0] != kHeader[0] || raw[1] != kHeader[1] ||
            raw[2] != kHeader[2] || raw[3] != kHeader[3])
        {
            return false;
        }
        out.assign(raw.begin() + kHeaderSize, raw.end());
        return !out.empty();
    }

    bool saveBlob(const uint8_t* data, size_t len) override
    {
        if (len == 0)
        {
            // No contacts: erase the key (put_blob with len 0 removes it).
            return platform::ui::settings_store::put_blob(kNamespace, kKey, nullptr, 0);
        }
        // Prepend the 4-byte header, then the raw Entry[] payload.
        std::vector<uint8_t> buffer;
        buffer.reserve(kHeaderSize + len);
        buffer.insert(buffer.end(), kHeader, kHeader + kHeaderSize);
        if (data != nullptr)
        {
            buffer.insert(buffer.end(), data, data + len);
        }
        return platform::ui::settings_store::put_blob(
            kNamespace, kKey, buffer.data(), buffer.size());
    }

  private:
    static constexpr const char* kNamespace = "tm_contacts";
    static constexpr const char* kKey = "contacts_v1";
    static constexpr size_t kHeaderSize = 4;
    static constexpr uint8_t kHeader[kHeaderSize] = {'T', 'M', 'C', 0x01};
};

// Self-owning store wrappers: each bundles its volatile blob store with the
// portable core store so a single unique_ptr<INodeStore>/unique_ptr<IContactStore>
// in ContactServicesBundle keeps both alive with correct destruction order
// (core destroyed before its blob_store_ reference dangles).
class RamNodeStore final : public chat::contacts::NodeStoreCore
{
  public:
    RamNodeStore() : chat::contacts::NodeStoreCore(blob_store_) {}

  private:
    RamNodeBlobStore blob_store_;
};

// Contact store wrapper backed by the NVS contact blob store so contacts
// persist across reboot / reflash (see NvsContactBlobStore above). The node
// store stays RAM-backed (RamNodeStore) by design.
class NvsContactStore final : public chat::contacts::ContactStoreCore
{
  public:
    NvsContactStore() : chat::contacts::ContactStoreCore(blob_store_) {}

  private:
    NvsContactBlobStore blob_store_;
};

std::unique_ptr<chat::IChatStore> createChatStore()
{
    // v1: RAM-backed message store. The Arduino factory prefers an SD-backed
    // LogStore when a card is present; that branch is intentionally dropped here
    // to keep the minimal chat facade free of the SD/Arduino-SD dependency.
    return std::unique_ptr<chat::IChatStore>(new chat::RamStore());
}

std::unique_ptr<chat::ChatService::IncomingMessageObserver>
createChatMessageObserver(chat::ChatService& service)
{
    return std::unique_ptr<chat::ChatService::IncomingMessageObserver>(
        new chat::infra::ChatEventBusBridge(service));
}

} // namespace

app::ContactServicesBundle createIdfContactServices()
{
    app::ContactServicesBundle bundle;
    // Portable core stores (the Arduino meshtastic::NodeStore /
    // contacts::ContactStore shells are not pure-IDF buildable). Each wrapper
    // owns its blob store as a member:
    //   - node store: volatile RAM (RamNodeStore). Ephemeral RF telemetry that
    //     repopulates from live packets; NVS-backing it would wear the flash
    //     (NodeStoreCore saves every ~5s). On boot ContactService::begin()
    //     re-synthesizes a node record for each persisted contact, so no node
    //     persistence is needed.
    //   - contact store: NVS-backed (NvsContactStore) so contacts (id +
    //     nickname, user-authored) survive reboot / reflash.
    bundle.node_store =
        std::unique_ptr<chat::contacts::INodeStore>(new RamNodeStore());
    bundle.contact_store =
        std::unique_ptr<chat::contacts::IContactStore>(new NvsContactStore());
    if (!bundle.node_store || !bundle.contact_store)
    {
        return bundle;
    }

    bundle.service = std::unique_ptr<chat::contacts::ContactService>(
        new chat::contacts::ContactService(*bundle.node_store, *bundle.contact_store));
    if (bundle.service)
    {
        bundle.service->begin();
    }
    return bundle;
}

IdfChatRuntime createIdfChatRuntime(const app::AppConfig& config, LoraBoard& lora_board)
{
    IdfChatRuntime runtime;

    runtime.contacts = createIdfContactServices();

    app::ChatServicesBundle& chat = runtime.chat;
    chat.model = std::unique_ptr<chat::ChatModel>(new chat::ChatModel());
    if (!chat.model)
    {
        return runtime;
    }
    chat.model->setPolicy(config.chat_policy);

    chat.store = createChatStore();
    if (!chat.store)
    {
        return runtime;
    }

    // The radio adapter IS the mesh runtime. Build it from the board's LoRa
    // radio, retain a borrow through the shared chat::IMeshAdapter seam for the
    // facade (getMeshAdapter / inline pump), then hand ownership to the bundle's
    // mesh_runtime slot. Apply the active mesh config up front so the radio is
    // configured before first TX/RX.
    //
    // Protocol selection (Phase 2, boot-time): construct the MeshCore adapter when
    // config.mesh_protocol is MeshCore, otherwise the Meshtastic radio adapter.
    // Both implement chat::IMeshAdapter and take the same LoraBoard&, so the rest
    // of the wiring (borrow, move into the chat runtime, ChatService ctor) is
    // identical. config.activeMeshConfig() already returns the matching mesh
    // config for the selected protocol.
    std::unique_ptr<chat::IMeshAdapter> adapter;
    if (config.mesh_protocol == chat::MeshProtocol::MeshCore)
    {
        adapter.reset(new chat::meshcore::MeshCoreAdapter(lora_board));
    }
    else
    {
        adapter.reset(new platform::esp::radio::MeshtasticRadioAdapter(lora_board));
    }
    if (!adapter)
    {
        return runtime;
    }
    adapter->applyConfig(config.activeMeshConfig());

    // Boot marker: announce the active adapter's real node id (derived from its
    // identity inside applyConfig() above), analogous to the Meshtastic adapter's
    // "radio ready node=" line. The MeshCore boot self-test check greps for this.
    if (config.mesh_protocol == chat::MeshProtocol::MeshCore)
    {
        ESP_LOGI("idf-mc", "meshcore-ready node=%08lX",
                 static_cast<unsigned long>(adapter->getNodeId()));
    }

    runtime.mesh_adapter = adapter.get();             // non-owning borrow (IMeshAdapter)
    chat.mesh_runtime = std::move(adapter);            // owner (as IMeshAdapter)

    chat.service = std::unique_ptr<chat::ChatService>(new chat::ChatService(
        *chat.model, *chat.mesh_runtime, *chat.store, config.mesh_protocol));
    if (!chat.service)
    {
        runtime.mesh_adapter = nullptr;
        return runtime;
    }

    chat.incoming_message_observer = createChatMessageObserver(*chat.service);
    return runtime;
}

} // namespace platform::esp::idf_common

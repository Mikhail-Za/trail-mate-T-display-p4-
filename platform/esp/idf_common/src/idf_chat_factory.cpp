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

#include "board/LoraBoard.h"
#include "chat/domain/chat_model.h"
#include "chat/infra/contact_store_core.h"
#include "chat/infra/node_store_core.h"
#include "chat/infra/store/ram_store.h"
#include "chat/ports/i_contact_blob_store.h"
#include "chat/ports/i_node_blob_store.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "platform/esp/arduino_common/chat/infra/chat_event_bus_bridge.h"

namespace platform::esp::idf_common
{
namespace
{

// ---------------------------------------------------------------------------
// In-memory blob stores for the minimal RAM-only chat scope.
//
// NodeStoreCore / ContactStoreCore persist via an injected blob-store port.
// The Arduino shells (meshtastic::NodeStore / contacts::ContactStore) back that
// port with SD/NVS through storage/sd_card_runtime.h, which hard-includes
// <SPI.h> / <Arduino.h> / <Preferences.h> and so cannot build in the pure
// ESP-IDF target. v1 chat keeps contacts in RAM only (no on-device persistence
// across reboots), so these blob stores are volatile: load() returns the bytes
// last saved this boot (initially empty), save() retains them, clear() drops
// them. This is the IDF analogue of the Linux*BlobStore classes in
// platform/linux/common/src/app/linux_app_services.cpp.
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

class RamContactBlobStore final : public chat::IContactBlobStore
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

  private:
    std::vector<uint8_t> blob_;
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

class RamContactStore final : public chat::contacts::ContactStoreCore
{
  public:
    RamContactStore() : chat::contacts::ContactStoreCore(blob_store_) {}

  private:
    RamContactBlobStore blob_store_;
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
    // Portable core stores backed by volatile RAM blob stores (see above): the
    // Arduino meshtastic::NodeStore / contacts::ContactStore shells are not
    // pure-IDF buildable. Each wrapper owns its blob store as a member.
    bundle.node_store =
        std::unique_ptr<chat::contacts::INodeStore>(new RamNodeStore());
    bundle.contact_store =
        std::unique_ptr<chat::contacts::IContactStore>(new RamContactStore());
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
    // radio, retain a typed borrow for the facade (getMeshAdapter / inline pump),
    // then hand ownership to the bundle's mesh_runtime slot. Apply the active
    // mesh config up front so the radio is configured before first TX/RX.
    auto adapter = std::unique_ptr<platform::esp::radio::MeshtasticRadioAdapter>(
        new platform::esp::radio::MeshtasticRadioAdapter(lora_board));
    if (!adapter)
    {
        return runtime;
    }
    adapter->applyConfig(config.activeMeshConfig());

    runtime.mesh_adapter = adapter.get();             // non-owning borrow
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

/**
 * @file idf_chat_facade.cpp
 * @brief Minimal ESP-IDF IAppFacade for on-device LoRa text chat.
 *
 * Modeled on platform/linux/common/src/app/linux_app_facade.cpp. Implements the
 * MUST methods (messaging / config getters / runtime chat-UI handle / the
 * updateCoreServices pump) and stubs team / admin / BLE exactly as the Linux
 * facade does.
 *
 * THE HOT PATH (see THE BIG RISK in IDF-FACADE-PLAN.md): the Arduino build
 * serviced the radio from a background FreeRTOS task. This facade has no such
 * task, so updateCoreServices() must pump the radio (RX + processSendQueue) and
 * drain the chat event queue every tick or the chat UI renders but sends/receives
 * nothing.
 */

#include "platform/esp/idf_common/idf_chat_facade.h"

#include <cstring>

#include "esp_log.h"

#include "app/app_facade_access.h"
#include "board/LoraBoard.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "platform/esp/boards/board_runtime.h"
#include "sys/event_bus.h"
#include "ui/chat_ui_runtime.h"

namespace platform::esp::idf_common
{
namespace
{

void copyBounded(char* out, std::size_t out_len, const char* text)
{
    if (out == nullptr || out_len == 0)
    {
        return;
    }
    if (text == nullptr)
    {
        out[0] = '\0';
        return;
    }
    std::strncpy(out, text, out_len - 1U);
    out[out_len - 1U] = '\0';
}

// Translate a NodeInfoUpdate event into a contacts NodeUpdate and apply it.
// Mirrors platform/esp/arduino_common/src/app_runtime_support.cpp dispatchEvent().
void applyNodeInfoEvent(::chat::contacts::ContactService& contacts,
                        const ::sys::NodeInfoUpdateEvent& node_event)
{
    ::chat::contacts::NodeUpdate update{};
    update.short_name = node_event.short_name;
    update.long_name = node_event.long_name;
    update.has_last_seen = true;
    update.last_seen = node_event.timestamp;
    update.has_snr = true;
    update.snr = node_event.snr;
    update.has_rssi = true;
    update.rssi = node_event.rssi;
    update.has_protocol = true;
    update.protocol = node_event.protocol;
    update.has_role = true;
    update.role = node_event.role;
    update.has_hops_away = true;
    update.hops_away = node_event.hops_away;
    update.has_hw_model = true;
    update.hw_model = node_event.hw_model;
    update.has_channel = true;
    update.channel = node_event.channel;
    update.has_macaddr = node_event.has_macaddr;
    if (node_event.has_macaddr)
    {
        std::memcpy(update.macaddr, node_event.macaddr, sizeof(update.macaddr));
    }
    update.has_via_mqtt = true;
    update.via_mqtt = node_event.via_mqtt;
    update.has_is_ignored = true;
    update.is_ignored = node_event.is_ignored;
    update.has_public_key = true;
    update.public_key_present = node_event.has_public_key;
    update.has_key_manually_verified = true;
    update.key_manually_verified = node_event.key_manually_verified;
    update.has_device_metrics = node_event.has_device_metrics;
    if (node_event.has_device_metrics)
    {
        update.device_metrics = node_event.device_metrics;
    }
    contacts.applyNodeUpdate(node_event.node_id, update);
}

void applyNodePositionEvent(::chat::contacts::ContactService& contacts,
                            const ::sys::NodePositionUpdateEvent& pos_event)
{
    ::chat::contacts::NodePosition pos{};
    pos.valid = true;
    pos.latitude_i = pos_event.latitude_i;
    pos.longitude_i = pos_event.longitude_i;
    pos.has_altitude = pos_event.has_altitude;
    pos.altitude = pos_event.altitude;
    pos.timestamp = pos_event.timestamp;
    pos.precision_bits = pos_event.precision_bits;
    pos.pdop = pos_event.pdop;
    pos.hdop = pos_event.hdop;
    pos.vdop = pos_event.vdop;
    pos.gps_accuracy_mm = pos_event.gps_accuracy_mm;
    contacts.updateNodePosition(pos_event.node_id, pos);
}

} // namespace

IdfChatFacade::IdfChatFacade(LoraBoard& lora_board, BoardBase* board)
    : lora_board_(lora_board), board_(board)
{
    // config_ is value-initialized (member initializer `config_{}`), which runs
    // AppConfig's default constructor and seeds all chat/mesh defaults. The
    // caller may overwrite getConfig() before calling initialize().
}

IdfChatFacade::~IdfChatFacade()
{
    shutdown();
}

bool IdfChatFacade::initialize()
{
    if (initialized_)
    {
        return true;
    }

    // Create the global EventBus queue BEFORE the chat runtime/bridge is built.
    // On Arduino this happens in app_context.cpp; the IDF startup path never did
    // it, so the queue stayed nullptr and every ChatNewMessageEvent the bridge
    // published was dropped (EventBus::publish frees the event and returns false
    // when queue_ == nullptr). That broke live refresh of an open chat thread on
    // received messages and delivery receipts. init() is idempotent (returns true
    // if the queue already exists), so a repeat call is harmless. Same global
    // sys::EventBus the ChatEventBusBridge publishes to and pumpMeshAndDrainEvents
    // drains.
    ::sys::EventBus::init(32U);

    runtime_ = createIdfChatRuntime(config_, lora_board_);
    if (!runtime_.isValid())
    {
        return false;
    }

    // The radio adapter derives the self node id from the SX126x board's efuse
    // MAC in its constructor (initNodeIdentity), so this is valid now and matches
    // the id the radio actually transmits. Avoids depending on the Arduino-only
    // device_identity.h (which pulls in <Preferences.h>, unavailable in the pure
    // ESP-IDF build).
    self_node_id_ = runtime_.mesh_adapter->getNodeId();

    if (!::app::hasAppFacade())
    {
        ::app::bindAppFacade(*this);
        bound_ = true;
    }

    initialized_ = true;
    return true;
}

void IdfChatFacade::shutdown()
{
    if (bound_ && ::app::hasAppFacade())
    {
        ::app::unbindAppFacade();
    }
    bound_ = false;
    initialized_ = false;
}

bool IdfChatFacade::isInitialized() const noexcept
{
    return initialized_;
}

// ---------------------------------------------------------------------------
// IAppConfigFacade
// ---------------------------------------------------------------------------

::app::AppConfig& IdfChatFacade::getConfig()
{
    return config_;
}

const ::app::AppConfig& IdfChatFacade::getConfig() const
{
    return config_;
}

void IdfChatFacade::saveConfig()
{
    // v1: config is in-memory only; persistence is out of scope for the minimal
    // chat facade. No-op (matches the linux facade's stub posture for unused
    // surfaces).
}

void IdfChatFacade::applyMeshConfig()
{
    if (::chat::IMeshAdapter* adapter = getMeshAdapter())
    {
        adapter->applyConfig(config_.activeMeshConfig());
    }
}

void IdfChatFacade::applyUserInfo()
{
    if (::chat::IMeshAdapter* adapter = getMeshAdapter())
    {
        adapter->setUserInfo(config_.node_name, config_.short_name);
    }
}

void IdfChatFacade::applyPositionConfig()
{
    // No GPS in the minimal chat facade.
}

void IdfChatFacade::applyNetworkLimits()
{
    // v1: not wired.
}

void IdfChatFacade::applyPrivacyConfig()
{
    // v1: not wired.
}

void IdfChatFacade::applyChatDefaults()
{
    if (runtime_.chat.model)
    {
        runtime_.chat.model->setPolicy(config_.chat_policy);
    }
}

::chat::MeshProtocol IdfChatFacade::getMeshProtocol() const
{
    return config_.mesh_protocol;
}

void IdfChatFacade::getEffectiveUserInfo(char* out_long,
                                         std::size_t long_len,
                                         char* out_short,
                                         std::size_t short_len) const
{
    copyBounded(out_long, long_len, config_.node_name);
    copyBounded(out_short, short_len, config_.short_name);
}

bool IdfChatFacade::switchMeshProtocol(::chat::MeshProtocol protocol, bool persist)
{
    // v1: single-protocol (Meshtastic radio adapter). Accept a no-op switch to
    // the active protocol; reject anything else rather than silently lying.
    (void)persist;
    return protocol == config_.mesh_protocol;
}

// ---------------------------------------------------------------------------
// IAppMessagingFacade
// ---------------------------------------------------------------------------

::chat::ChatService& IdfChatFacade::getChatService()
{
    return *runtime_.chat.service;
}

::chat::contacts::ContactService& IdfChatFacade::getContactService()
{
    return *runtime_.contacts.service;
}

::chat::IMeshAdapter* IdfChatFacade::getMeshAdapter()
{
    return runtime_.mesh_adapter;
}

const ::chat::IMeshAdapter* IdfChatFacade::getMeshAdapter() const
{
    return runtime_.mesh_adapter;
}

::chat::NodeId IdfChatFacade::getSelfNodeId() const
{
    return self_node_id_;
}

// ---------------------------------------------------------------------------
// IAppTeamFacade -- stubbed (the chat screen guards a null team controller)
// ---------------------------------------------------------------------------

::team::TeamController* IdfChatFacade::getTeamController()
{
    return nullptr;
}

::team::TeamPairingService* IdfChatFacade::getTeamPairing()
{
    return nullptr;
}

::team::TeamService* IdfChatFacade::getTeamService()
{
    return nullptr;
}

const ::team::TeamService* IdfChatFacade::getTeamService() const
{
    return nullptr;
}

::team::TeamTrackSampler* IdfChatFacade::getTeamTrackSampler()
{
    return nullptr;
}

void IdfChatFacade::setTeamModeActive(bool active)
{
    (void)active;
}

// ---------------------------------------------------------------------------
// IAppAdminFacade
// ---------------------------------------------------------------------------

void IdfChatFacade::broadcastNodeInfo()
{
    if (runtime_.mesh_adapter)
    {
        // Route through the protocol-agnostic IMeshAdapter seam so this works for
        // both the Meshtastic and MeshCore adapters (mesh_adapter is borrowed as
        // chat::IMeshAdapter*). For the Meshtastic adapter SendIdBroadcast maps
        // straight to its broadcastNodeInfo(); the MeshCore adapter emits its own
        // identity/advert broadcast for the same action.
        (void)runtime_.mesh_adapter->triggerDiscoveryAction(
            ::chat::MeshDiscoveryAction::SendIdBroadcast);
    }
}

void IdfChatFacade::clearNodeDb()
{
    if (runtime_.contacts.node_store)
    {
        runtime_.contacts.node_store->clear();
    }
}

void IdfChatFacade::clearMessageDb()
{
    if (runtime_.chat.service)
    {
        runtime_.chat.service->clearAllMessages();
    }
}

// ---------------------------------------------------------------------------
// IAppRuntimeFacade
// ---------------------------------------------------------------------------

::ble::BleManager* IdfChatFacade::getBleManager()
{
    return nullptr;
}

const ::ble::BleManager* IdfChatFacade::getBleManager() const
{
    return nullptr;
}

bool IdfChatFacade::isBleEnabled() const
{
    return false;
}

void IdfChatFacade::setBleEnabled(bool enabled)
{
    (void)enabled;
}

void IdfChatFacade::restartDevice()
{
    // v1: no device-restart wiring in the minimal facade.
}

::chat::ui::IChatUiRuntime* IdfChatFacade::getChatUiRuntime()
{
    return chat_ui_runtime_;
}

void IdfChatFacade::setChatUiRuntime(::chat::ui::IChatUiRuntime* runtime)
{
    chat_ui_runtime_ = runtime;
}

::BoardBase* IdfChatFacade::getBoard()
{
    return board_;
}

const ::BoardBase* IdfChatFacade::getBoard() const
{
    return board_;
}

// ---------------------------------------------------------------------------
// IAppLifecycleFacade
// ---------------------------------------------------------------------------

void IdfChatFacade::updateCoreServices()
{
    // THE hot method. The IDF loop (tickBoundLifecycle) calls all three lifecycle
    // methods, but tickEventRuntime()/dispatchPendingEvents() are no-ops here, so
    // ALL per-tick chat work happens in this single call:
    //   1. pump the radio (RX poll + TX drain) -- replaces the dropped Arduino
    //      background radio task,
    //   2. move received frames into the chat model/store (fires the event-bus
    //      bridge),
    //   3. drain the event queue: resolve send status, update contacts, and feed
    //      the chat UI so bubbles/receipts render.
    pumpMeshAndDrainEvents(32U);
}

void IdfChatFacade::tickEventRuntime()
{
    // No-op: pumping is inline in updateCoreServices() (see plan -- STUB list).
}

void IdfChatFacade::dispatchPendingEvents(std::size_t max_events)
{
    // No-op: the event drain is inline in updateCoreServices() because the radio
    // pump and the event drain must run together each tick and the other two
    // lifecycle hooks are stubbed. (See plan -- STUB list.)
    (void)max_events;
}

void IdfChatFacade::pumpMeshAndDrainEvents(std::size_t max_events)
{
    // TEMP DIAG (radio pump trace): prove this runs on-device and show guard state.
    static uint32_t s_pump_n = 0;
    const bool gated = (!initialized_ || !runtime_.isValid());
    if (s_pump_n < 5U || (s_pump_n % 256U) == 0U)
    {
        ESP_LOGI("idf-pump", "pump n=%lu gated=%d adapter=%p",
                 static_cast<unsigned long>(s_pump_n), gated ? 1 : 0,
                 static_cast<void*>(runtime_.mesh_adapter));
    }
    ++s_pump_n;

    if (gated)
    {
        return;
    }

    ::chat::ChatService& chat_service = *runtime_.chat.service;

    // 1. Pump the radio. MeshtasticRadioAdapter::processSendQueue() internally
    //    polls the SX126x for RX, drains the TX queue, and emits the one-time
    //    NodeInfo broadcast. This is the entire replacement for the Arduino
    //    meshTask loop.
    if (runtime_.mesh_adapter)
    {
        runtime_.mesh_adapter->processSendQueue();
    }

    // 2. Drain adapter RX into the chat model/store. This invokes the
    //    ChatEventBusBridge, which publishes ChatNewMessage / node events.
    chat_service.processIncoming();
    chat_service.flushStore();

    // 3. Drain the event bus. Ownership rule: subscribe() yields a heap Event*
    //    that we must delete unless we hand it to chat_ui_runtime_->onChatEvent(),
    //    which takes ownership. Routing mirrors the Arduino dispatchEvent() +
    //    handleUiEvent() split, folded into one loop.
    ::sys::Event* event = nullptr;
    for (std::size_t processed = 0;
         processed < max_events && ::sys::EventBus::subscribe(&event, 0);)
    {
        if (event == nullptr)
        {
            continue;
        }
        ++processed;

        switch (event->type)
        {
        case ::sys::EventType::ChatSendResult:
        {
            auto* result = static_cast<::sys::ChatSendResultEvent*>(event);
            chat_service.handleSendResult(result->msg_id, result->success);
            // Fall through to the UI feed below so the chat page can update the
            // message's delivery state (matches Arduino: dispatchEvent returns
            // false for ChatSendResult and lets handleUiEvent forward it).
            break;
        }
        case ::sys::EventType::NodeInfoUpdate:
            applyNodeInfoEvent(getContactService(),
                               *static_cast<::sys::NodeInfoUpdateEvent*>(event));
            delete event;
            continue;
        case ::sys::EventType::NodeProtocolUpdate:
        {
            auto* node_event = static_cast<::sys::NodeProtocolUpdateEvent*>(event);
            getContactService().updateNodeProtocol(node_event->node_id,
                                                   node_event->protocol,
                                                   node_event->timestamp);
            delete event;
            continue;
        }
        case ::sys::EventType::NodePositionUpdate:
            applyNodePositionEvent(getContactService(),
                                   *static_cast<::sys::NodePositionUpdateEvent*>(event));
            delete event;
            continue;
        default:
            break;
        }

        // UI feed for chat-relevant events (ChatNewMessage, ChatSendResult,
        // channel/unread changes, key-verification). onChatEvent() consumes the
        // event; if no UI runtime is attached we own it and must free it.
        //
        // Thread safety: onChatEvent() reaches into LVGL (reloadConversationView)
        // and the LVGL render task runs separately, so this single UI feed must be
        // serialized against it via the display lock. The lock wraps ONLY this
        // call, never the radio pump (processSendQueue) above or the non-UI node
        // events (which already continued out of the loop). Use a bounded timeout;
        // if the lock cannot be taken, defer this event (the message is already in
        // the store, so the next pump tick retries the UI feed) rather than calling
        // LVGL unlocked.
        if (chat_ui_runtime_ != nullptr)
        {
            constexpr uint32_t kUiLockTimeoutMs = 150U;
            if (::platform::esp::boards::lockDisplay(kUiLockTimeoutMs))
            {
                chat_ui_runtime_->onChatEvent(event);
                ::platform::esp::boards::unlockDisplay();
            }
            else
            {
                // Lock contended: hand the event back to the bus and stop draining
                // for this tick. The next updateCoreServices() tick retries the UI
                // feed instead of busy-spinning here against a still-held lock. If
                // the queue is full, publish frees the event (last-resort drop; the
                // message is already in the store, so re-entry still shows it).
                ::sys::EventBus::publish(event, 0U);
                break;
            }
        }
        else
        {
            delete event;
        }
    }
}

} // namespace platform::esp::idf_common

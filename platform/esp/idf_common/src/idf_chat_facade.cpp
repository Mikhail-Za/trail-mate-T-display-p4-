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
#include "platform/esp/idf_common/sx126x_radio.h"
#include "platform/esp/idf_common/team/idf_nvs_team_ui_snapshot_store.h"
#include "platform/ui/team_ui_snapshot_store.h"
#include "sys/event_bus.h"
#include "team/protocol/team_mgmt.h"
#include "team/protocol/team_portnum.h"
#include "ui/chat_ui_runtime.h"
#include "ui/screens/team/team_page_shell.h"

namespace platform::esp::idf_common
{
namespace
{

// How often to broadcast the team presence heartbeat while keys are held. The
// online window the peer uses is 120 s (ui::team_presence::kDefaultOnlineWindowSeconds);
// 25 s gives ~4 heartbeats per window so a single dropped frame never lapses a
// peer to "stale", while staying light on LoRa airtime (a status frame is tiny).
constexpr uint32_t kTeamPresenceIntervalMs = 25000U;

// Throttle the team SystemTick to ~1 Hz. pumpMeshAndDrainEvents runs many times a
// second; publishing a SystemTick (and routing it through the team page handler)
// every iteration floods the main loop and starves LVGL/touch -- the pairing screen
// went unresponsive. 1 Hz is ample to drive the team page's scheduled status-
// rebroadcast / keydist-retry work (the Linux facade ticks at a controlled rate too).
constexpr uint32_t kTeamSystemTickIntervalMs = 1000U;

// True for the team runtime events the team page handler owns, plus SystemTick
// (which the team page uses to drive periodic status/keydist work). Mirrors
// app_event_runtime_support.cpp::isTeamRuntimeEvent on the Arduino path.
bool isTeamRuntimeOrTickEvent(::sys::EventType type)
{
    switch (type)
    {
    case ::sys::EventType::TeamKick:
    case ::sys::EventType::TeamTransferLeader:
    case ::sys::EventType::TeamKeyDist:
    case ::sys::EventType::TeamKeyRequest:
    case ::sys::EventType::TeamStatus:
    case ::sys::EventType::TeamPosition:
    case ::sys::EventType::TeamWaypoint:
    case ::sys::EventType::TeamTrack:
    case ::sys::EventType::TeamChat:
    case ::sys::EventType::TeamPairing:
    case ::sys::EventType::TeamError:
    case ::sys::EventType::SystemTick:
        return true;
    default:
        return false;
    }
}

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

    // Load the persisted 8-slot Meshtastic channel config (NvsChannelBlobStore)
    // BEFORE building the runtime so the radio adapter starts from the stored
    // channels. On first boot (no blob), this migrates the legacy
    // primary_/secondary_ scalars into slots 0/1 so channel 0 is unchanged.
    (void)loadChannelConfigFromNvs(config_);

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

    // Push the configured user name to the radio adapter at startup so the first
    // (and periodic) NodeInfo announce carries a name. Without this the IDF path
    // never called applyUserInfo(), so every boot announced an empty name and
    // peers rendered us as a hex id.
    applyUserInfo();

    // Construct the team services now that runtime_.mesh_adapter is valid. This
    // makes getTeamController()/getTeamService() return real objects, so the Team
    // screen enables Create/Join (its actions are gated on a non-null controller).
    initTeamServices();

    if (!::app::hasAppFacade())
    {
        ::app::bindAppFacade(*this);
        bound_ = true;
    }

    initialized_ = true;
    return true;
}

void IdfChatFacade::initTeamServices()
{
    if (runtime_.mesh_adapter == nullptr)
    {
        return;
    }

    // Reboot persistence: install the durable NVS-backed Team UI snapshot store
    // BEFORE anything builds the team UI, replacing the default RAM-only
    // TeamUiSnapshotMemoryStore (wiped on every boot). This terminates the
    // existing team-key save/restore seam (team_ui_save_keys_now -> store.save();
    // the team page's loadOnce/applySnapshot -> store.load()) in NVS flash, so a
    // paired team's PSK + key_id + team_id survive reboot / reflash and the team
    // page rehydrates its state (incl. PSK) from flash. Function-local static: the
    // store pointer is held globally by team_ui_set_snapshot_store and must outlive
    // this facade; initTeamServices() runs exactly once.
    static team_infra::IdfNvsTeamUiSnapshotStore s_nvs_team_ui_store;
    team::ui::team_ui_set_snapshot_store(&s_nvs_team_ui_store);

    // One-time crypto self-check: if mbedtls ChaCha20-Poly1305/SHA256 is misbuilt
    // (e.g. the Kconfig flags are off), this logs FAILED so team frames that would
    // silently fail to interop with the rweather peers are caught loudly. It does
    // not block construction (the UI still enables; only on-air interop depends on
    // crypto correctness).
    (void)team_infra::IdfTeamCrypto::runSelfTest();

    // Mirror create_team_services() (Arduino app_context_platform_bindings.cpp):
    // TeamService(crypto, mesh_adapter, event_sink, runtime) -> TeamController ->
    // TeamTrackSampler(runtime, track_source). The ports are facade members
    // (team_crypto_/team_runtime_/team_event_sink_/team_track_source_) and outlive
    // these services.
    team_service_.reset(new ::team::TeamService(
        team_crypto_, *runtime_.mesh_adapter, team_event_sink_, team_runtime_));
    team_controller_.reset(new ::team::TeamController(*team_service_));
    team_track_sampler_.reset(
        new ::team::TeamTrackSampler(team_runtime_, team_track_source_));

    // Reboot persistence (part 3): re-inject any persisted team keys into the live
    // TeamService at boot. The team page's loadOnce/applySnapshot rehydrates the UI
    // snapshot from the NVS store above, but the TeamService itself starts keyless;
    // without this, RX decrypt + presence stay dark until the user re-pairs even
    // though the keys are on flash. setKeysFromPsk re-derives the four sub-keys from
    // the stored PSK so hasKeys() == true from boot. Idempotent and a safe no-op
    // when no team was saved (load() returns false).
    {
        team::ui::TeamUiSnapshot snap;
        if (team::ui::team_ui_snapshot_store().load(snap) && snap.has_team_psk &&
            snap.has_team_id)
        {
            const bool keyed = team_controller_->setKeysFromPsk(
                snap.team_id, snap.security_round, snap.team_psk.data(),
                snap.team_psk.size());
            ESP_LOGI("idf-team", "boot key restore from NVS: %s",
                     keyed ? "keys set" : "no/!invalid keys");
        }
    }

    // Phase 2: construct the LoRa pairing service so getTeamPairing() is non-null
    // and the Team screen's Create/Join actually pair two units over LoRa. It
    // drives the shared TeamPairingCoordinator over the same mesh adapter on
    // TEAM_PAIR_APP. The coordinator publishes pairing events through the service
    // (which applies the Option-A passphrase PSK derivation) and on to the real
    // pairing event sink. RX: TeamService::processIncoming() drains the adapter
    // data queue and hands any portnum it does not own (i.e. TEAM_PAIR_APP) to the
    // router below, which forwards it into the pairing transport.
    team_pairing_.reset(new team_infra::IdfLoraTeamPairingService(
        team_runtime_, team_pairing_event_sink_, team_crypto_, *runtime_.mesh_adapter));
    pairing_app_data_router_.reset(new PairingAppDataRouter(*this));
    team_service_->setUnhandledAppDataObserver(pairing_app_data_router_.get());

    // appselftest harness + owner-facing trace: prove the controller AND pairing
    // service are live so a serial capture confirms the Team screen will enable
    // Create/Join AND can pair over LoRa.
    ESP_LOGI("idf-team", "team services constructed controller=%p service=%p pairing=%p",
             static_cast<void*>(team_controller_.get()),
             static_cast<void*>(team_service_.get()),
             static_cast<void*>(team_pairing_.get()));
}

void IdfChatFacade::PairingAppDataRouter::onUnhandledAppData(const ::chat::MeshIncomingData& msg)
{
    // Only LoRa pairing frames are routed to the pairing transport; every other
    // unhandled portnum is ignored here (the IDF build has no other unhandled-app
    // consumer). The coordinator/transport re-validate the wire, so a spurious
    // frame on this port is harmless.
    if (msg.portnum != ::team::proto::TEAM_PAIR_APP)
    {
        return;
    }
    if (facade_.team_pairing_)
    {
        facade_.team_pairing_->deliverPairingFrame(
            msg.from, msg.payload.data(), msg.payload.size());
    }
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
    // Persist the channel config (NvsChannelBlobStore) so user channel edits
    // survive reboot / reflash. applyMeshConfig() is the funnel the settings UI
    // calls after any mesh edit, so saving here covers every channel change; it
    // is user-action-gated and infrequent (same posture as the contacts store).
    if (config_.mesh_protocol == ::chat::MeshProtocol::Meshtastic)
    {
        saveChannelConfigToNvs(config_);
    }
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
// IAppTeamFacade -- Phase 1: real TeamService/TeamController/TeamTrackSampler.
// getTeamPairing() stays null until Phase 2 (LoRa pairing transport); the team
// page guards a null pairing service everywhere.
// ---------------------------------------------------------------------------

::team::TeamController* IdfChatFacade::getTeamController()
{
    return team_controller_.get();
}

::team::TeamPairingService* IdfChatFacade::getTeamPairing()
{
    // Phase 2: the LoRa pairing service is live. Returning it non-null makes the
    // Team screen's pairing UX (TeamPagePairingPortAdapter) active so Create
    // beacons and Join scans over LoRa (TEAM_PAIR_APP). Confidentiality (the PSK
    // off the air) is enforced inside the service via the Option-A passphrase.
    return team_pairing_.get();
}

::team::TeamService* IdfChatFacade::getTeamService()
{
    return team_service_.get();
}

const ::team::TeamService* IdfChatFacade::getTeamService() const
{
    return team_service_.get();
}

::team::TeamTrackSampler* IdfChatFacade::getTeamTrackSampler()
{
    return team_track_sampler_.get();
}

void IdfChatFacade::setTeamModeActive(bool active)
{
    // Track the flag so the per-tick pump can drive the track sampler only while
    // team mode is active (the Arduino app_runtime_support.cpp does the same). On
    // a board without a GPS fix the sampler degrades gracefully (no shared point).
    team_mode_active_ = active;
}

bool IdfChatFacade::setTeamPairingPassphrase(const char* passphrase)
{
    // Option A floor: forward the shared passphrase to the LoRa pairing service so
    // both units derive PSK = sha256(passphrase||team_id) locally and the PSK
    // never crosses the air. Callable from any UI seam (e.g. a settings/team text
    // modal) without changing the cross-platform team-pairing port signatures.
    if (!team_pairing_)
    {
        return false;
    }
    team_pairing_->setPassphrase(passphrase);
    return true;
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
    //
    //    EXCEPTION: while an exclusive radio mode holds the chip (walkie-talkie has
    //    reconfigured the SX1262 for FSK voice), skip the pump entirely. Touching
    //    the radio here would poll/reconfigure it back toward LoRa receive and
    //    corrupt the in-flight voice session. The walkie path releases the hold
    //    (Sx126xRadio::setExclusiveHold(false)) on exit, after which the very next
    //    tick re-arms LoRa receive. The event drain below is radio-free and still
    //    runs so the UI stays responsive.
    if (runtime_.mesh_adapter &&
        !platform::esp::idf_common::Sx126xRadio::instance().hasExclusiveHold())
    {
        runtime_.mesh_adapter->processSendQueue();
    }

    // 2. Drain adapter RX into the chat model/store. This invokes the
    //    ChatEventBusBridge, which publishes ChatNewMessage / node events.
    chat_service.processIncoming();
    chat_service.flushStore();

    // 2b. Team service tick. TeamService::processIncoming() ALSO polls the
    //     adapter's data queue (the same single queue ChatService polls) and
    //     dispatches TEAM_MGMT/TEAM_POSITION/TEAM_WAYPOINT/TEAM_TRACK/TEAM_CHAT
    //     frames (portnums 300-304), emitting team events onto the EventBus that
    //     step 3 below drains this same tick. This is SAFE because ChatService's
    //     data-observer list is empty in this facade, so chat_service
    //     .processIncoming() never drains the data queue (it polls only text +
    //     early-returns on no data observers); the team frames are still present
    //     when team polls. Mirrors app_runtime_support.cpp::updateCoreServices
    //     (chat then team). If an exclusive radio hold is active (walkie voice),
    //     the adapter queue is simply empty, so this is a cheap no-op.
    //
    //     Phase 2: processIncoming() ALSO hands any TEAM_PAIR_APP frame (a portnum
    //     it does not own) to pairing_app_data_router_, which buffers it in
    //     team_pairing_. team_pairing_->update() below then drains that buffer into
    //     the pairing coordinator and advances its state machine (beacon cadence,
    //     join retries, timeouts). Driving pairing right after team_service_ keeps
    //     RX-buffer -> coordinator handoff within a single tick.
    if (team_service_)
    {
        team_service_->processIncoming();

        if (team_pairing_)
        {
            team_pairing_->update();
        }

        // Drive team-mode + the track sampler exactly as the Arduino path does:
        // team is "active" once keys are set, and the sampler periodically reads
        // the GPS fix to share position (degrades to no-op without a fix).
        const bool team_active = team_service_->hasKeys();
        setTeamModeActive(team_active);
        if (team_track_sampler_)
        {
            team_track_sampler_->update(team_controller_.get(), team_active);
        }

        // Periodic team presence heartbeat (GPS-free liveness so a peer stays
        // marked online), then a SystemTick so the team page runs its periodic
        // status/keydist work. Both are no-ops until a team has keys; the
        // SystemTick is always published so an in-team leader's scheduled status
        // rebroadcast fires even with the team screen closed.
        tickTeamPresence();
        publishTeamSystemTick();
    }

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

        // Team events (and SystemTick) go to the team page handler, NOT the chat
        // UI runtime. This is the fix for the "member stuck on Scanning / leader
        // shows member stale" bugs: the team page reducer is what applies
        // TeamKeyDist/TeamPairing to the team UI snapshot (in_team, members,
        // setKeysFromPsk) and advances the screen, and its SystemTick branch drives
        // status/keydist rebroadcasts. The Arduino build routes these the same way
        // (app_event_runtime_support.cpp handleUiEvent); the IDF facade previously
        // dropped them into onChatEvent(), where the team UI never saw them.
        // routeTeamEvent() takes ownership (deletes) when it consumes the event.
        if (routeTeamEvent(event))
        {
            continue;
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

bool IdfChatFacade::routeTeamEvent(::sys::Event* event)
{
    if (event == nullptr || !isTeamRuntimeOrTickEvent(event->type))
    {
        return false;
    }
    // team::ui::shell::handle_event runs the team page reducer: applies key dist /
    // pairing / status to the team UI snapshot (in_team, members, roster,
    // setKeysFromPsk), advances the page off Scanning/Waiting, and on SystemTick
    // drives process_status_broadcasts()/process_keydist_retries(). It does NOT
    // take ownership of the event, so we delete it here (matches the Arduino
    // handleUiEvent team branch). Safe even when the team screen is closed: the
    // handler updates the snapshot/controller and only repaints if active.
    //
    // THREAD SAFETY: handle_event repaints the team page (render_page -> lv_obj_clean
    // + rebuild), and the LVGL render task (taskLVGL) runs separately. This MUST be
    // serialized against it with the display lock -- exactly like the chat UI feed in
    // the drain loop above (see kUiLockTimeoutMs there). Without it, the app-loop
    // repaint raced taskLVGL on the same widgets (interleaved lv_obj_clean/rebuild)
    // and duplicated/churned the pairing screen's Retry/Cancel buttons. On lock
    // contention, defer (re-publish) rather than drop, so a pairing/keydist state
    // transition is never lost.
    constexpr uint32_t kTeamUiLockTimeoutMs = 150U;
    if (::platform::esp::boards::lockDisplay(kTeamUiLockTimeoutMs))
    {
        (void)::team::ui::shell::handle_event(nullptr, event);
        ::platform::esp::boards::unlockDisplay();
        delete event;
    }
    else
    {
        ::sys::EventBus::publish(event, 0U);
    }
    return true;
}

void IdfChatFacade::publishTeamSystemTick()
{
    // The team page's SystemTick handler is the only driver of the leader's
    // scheduled status rebroadcast and keydist retries. Nothing else publishes
    // SystemTick on IDF (the Linux facade did, in tickEventRuntime()). Published
    // unconditionally and cheaply; the team page early-outs when there is no
    // pending status/keydist work. routeTeamEvent() (in the same pump tick's drain)
    // will deliver it to the team page. THROTTLED to ~1 Hz (kTeamSystemTickIntervalMs):
    // the pump runs many times a second, and a SystemTick every iteration floods the
    // main loop + the team page handler, freezing LVGL/touch on the pairing screen.
    const uint32_t now_ms = team_runtime_.nowMillis();
    if (team_systick_last_ms_ != 0 && (now_ms - team_systick_last_ms_) < kTeamSystemTickIntervalMs)
    {
        return;
    }
    team_systick_last_ms_ = now_ms;
    ::sys::EventBus::publish(new ::sys::Event(::sys::EventType::SystemTick), 0);
}

void IdfChatFacade::tickTeamPresence()
{
    if (team_service_ == nullptr || team_controller_ == nullptr)
    {
        return;
    }

    const bool has_keys = team_service_->hasKeys();
    if (!has_keys)
    {
        // Left the team (or never joined): reset so the next join heartbeats at once.
        team_presence_had_keys_ = false;
        team_presence_last_tx_ms_ = 0;
        return;
    }

    const uint32_t now_ms = team_runtime_.nowMillis();
    const bool first_tick_with_keys = !team_presence_had_keys_;
    team_presence_had_keys_ = true;

    const bool due =
        first_tick_with_keys || team_presence_last_tx_ms_ == 0 ||
        (now_ms - team_presence_last_tx_ms_) >= kTeamPresenceIntervalMs;
    if (!due)
    {
        return;
    }
    team_presence_last_tx_ms_ = now_ms;

    // Minimal presence beacon: a TeamStatus with NO member roster. On the peer,
    // reduceStatus() touch-updates THIS unit's last_seen (marking us online)
    // without disturbing the peer's roster, because applyStatusRoster() ignores a
    // status whose has_members is false. The leader's authoritative roster still
    // flows via its own (member-listed) status rebroadcast. Sent both encrypted
    // (the live team channel) and plain so a peer that has keys, and one mid-key-
    // setup, both receive it -- mirrors the team page's dual status send.
    ::team::proto::TeamStatus presence{};
    presence.has_members = false; // liveness only; do not assert a roster

    const bool sent_enc =
        team_controller_->onStatus(presence, ::chat::ChannelId::PRIMARY, 0);
    const bool sent_plain =
        team_controller_->onStatusPlain(presence, ::chat::ChannelId::PRIMARY, 0);

    ESP_LOGI("idf-team",
             "presence heartbeat TX self=%08lX enc=%d plain=%d (online-keepalive)",
             static_cast<unsigned long>(self_node_id_), sent_enc ? 1 : 0,
             sent_plain ? 1 : 0);
}

} // namespace platform::esp::idf_common

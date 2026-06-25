/**
 * @file idf_chat_facade.h
 * @brief Minimal ESP-IDF IAppFacade for on-device LoRa text chat.
 *
 * Modeled on platform/linux/common/src/app/linux_app_facade.cpp: implements the
 * IAppFacade interface family, backs the messaging/config/runtime getters with a
 * real chat stack, and stubs team / admin / BLE the same way the Linux facade
 * does.
 *
 * v1 scope = chat only (no Wi-Fi/BLE/ESP-NOW, team, admin, map). The radio is a
 * platform::esp::radio::MeshtasticRadioAdapter built from the board's LoraBoard
 * by idf_chat_factory.
 *
 * THE HOT PATH: the Arduino build serviced the radio from a background FreeRTOS
 * task. This facade drops that task, so updateCoreServices() pumps the mesh
 * adapter (RX + processSendQueue) and drains the chat event queue every tick.
 * tickEventRuntime() and dispatchPendingEvents() are no-ops -- all per-tick work
 * lives in updateCoreServices() (see the .cpp for why, given the IDF loop calls
 * all three).
 */

#pragma once

#include <cstddef>
#include <memory>

#include "app/app_config.h"
#include "app/app_facades.h"
#include "chat/domain/chat_types.h"
#include "platform/esp/idf_common/idf_chat_factory.h"
#include "platform/esp/idf_common/team/idf_lora_pairing_service.h"
#include "platform/esp/idf_common/team/idf_team_crypto.h"
#include "platform/esp/idf_common/team/idf_team_event_sinks.h"
#include "platform/esp/idf_common/team/idf_team_track_source.h"
#include "platform/esp/idf_common/team/team_runtime_idf.h"
#include "team/usecase/team_controller.h"
#include "team/usecase/team_service.h"
#include "team/usecase/team_track_sampler.h"

class BoardBase;
class LoraBoard;

namespace sys
{
struct Event;
}

namespace platform::esp::idf_common
{

class IdfChatFacade final : public ::app::IAppFacade
{
  public:
    /**
     * @param lora_board The board's LoRa radio (must outlive the facade).
     * @param board Optional BoardBase for getBoard() (haptics / message tone);
     *              may be nullptr.
     */
    explicit IdfChatFacade(LoraBoard& lora_board, BoardBase* board = nullptr);
    ~IdfChatFacade() override;

    IdfChatFacade(const IdfChatFacade&) = delete;
    IdfChatFacade& operator=(const IdfChatFacade&) = delete;

    /**
     * @brief Build the chat stack and bind this facade as the global app facade.
     *
     * Mirrors MinimalLinuxAppFacade::initialize(): constructs services, then
     * app::bindAppFacade(*this) if nothing is bound yet. Must be called BEFORE
     * idf_app_runtime_access::initialize so the UI sees a live facade.
     *
     * @return true on success; false if the chat runtime failed to build.
     */
    bool initialize();
    void shutdown();
    [[nodiscard]] bool isInitialized() const noexcept;

    // -- IAppConfigFacade ---------------------------------------------------
    ::app::AppConfig& getConfig() override;
    const ::app::AppConfig& getConfig() const override;
    void saveConfig() override;
    void applyMeshConfig() override;
    void applyUserInfo() override;
    void applyPositionConfig() override;
    void applyNetworkLimits() override;
    void applyPrivacyConfig() override;
    void applyChatDefaults() override;
    ::chat::MeshProtocol getMeshProtocol() const override;
    void getEffectiveUserInfo(char* out_long,
                              std::size_t long_len,
                              char* out_short,
                              std::size_t short_len) const override;
    bool switchMeshProtocol(::chat::MeshProtocol protocol, bool persist = true) override;

    // -- IAppMessagingFacade ------------------------------------------------
    ::chat::ChatService& getChatService() override;
    ::chat::contacts::ContactService& getContactService() override;
    ::chat::IMeshAdapter* getMeshAdapter() override;
    const ::chat::IMeshAdapter* getMeshAdapter() const override;
    ::chat::NodeId getSelfNodeId() const override;

    // -- IAppTeamFacade (stubbed: chat screen guards null team) -------------
    ::team::TeamController* getTeamController() override;
    ::team::TeamPairingService* getTeamPairing() override;
    ::team::TeamService* getTeamService() override;
    const ::team::TeamService* getTeamService() const override;
    ::team::TeamTrackSampler* getTeamTrackSampler() override;
    void setTeamModeActive(bool active) override;
    bool setTeamPairingPassphrase(const char* passphrase) override;

    // -- IAppAdminFacade ----------------------------------------------------
    void broadcastNodeInfo() override;
    void clearNodeDb() override;
    void clearMessageDb() override;

    // -- IAppRuntimeFacade --------------------------------------------------
    ::ble::BleManager* getBleManager() override;
    const ::ble::BleManager* getBleManager() const override;
    bool isBleEnabled() const override;
    void setBleEnabled(bool enabled) override;
    void restartDevice() override;
    ::chat::ui::IChatUiRuntime* getChatUiRuntime() override;
    void setChatUiRuntime(::chat::ui::IChatUiRuntime* runtime) override;
    ::BoardBase* getBoard() override;
    const ::BoardBase* getBoard() const override;

    // -- IAppLifecycleFacade ------------------------------------------------
    void updateCoreServices() override;
    void tickEventRuntime() override;
    void dispatchPendingEvents(std::size_t max_events = 32) override;

  private:
    void pumpMeshAndDrainEvents(std::size_t max_events);

    // Deliver a single bus event to the team page event handler
    // (team::ui::shell::handle_event). Mirrors the Arduino handleUiEvent() team
    // branch: the team page reducer is what applies TeamKeyDist/TeamPairing/etc.
    // to the team UI snapshot (in_team, members, setKeysFromPsk) and advances the
    // screen off "Scanning"/"Waiting for member". Returns true if the event was a
    // team/SystemTick event consumed here (caller must NOT also feed it to the
    // chat UI runtime). Deletes the event when it consumes it.
    bool routeTeamEvent(::sys::Event* event);

    // Periodic team presence heartbeat. While the team has keys, broadcast a
    // minimal (empty-roster) TeamStatus over the encrypted + plain team channel so
    // the PEER marks this unit online (the receiver's reducer touch-updates the
    // sender's last_seen on any team frame; an empty roster is non-destructive --
    // applyStatusRoster() ignores a status with no members). This is the GPS-free
    // member->leader liveness the TeamTrackSampler cannot provide without a fix,
    // and it flows symmetrically both ways. No-op unless team_service_->hasKeys().
    void tickTeamPresence();

    // Publish a SystemTick onto the bus each pump so the team page runs its
    // periodic work (process_status_broadcasts / process_keydist_retries). The IDF
    // loop never published SystemTick (only the Linux facade did), so team presence
    // + keydist retries never fired on-device.
    void publishTeamSystemTick();

    // Construct the team services (crypto/runtime/track-source/event-sink ->
    // TeamService -> TeamController + TeamTrackSampler) so getTeamController()/
    // getTeamService() return real objects. Mirrors create_team_services() in
    // platform/esp/arduino_common/.../app_context_platform_bindings.cpp. Called
    // from initialize() once runtime_.mesh_adapter is valid. Phase 2 also
    // constructs the LoRa pairing service here so getTeamPairing() is non-null.
    void initTeamServices();

    // Routes TeamService's "unhandled app data" (any portnum it does not consume)
    // to the LoRa pairing service when the portnum is TEAM_PAIR_APP. TeamService's
    // processIncoming() drains the single adapter data queue and dispatches its own
    // portnums (TEAM_MGMT/POSITION/WAYPOINT/CHAT/TRACK); pairing frames fall through
    // to here. This is how TEAM_PAIR_APP frames reach the pairing transport without
    // the facade racing TeamService for the shared pollIncomingData queue.
    class PairingAppDataRouter final : public ::team::TeamService::UnhandledAppDataObserver
    {
      public:
        explicit PairingAppDataRouter(IdfChatFacade& facade) : facade_(facade) {}
        void onUnhandledAppData(const ::chat::MeshIncomingData& msg) override;

      private:
        IdfChatFacade& facade_;
    };

    LoraBoard& lora_board_;
    BoardBase* board_ = nullptr;
    ::app::AppConfig config_{};
    IdfChatRuntime runtime_{};
    ::chat::ui::IChatUiRuntime* chat_ui_runtime_ = nullptr;
    ::chat::NodeId self_node_id_ = 0;
    bool initialized_ = false;
    bool bound_ = false;

    // -- Team services (Phase 1). Members are owned here and outlive the UI; the
    //    services hold references to the runtime/crypto/sink/track-source, so
    //    declaration order matters: ports first, then service, then controller/
    //    sampler that reference the service. team_pairing_ is intentionally not
    //    constructed yet (Phase 2 LoRa pairing).
    team_infra::TeamRuntimeIdf team_runtime_{};
    team_infra::IdfTeamCrypto team_crypto_{};
    team_infra::IdfTeamTrackSource team_track_source_{};
    team_infra::IdfTeamEventBusSink team_event_sink_{};
    team_infra::IdfTeamPairingEventQueue team_pairing_event_sink_{};
    std::unique_ptr<::team::TeamService> team_service_;
    std::unique_ptr<::team::TeamController> team_controller_;
    std::unique_ptr<::team::TeamTrackSampler> team_track_sampler_;
    // Phase 2: LoRa pairing service (drives TeamPairingCoordinator over the mesh
    // adapter on TEAM_PAIR_APP). Non-null after initTeamServices(), so
    // getTeamPairing() returns it and the Team screen's pairing UX is live. The
    // router forwards TEAM_PAIR_APP frames from TeamService's unhandled-app-data
    // hook into team_pairing_'s transport.
    std::unique_ptr<team_infra::IdfLoraTeamPairingService> team_pairing_;
    std::unique_ptr<PairingAppDataRouter> pairing_app_data_router_;
    bool team_mode_active_ = false;

    // Wall-clock (ms) of the last team presence heartbeat TX. 0 = never sent;
    // tickTeamPresence() seeds it on the first in-team tick and re-broadcasts every
    // kTeamPresenceIntervalMs while keys are held. Kept well under the 120 s online
    // window so a peer never lapses to "stale" between heartbeats.
    uint32_t team_presence_last_tx_ms_ = 0;
    // Tracks key state across ticks so a heartbeat fires promptly on the first tick
    // after keys are established (rather than waiting a full interval), and so the
    // timer resets when a team is left.
    bool team_presence_had_keys_ = false;
};

} // namespace platform::esp::idf_common

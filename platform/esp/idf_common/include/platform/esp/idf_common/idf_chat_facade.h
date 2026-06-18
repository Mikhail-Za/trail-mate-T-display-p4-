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

#include "app/app_config.h"
#include "app/app_facades.h"
#include "chat/domain/chat_types.h"
#include "platform/esp/idf_common/idf_chat_factory.h"

class BoardBase;
class LoraBoard;

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

    LoraBoard& lora_board_;
    BoardBase* board_ = nullptr;
    ::app::AppConfig config_{};
    IdfChatRuntime runtime_{};
    ::chat::ui::IChatUiRuntime* chat_ui_runtime_ = nullptr;
    ::chat::NodeId self_node_id_ = 0;
    bool initialized_ = false;
    bool bound_ = false;
};

} // namespace platform::esp::idf_common

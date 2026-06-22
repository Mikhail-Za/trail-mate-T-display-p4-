/**
 * @file chat_ui_controller.h
 * @brief Public shared Chat UI controller header
 */

#pragma once

#include "chat/domain/chat_types.h"
#include "chat/usecase/chat_service.h"
#include "lvgl.h"
#include "sys/event_bus.h"
#include "ui/chat_ui_runtime.h"
#include "ui/screens/chat/chat_compose_components.h"
#include "ui/screens/chat/chat_conversation_components.h"
#include "ui/screens/chat/chat_message_list_components.h"
#include "ui/widgets/ime/ime_widget.h"
#include "ui_chat_runtime/chat_ui_refresh_sink.h"
#include "ui_lvgl_ux_packs/common/key_verification_modal_renderer.h"
#include "ui_lvgl_ux_packs/common/team_position_picker_renderer.h"
#include "ui_presentation/chat/chat_workspace_model.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ui::key_verification
{
class KeyVerificationModel;
struct KeyVerificationSnapshot;
} // namespace ui::key_verification

namespace chat
{
namespace ui
{

class ChatTeamWorkflow;

// Phase 5.6 note:
//
// UiController is still a legacy application controller. It has started
// consuming ChatWorkspaceModel for the non-team direct/channel chat path and a
// dedicated Team ChatWorkspaceModel for team text projection/send, but it still
// owns legacy responsibilities such as event handling, Team location/command
// flows, key verification, conversation cache, and ChatService lifecycle calls.
//
// Do not add new direct ChatService rendering logic here unless it is
// documented as legacy ownership. New non-team chat presentation should flow
// through ChatWorkspaceModel.
class UiController : public IChatUiRefreshSink
{
  public:
    using State = ChatUiState;
    using ExitRequestCallback = void (*)(void*);

    UiController(lv_obj_t* parent,
                 chat::ChatService& service,
                 ::ui::chat::ChatWorkspaceModel& chat_model,
                 ChatTeamWorkflow& team_workflow,
                 ::ui::key_verification::KeyVerificationModel*
                     key_verification_model = nullptr,
                 chat::ChannelId initial_channel = chat::ChannelId::PRIMARY,
                 ExitRequestCallback exit_request = nullptr,
                 void* exit_request_user_data = nullptr);
    ~UiController();

    void init();
    void update();
    void onInput(const sys::InputEvent& event);
    void backToList();
    void handleConversationAction(ChatConversationScreen::ActionIntent intent);
    void handleComposeAction(ChatComposeScreen::ActionIntent intent);
    void openNewMessagePicker();
    void exitToMenu();

    State getState() const { return state_; }
    bool isTeamConversationActive() const { return team_conv_active_; }
    lv_obj_t* getParent() const { return parent_; }
    void onChannelClicked(chat::ConversationId conv);
    void onRuntimeMessageArrived(chat::MessageId msg_id) override;
    void onRuntimeSendResult(chat::MessageId msg_id) override;
    void onRuntimeUnreadChanged() override;
    void showKeyVerification(
        const ::ui::key_verification::KeyVerificationSnapshot& snapshot) override;

  private:
    lv_obj_t* parent_;
    // Runtime ownership:
    // ChatService is retained for event processing, Team workflow, key
    // verification, and controller refresh flows. Presentation state is owned
    // by ChatWorkspaceModel and runtime projection ports.
    chat::ChatService& service_;
    ::ui::chat::ChatWorkspaceModel& chat_model_;
    ChatTeamWorkflow& team_workflow_;
    ::ui::key_verification::KeyVerificationModel* key_verification_model_ =
        nullptr;
    State state_;

    std::unique_ptr<ChatMessageListScreen> channel_list_;
    std::unique_ptr<ChatConversationScreen> conversation_;
    std::unique_ptr<ChatComposeScreen> compose_;
    std::unique_ptr<::ui::widgets::ImeWidget> compose_ime_;
    std::unique_ptr<TeamPositionPickerRenderer> team_position_picker_;

    chat::ChannelId current_channel_;
    chat::ConversationId current_conv_;
    bool team_conv_active_ = false;
    lv_timer_t* team_conv_timer_ = nullptr;
    bool exiting_ = false;
    ExitRequestCallback exit_request_ = nullptr;
    void* exit_request_user_data_ = nullptr;

    void switchToChannelList();
    void switchToConversation(chat::ConversationId conv);
    void switchToCompose(chat::ConversationId conv);
    void handleChannelSelected(const chat::ConversationId& conv);
    void handleSendMessage(const std::string& text);
    void handleComposeSendDone(bool ok, bool timeout);
    static void handleComposeSendDoneCallback(bool ok, bool timeout, void* user_data);
    void refreshUnreadCounts();
    void refreshUnreadCounts(bool force_reload);
    void cleanupComposeIme();
    bool isTeamConversation(const chat::ConversationId& conv) const;
    bool loadChatSnapshot();
    bool loadTeamChatSnapshot();
    void syncConversationListFromStore();
    void normalizeConversationNames(std::vector<chat::ConversationMeta>& convs) const;
    std::string resolveConversationDisplayName(const chat::ConversationId& conv) const;
    void applyConversationListToUi();
    void updateConversationMetaForMessage(const chat::ChatMessage& msg, bool increment_unread);
    bool updateConversationViewForIncoming(const chat::ChatMessage& msg);
    void reloadConversationView();
    // Team text, read projection, and location action sends flow through
    // ChatTeamWorkflow.
    void refreshTeamConversation();
    void startTeamConversationTimer();
    void stopTeamConversationTimer();
    void openTeamPositionPicker();
    void closeTeamPositionPicker(bool restore_group);
    void updateTeamPositionPickerHint(uint8_t icon_id);
    void onTeamPositionIconSelected(uint8_t icon_id);
    void onTeamPositionCancel();
    bool isTeamPositionPickerOpen() const;
    bool isKeyVerificationModalOpen() const;
    void destroyKeyVerificationModal(bool restore_group);
    void renderKeyVerificationModal(
        const ::ui::key_verification::KeyVerificationSnapshot& snapshot);
    void closeKeyVerificationModal(bool restore_group);
    void submitKeyVerificationInput();
    void trustKeyFromVerificationModal();
    void clearKeyVerificationError();

    // New-message picker modal (start a fresh broadcast or DM from the Chat
    // list). Lightweight self-contained overlay; mirrors the contacts action
    // menu pattern. Closes before handing off to switchToCompose().
    bool isNewMessagePickerOpen() const;
    void closeNewMessagePicker(bool restore_group);
    void onNewMessagePicked(const chat::ConversationId& conv);
    static void new_message_pick_event_cb(lv_event_t* e);
    static void new_message_cancel_event_cb(lv_event_t* e);
    static void new_message_key_event_cb(lv_event_t* e);

    lv_obj_t* new_msg_modal_ = nullptr;
    lv_group_t* new_msg_group_ = nullptr;
    lv_group_t* new_msg_prev_group_ = nullptr;
    std::vector<chat::ConversationId> new_msg_targets_;

    KeyVerificationModalRefs key_verify_modal_;
    std::unique_ptr<::ui::widgets::ImeWidget> key_verify_ime_;
    std::vector<chat::ConversationMeta> cached_conversations_;
    // Chat snapshots are large enough to overflow ESP loopTask when copied on
    // the stack during page entry. Reuse controller-owned buffers instead.
    ::ui::chat::ChatWorkspaceSnapshot chat_snapshot_buffer_{};
    ::ui::chat::ChatWorkspaceSnapshot team_chat_snapshot_buffer_{};
    bool conversation_list_dirty_ = true;

    static void key_verify_submit_event_cb(lv_event_t* e);
    static void key_verify_close_event_cb(lv_event_t* e);
    static void key_verify_trust_event_cb(lv_event_t* e);
};

} // namespace ui
} // namespace chat

#if !defined(ARDUINO_T_WATCH_S3)
/**
 * @file chat_conversation.cpp
 * @brief Chat conversation screen implementation
 */
#include "ui/screens/chat/chat_conversation_components.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/usecase/contact_service.h"
#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/screens/chat/chat_conversation_input.h"
#include "ui/screens/chat/chat_conversation_layout.h"
#include "ui/screens/chat/chat_conversation_styles.h"
#include "ui/ui_common.h"
#include "ui/widgets/system_notification.h"
#include "ui_presentation/chat/chat_workspace_snapshot.h"

#include "sys/clock.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#ifndef CHAT_CONVERSATION_LOG_ENABLE
#define CHAT_CONVERSATION_LOG_ENABLE 0
#endif

#if CHAT_CONVERSATION_LOG_ENABLE
#define CHAT_CONVERSATION_LOG(...) std::printf(__VA_ARGS__)
#else
#define CHAT_CONVERSATION_LOG(...)
#endif

namespace chat
{
namespace ui
{

// Keep original constant meaning (bubble max width and 70% rule)
namespace
{
constexpr lv_coord_t kBubbleMaxWidth = 322; // same as original
constexpr lv_coord_t kBubblePadX = 10;      // keep in sync with styles.cpp
constexpr uint32_t kSecondsPerDay = 24U * 60U * 60U;
constexpr uint32_t kSecondsPerMonth = 30U * kSecondsPerDay;
constexpr uint32_t kSecondsPerYear = 365U * kSecondsPerDay;
constexpr uint32_t kMinValidEpochSeconds = 1577836800U; // 2020-01-01
constexpr size_t kMaxPrefixedSenderLen = 20;

lv_coord_t bubble_pad_x()
{
    return ::ui::page_profile::is_dense() ? 6 : kBubblePadX;
}

::ui::chat::MessageDeliveryState delivery_from_message_status(
    chat::MessageStatus status)
{
    switch (status)
    {
    case chat::MessageStatus::Incoming:
        return ::ui::chat::MessageDeliveryState::Received;
    case chat::MessageStatus::Queued:
        return ::ui::chat::MessageDeliveryState::Queued;
    case chat::MessageStatus::Sent:
        return ::ui::chat::MessageDeliveryState::Sent;
    case chat::MessageStatus::Failed:
        return ::ui::chat::MessageDeliveryState::Failed;
    }
    return ::ui::chat::MessageDeliveryState::Unknown;
}

bool message_ref_matches_id(const ::ui::chat::MessageRef& ref,
                            chat::MessageId msg_id)
{
    if (ref.protocol_id != 0)
    {
        return ref.protocol_id == msg_id;
    }
    return ref.local_id == static_cast<uint64_t>(msg_id);
}

uint32_t timestamp_from_presentation_label(const ::ui::FixedText<24>& label)
{
    const char* text = label.c_str();
    if (!text || text[0] == '\0')
    {
        return 0;
    }

    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || (end && *end != '\0'))
    {
        return 0;
    }
    return static_cast<uint32_t>(value);
}

bool is_structured_team_payload(
    const ::ui::chat::TeamMessageRichPayload& payload)
{
    return payload.kind == ::ui::chat::TeamMessageRichPayloadKind::Location ||
           payload.kind == ::ui::chat::TeamMessageRichPayloadKind::Command;
}

std::string rich_payload_kind_label(
    ::ui::chat::TeamMessageRichPayloadKind kind)
{
    switch (kind)
    {
    case ::ui::chat::TeamMessageRichPayloadKind::Location:
        return "Location";
    case ::ui::chat::TeamMessageRichPayloadKind::Command:
        return "Command";
    case ::ui::chat::TeamMessageRichPayloadKind::Unsupported:
        return "Unsupported";
    case ::ui::chat::TeamMessageRichPayloadKind::Text:
        return "Text";
    case ::ui::chat::TeamMessageRichPayloadKind::None:
        break;
    }
    return "Team";
}

std::string format_team_rich_payload_text(
    const ::ui::chat::TeamMessageRichPayload& payload)
{
    if (!payload.summary.empty())
    {
        return payload.summary.c_str();
    }
    if (!payload.title.empty())
    {
        return payload.title.c_str();
    }
    return rich_payload_kind_label(payload.kind);
}
} // namespace

// Sentinel address stored as user_data on the "no messages yet" empty-state label so
// it can be located and removed once a real message arrives. Kept file-static because
// the screen header (which we do not own) has no member field for it.
static char g_conv_empty_placeholder_tag = 0;

static void remove_conversation_empty_placeholder(lv_obj_t* msg_list)
{
    if (!msg_list || !lv_obj_is_valid(msg_list))
    {
        return;
    }
    const uint32_t count = lv_obj_get_child_count(msg_list);
    for (uint32_t i = 0; i < count; ++i)
    {
        lv_obj_t* child = lv_obj_get_child(msg_list, i);
        if (child && lv_obj_get_user_data(child) == &g_conv_empty_placeholder_tag)
        {
            lv_obj_del(child);
            return;
        }
    }
}

static void show_conversation_empty_placeholder(lv_obj_t* msg_list)
{
    if (!msg_list || !lv_obj_is_valid(msg_list))
    {
        return;
    }
    remove_conversation_empty_placeholder(msg_list); // guard against duplicates
    lv_obj_t* label = lv_label_create(msg_list);
    lv_obj_set_user_data(label, &g_conv_empty_placeholder_tag);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0x9A8A6A), LV_PART_MAIN);
    lv_obj_set_style_margin_top(label, 16, LV_PART_MAIN);
    ::ui::i18n::set_label_text(label, "No messages yet");
    ::ui::fonts::apply_localized_font(
        label, lv_label_get_text(label), ::ui::fonts::ui_chrome_font());
}

static bool is_valid_epoch_ts(uint32_t ts)
{
    return ts >= kMinValidEpochSeconds;
}

static void format_message_time(char* out, size_t out_len, uint32_t ts)
{
    if (!out || out_len == 0) return;
    if (ts == 0)
    {
        snprintf(out, out_len, "--");
        return;
    }

    uint32_t now_epoch = sys::epoch_seconds_now();
    bool ts_is_epoch = is_valid_epoch_ts(ts);
    bool now_is_epoch = is_valid_epoch_ts(now_epoch);
    uint32_t now_secs = now_is_epoch ? now_epoch : static_cast<uint32_t>(sys::millis_now() / 1000U);
    if (ts_is_epoch && !now_is_epoch)
    {
        ts_is_epoch = false;
    }
    if (now_secs < ts)
    {
        now_secs = ts;
    }
    uint32_t diff = now_secs - ts;

    if (!ts_is_epoch)
    {
        if (diff < 60U)
        {
            snprintf(out, out_len, "%s", ::ui::i18n::tr("now"));
            return;
        }
        if (diff < 3600U)
        {
            snprintf(out, out_len, "%s", ::ui::i18n::format("%um", static_cast<unsigned>(diff / 60U)).c_str());
            return;
        }
        if (diff < kSecondsPerDay)
        {
            snprintf(out, out_len, "%s", ::ui::i18n::format("%uh", static_cast<unsigned>(diff / 3600U)).c_str());
            return;
        }
        if (diff < kSecondsPerMonth)
        {
            snprintf(out, out_len, "%s", ::ui::i18n::format("%ud", static_cast<unsigned>(diff / kSecondsPerDay)).c_str());
            return;
        }
        if (diff < kSecondsPerYear)
        {
            snprintf(out, out_len, "%s", ::ui::i18n::format("%umo", static_cast<unsigned>(diff / kSecondsPerMonth)).c_str());
            return;
        }
        snprintf(out, out_len, "%s", ::ui::i18n::format("%uy", static_cast<unsigned>(diff / kSecondsPerYear)).c_str());
        return;
    }

    time_t t = ui_apply_timezone_offset(static_cast<time_t>(ts));
    struct tm* info = gmtime(&t);
    if (info)
    {
        strftime(out, out_len, "%H:%M", info);
    }
    else
    {
        snprintf(out, out_len, "--");
    }
}

static bool sender_token_is_valid(const std::string& sender)
{
    if (sender.empty() || sender.size() > kMaxPrefixedSenderLen)
    {
        return false;
    }
    for (char c : sender)
    {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!(std::isalnum(uc) || c == '_' || c == '-' || c == '.'))
        {
            return false;
        }
    }
    return true;
}

static bool split_prefixed_sender_text(const std::string& text,
                                       std::string* out_sender,
                                       std::string* out_body)
{
    if (!out_sender || !out_body || text.empty())
    {
        return false;
    }

    const size_t sep = text.find(':');
    if (sep == std::string::npos || sep == 0 || sep > kMaxPrefixedSenderLen)
    {
        return false;
    }

    std::string sender = text.substr(0, sep);
    while (!sender.empty() && sender.back() == ' ')
    {
        sender.pop_back();
    }
    if (!sender_token_is_valid(sender))
    {
        return false;
    }

    size_t body_start = sep + 1;
    while (body_start < text.size() && text[body_start] == ' ')
    {
        ++body_start;
    }
    if (body_start >= text.size())
    {
        return false;
    }

    *out_sender = sender;
    *out_body = text.substr(body_start);
    return true;
}

ChatConversationScreen::ChatConversationScreen(lv_obj_t* parent, chat::ConversationId conv)
    : conv_(conv)
{
    guard_ = new LifetimeGuard();
    guard_->alive = true;
    guard_->pending_async = 0;

    lv_obj_t* active = lv_screen_active();
    if (!active)
    {
        CHAT_CONVERSATION_LOG("[ChatConversation] WARNING: lv_screen_active() is null\n");
    }
    else
    {
        CHAT_CONVERSATION_LOG("[ChatConversation] init: active=%p parent=%p\n", active, parent);
    }

    // ----- Layout -----
    auto w = chat::ui::layout::create_conversation_base(parent);
    container_ = w.root;
    msg_list_ = w.msg_list;
    action_bar_ = w.action_bar;
    reply_btn_ = w.reply_btn;

    // ----- Styles -----
    chat::ui::conversation::styles::apply_root(container_);
    chat::ui::conversation::styles::apply_msg_list(msg_list_);
    chat::ui::conversation::styles::apply_action_bar(action_bar_);
    chat::ui::conversation::styles::apply_reply_btn(reply_btn_);

    // Reply label text + style
    ::ui::i18n::set_label_text(w.reply_label, "Reply");
    chat::ui::conversation::styles::apply_reply_label(w.reply_label);
    ::ui::fonts::apply_localized_font(w.reply_label, lv_label_get_text(w.reply_label), ::ui::fonts::ui_chrome_font());

    // ----- Top bar (existing widget, unchanged behavior) -----
    ::ui::widgets::top_bar_init(top_bar_, container_);
    const char* title = (conv_.peer == 0) ? ::ui::i18n::tr("Broadcast") : ::ui::i18n::tr("Direct");
    ::ui::widgets::top_bar_set_title(top_bar_, title);
    ::ui::widgets::top_bar_set_right_text(top_bar_, "");
    ::ui::widgets::top_bar_set_back_callback(top_bar_, handle_back, this);
    if (top_bar_.container)
    {
        lv_obj_move_to_index(top_bar_.container, 0);
    }

    if (container_)
    {
        lv_obj_add_event_cb(container_, on_root_deleted, LV_EVENT_DELETE, this);
    }

    if (container_ && !lv_obj_is_valid(container_))
    {
        CHAT_CONVERSATION_LOG("[ChatConversation] WARNING: container invalid\n");
    }
    if (msg_list_ && !lv_obj_is_valid(msg_list_))
    {
        CHAT_CONVERSATION_LOG("[ChatConversation] WARNING: msg_list invalid\n");
    }

    // ----- Event (unchanged) -----
    reply_ctx_.screen = this;
    reply_ctx_.intent = ActionIntent::Reply;
    lv_obj_add_event_cb(reply_btn_, action_event_cb, LV_EVENT_CLICKED, &reply_ctx_);

    // ----- Input layer (explicit, v0 no-op) -----
    chat::ui::conversation::input::init(this, &input_binding_);

    // Offer "Save contact" for direct conversations with a real peer.
    refreshSaveContactButton();
}

ChatConversationScreen::~ChatConversationScreen()
{
    // Tear the save prompt down explicitly (group restore + widget delete) before the
    // rest of the screen goes away. It is parented to container_, so deleting
    // container_ below would also free it, but we close first so the focus group and
    // pointers unwind cleanly.
    closeSaveContactPrompt();
    if (container_ && lv_obj_is_valid(container_))
    {
        lv_obj_del(container_);
    }
    if (guard_)
    {
        guard_->alive = false;
        if (guard_->pending_async == 0)
        {
            delete guard_;
        }
        guard_ = nullptr;
    }
}

void ChatConversationScreen::addMessage(const ::ui::chat::MessageRow& row)
{
    if (!guard_ || !guard_->alive || !msg_list_ || !lv_obj_is_valid(msg_list_))
    {
        return;
    }
    // A real message is arriving: drop the "no messages yet" hint if it is showing.
    remove_conversation_empty_placeholder(msg_list_);
    if (messages_.size() >= MAX_DISPLAY_MESSAGES)
    {
        MessageItem& oldest = messages_[0];
        if (oldest.container)
        {
            lv_obj_del(oldest.container);
        }
        messages_.erase(messages_.begin());
    }

    createMessageItem(row);
    scrollToBottom();
}

void ChatConversationScreen::clearMessages()
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    for (auto& item : messages_)
    {
        if (item.container)
        {
            lv_obj_del(item.container);
        }
    }
    messages_.clear();
    // With no messages left, the scroll area would be blank; show a centered hint
    // (mirrors the channel list's empty-state) until a real message is added.
    show_conversation_empty_placeholder(msg_list_);
}

void ChatConversationScreen::scrollToBottom()
{
    if (guard_ && guard_->alive && msg_list_)
    {
        lv_obj_scroll_to_y(msg_list_, LV_COORD_MAX, LV_ANIM_OFF);
    }
}

bool ChatConversationScreen::updateMessageStatus(const chat::MessageId msg_id,
                                                 const chat::MessageStatus status)
{
    if (!guard_ || !guard_->alive || msg_id == 0)
    {
        return false;
    }

    for (auto& item : messages_)
    {
        if (!message_ref_matches_id(item.ref, msg_id))
        {
            continue;
        }
        item.delivery = delivery_from_message_status(status);
        if (!item.status_label)
        {
            return true;
        }

        if (status == MessageStatus::Failed)
        {
            ::ui::i18n::set_label_text(item.status_label, "Failed");
            ::ui::fonts::apply_localized_font(
                item.status_label, lv_label_get_text(item.status_label), ::ui::fonts::ui_chrome_font());
            lv_obj_clear_flag(item.status_label, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_label_set_text(item.status_label, "");
            ::ui::fonts::apply_localized_font(
                item.status_label, lv_label_get_text(item.status_label), ::ui::fonts::ui_chrome_font());
            lv_obj_add_flag(item.status_label, LV_OBJ_FLAG_HIDDEN);
        }
        return true;
    }

    return false;
}

void ChatConversationScreen::setActionCallback(void (*cb)(ActionIntent intent, void*), void* user_data)
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    action_cb_ = cb;
    action_cb_user_data_ = user_data;
}

void ChatConversationScreen::setHeaderText(const char* title, const char* status)
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    ::ui::widgets::top_bar_set_title(top_bar_, title);
    if (status != nullptr)
    {
        ::ui::widgets::top_bar_set_right_text(top_bar_, status);
    }
    refreshSaveContactButton();
}

void ChatConversationScreen::updateBatteryFromBoard()
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    ui_update_top_bar_battery(top_bar_);
}

void ChatConversationScreen::setBackCallback(void (*cb)(void*), void* user_data)
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    back_cb_ = cb;
    back_cb_user_data_ = user_data;
}

void ChatConversationScreen::setReplyEnabled(bool enabled)
{
    if (!guard_ || !guard_->alive || !reply_btn_)
    {
        return;
    }
    reply_enabled_ = enabled;
    if (enabled)
    {
        lv_obj_clear_state(reply_btn_, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(reply_btn_, LV_STATE_DISABLED);
    }
}

void ChatConversationScreen::ensureSaveButton()
{
    if (!guard_ || !guard_->alive || !action_bar_)
    {
        return;
    }
    if (save_btn_)
    {
        return;
    }

    const auto& profile = ::ui::page_profile::current();
    save_btn_ = lv_btn_create(action_bar_);
    lv_obj_set_size(save_btn_,
                    profile.dense ? 92 : 120,
                    ::ui::page_profile::resolve_control_button_height());
    lv_obj_clear_flag(save_btn_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(save_btn_, LV_SCROLLBAR_MODE_OFF);
    chat::ui::conversation::styles::apply_reply_btn(save_btn_);

    save_label_ = lv_label_create(save_btn_);
    lv_obj_center(save_label_);
    ::ui::i18n::set_label_text(save_label_, "Save");
    chat::ui::conversation::styles::apply_reply_label(save_label_);
    ::ui::fonts::apply_localized_font(
        save_label_, lv_label_get_text(save_label_), ::ui::fonts::ui_chrome_font());

    save_ctx_.screen = this;
    save_ctx_.intent = ActionIntent::SaveContact;
    lv_obj_add_event_cb(save_btn_, action_event_cb, LV_EVENT_CLICKED, &save_ctx_);
}

bool ChatConversationScreen::peerIsContact() const
{
    if (conv_.peer == 0 || !app::hasAppFacade())
    {
        return false;
    }
    // A peer is "already a contact" exactly when it has a user-assigned nickname.
    // getContacts() returns only nodes with nicknames, so a node_id match here means
    // a real contact (getContactName falls back to short_name and cannot be used).
    auto contacts = app::messagingFacade().getContactService().getContacts();
    for (const auto& c : contacts)
    {
        if (c.node_id == conv_.peer)
        {
            return true;
        }
    }
    return false;
}

void ChatConversationScreen::refreshSaveContactButton()
{
    if (!guard_ || !guard_->alive || !action_bar_)
    {
        return;
    }

    // Broadcast / team conversations (peer 0) never get a Save action.
    if (conv_.peer == 0)
    {
        if (save_btn_)
        {
            lv_obj_add_flag(save_btn_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    ensureSaveButton();
    if (!save_btn_)
    {
        return;
    }
    lv_obj_clear_flag(save_btn_, LV_OBJ_FLAG_HIDDEN);
    // Relabel to "Edit" when the peer already has a nickname so the action reads as
    // editing rather than creating; the prompt prefills the existing name.
    const char* label = peerIsContact() ? "Edit" : "Save";
    if (save_label_)
    {
        ::ui::i18n::set_label_text(save_label_, label);
        ::ui::fonts::apply_localized_font(
            save_label_, lv_label_get_text(save_label_), ::ui::fonts::ui_chrome_font());
    }
}

void ChatConversationScreen::schedule_save_async()
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    auto* payload = new BackPayload();
    payload->guard = guard_;
    payload->back_cb = nullptr;
    payload->user_data = this;
    guard_->pending_async++;
    lv_async_call(async_save_cb, payload);
}

void ChatConversationScreen::async_save_cb(void* user_data)
{
    auto* payload = static_cast<BackPayload*>(user_data);
    if (!payload)
    {
        return;
    }
    LifetimeGuard* guard = payload->guard;
    if (guard && guard->alive)
    {
        auto* screen = static_cast<ChatConversationScreen*>(payload->user_data);
        if (screen)
        {
            screen->openSaveContactPrompt();
        }
    }
    if (guard && guard->pending_async > 0)
    {
        guard->pending_async--;
        if (!guard->alive && guard->pending_async == 0)
        {
            delete guard;
        }
    }
    delete payload;
}

void ChatConversationScreen::openSaveContactPrompt()
{
    if (!guard_ || !guard_->alive || save_modal_)
    {
        return;
    }
    if (conv_.peer == 0)
    {
        ::ui::SystemNotification::show("Cannot save broadcast", 1800);
        return;
    }

    const auto& profile = ::ui::page_profile::current();

    // Dim background over the whole conversation so the prompt reads as modal. Parent
    // it to the conversation root (active screen), NOT lv_layer_top(): the global
    // keyboard button and the IME both live on lv_layer_top(), which always sits
    // above the active screen, so a screen-level backdrop covers the conversation
    // content while leaving the top-layer button + keyboard tappable. (A backdrop on
    // lv_layer_top() lands ABOVE the button created at boot and swallows its touches,
    // so the keyboard can never be pulled up.) This mirrors the contacts/settings
    // text-edit modals, whose backdrop parents to the active screen for the same
    // reason. container_ is a flex column, so flag the backdrop FLOATING to keep it
    // out of the flex flow and align it explicitly to cover the whole screen.
    lv_obj_t* modal_parent = (container_ && lv_obj_is_valid(container_))
                                 ? container_
                                 : lv_screen_active();
    save_modal_ = lv_obj_create(modal_parent);
    lv_obj_add_flag(save_modal_, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(save_modal_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(save_modal_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(save_modal_, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(save_modal_, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_border_width(save_modal_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(save_modal_, 0, LV_PART_MAIN);
    lv_obj_clear_flag(save_modal_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(save_modal_, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* win = lv_obj_create(save_modal_);
    lv_obj_set_size(win, profile.large_touch_hitbox ? 320 : 260, profile.large_touch_hitbox ? 180 : 160);
    lv_obj_center(win);
    lv_obj_set_style_bg_color(win, lv_color_hex(0xFAF0D8), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(win, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(win, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(win, lv_color_hex(0xE7C98F), LV_PART_MAIN);
    lv_obj_set_style_radius(win, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(win, 8, LV_PART_MAIN);
    lv_obj_clear_flag(win, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(win);
    const bool editing = peerIsContact();
    ::ui::i18n::set_label_text(title, editing ? "Edit contact" : "Save contact");
    lv_obj_set_style_text_color(title, lv_color_hex(0x6B4A1E), 0);
    ::ui::fonts::apply_localized_font(title, lv_label_get_text(title), ::ui::fonts::ui_chrome_font());
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    save_modal_textarea_ = lv_textarea_create(win);
    lv_textarea_set_one_line(save_modal_textarea_, true);
    lv_textarea_set_max_length(save_modal_textarea_, 12);
    lv_textarea_set_placeholder_text(save_modal_textarea_, "nickname");
    lv_obj_set_width(save_modal_textarea_, LV_PCT(100));
    lv_obj_align(save_modal_textarea_, LV_ALIGN_TOP_MID, 0, profile.large_touch_hitbox ? 40 : 28);
    if (editing && app::hasAppFacade())
    {
        const std::string existing =
            app::messagingFacade().getContactService().getContactName(conv_.peer);
        if (!existing.empty())
        {
            lv_textarea_set_text(save_modal_textarea_, existing.c_str());
            lv_textarea_set_cursor_pos(save_modal_textarea_, LV_TEXTAREA_CURSOR_LAST);
        }
    }

    save_modal_error_ = lv_label_create(win);
    lv_label_set_text(save_modal_error_, "");
    lv_obj_set_style_text_color(save_modal_error_, lv_color_hex(0xB94A2C), 0);
    lv_obj_align(save_modal_error_, LV_ALIGN_TOP_MID, 0, profile.large_touch_hitbox ? 84 : 58);
    lv_obj_add_flag(save_modal_error_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* btn_row = lv_obj_create(win);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* save_btn = lv_btn_create(btn_row);
    lv_obj_set_size(save_btn, ::ui::page_profile::resolve_control_button_min_width(),
                    ::ui::page_profile::resolve_control_button_height());
    chat::ui::conversation::styles::apply_reply_btn(save_btn);
    lv_obj_t* save_lbl = lv_label_create(save_btn);
    ::ui::i18n::set_label_text(save_lbl, "Save");
    ::ui::fonts::apply_localized_font(save_lbl, lv_label_get_text(save_lbl), ::ui::fonts::ui_chrome_font());
    lv_obj_center(save_lbl);
    lv_obj_add_event_cb(save_btn, on_save_modal_save_clicked, LV_EVENT_CLICKED, this);

    lv_obj_t* cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn, ::ui::page_profile::resolve_control_button_min_width(),
                    ::ui::page_profile::resolve_control_button_height());
    chat::ui::conversation::styles::apply_reply_btn(cancel_btn);
    lv_obj_t* cancel_lbl = lv_label_create(cancel_btn);
    ::ui::i18n::set_label_text(cancel_lbl, "Cancel");
    ::ui::fonts::apply_localized_font(cancel_lbl, lv_label_get_text(cancel_lbl), ::ui::fonts::ui_chrome_font());
    lv_obj_center(cancel_lbl);
    lv_obj_add_event_cb(cancel_btn, on_save_modal_cancel_clicked, LV_EVENT_CLICKED, this);

    // Dedicated focus group so the textarea/buttons take keypad/encoder focus and
    // the global keyboard button finds the field; restore the previous group on close.
    // Use set_default_group (not raw lv_group_set_default): it also rebinds the
    // pointer/keypad/encoder indevs to this group, so a tap on the textarea keeps it
    // focused in the default group. The keyboard button resolves the target via
    // lv_group_get_focused(lv_group_get_default()); without the indev rebind the
    // indev still drives the conversation group and the tapped field is not focused
    // in the default group, so the keyboard cannot find it. Every other modal in the
    // app (contacts, settings) goes through this wrapper for the same reason.
    save_modal_prev_group_ = lv_group_get_default();
    save_modal_group_ = lv_group_create();
    lv_group_add_obj(save_modal_group_, save_modal_textarea_);
    lv_group_add_obj(save_modal_group_, save_btn);
    lv_group_add_obj(save_modal_group_, cancel_btn);
    set_default_group(save_modal_group_);
    lv_group_focus_obj(save_modal_textarea_);
}

void ChatConversationScreen::closeSaveContactPrompt(bool restore_group)
{
    if (save_modal_group_)
    {
        if (restore_group && save_modal_prev_group_ &&
            lv_group_get_default() == save_modal_group_)
        {
            // Mirror the open path: rebind the indevs back to the previous group, not
            // just the default-group global. When restore_group is false the owner
            // installs the next screen's group during teardown, so we skip this.
            set_default_group(save_modal_prev_group_);
        }
        lv_group_del(save_modal_group_);
        save_modal_group_ = nullptr;
    }
    save_modal_prev_group_ = nullptr;
    if (save_modal_)
    {
        lv_obj_del(save_modal_);
        save_modal_ = nullptr;
    }
    save_modal_textarea_ = nullptr;
    save_modal_error_ = nullptr;
}

void ChatConversationScreen::commitSaveContact()
{
    if (!save_modal_textarea_ || !save_modal_error_)
    {
        return;
    }
    if (conv_.peer == 0)
    {
        closeSaveContactPrompt();
        return;
    }
    const char* name = lv_textarea_get_text(save_modal_textarea_);
    if (!name || std::strlen(name) == 0)
    {
        ::ui::i18n::set_label_text(save_modal_error_, "Name required");
        lv_obj_clear_flag(save_modal_error_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (std::strlen(name) > 12)
    {
        ::ui::i18n::set_label_text(save_modal_error_, "Name too long");
        lv_obj_clear_flag(save_modal_error_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (!app::hasAppFacade())
    {
        ::ui::i18n::set_label_text(save_modal_error_, "Save failed");
        lv_obj_clear_flag(save_modal_error_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // addContact synthesizes a node record if the peer was never heard, and the
    // store rejects a duplicate nickname (returns false) which we surface here.
    const bool ok =
        app::messagingFacade().getContactService().addContact(conv_.peer, name);
    if (!ok)
    {
        ::ui::i18n::set_label_text(save_modal_error_, "Name in use or failed");
        lv_obj_clear_flag(save_modal_error_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    closeSaveContactPrompt();
    ::ui::SystemNotification::show("Contact saved", 1500);
    refreshSaveContactButton();
}

void ChatConversationScreen::on_save_modal_save_clicked(lv_event_t* e)
{
    auto* screen = static_cast<ChatConversationScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->guard_ || !screen->guard_->alive)
    {
        return;
    }
    screen->commitSaveContact();
}

void ChatConversationScreen::on_save_modal_cancel_clicked(lv_event_t* e)
{
    auto* screen = static_cast<ChatConversationScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->guard_ || !screen->guard_->alive)
    {
        return;
    }
    screen->closeSaveContactPrompt();
}

void ChatConversationScreen::createMessageItem(const ::ui::chat::MessageRow& row)
{
    if (!guard_ || !guard_->alive || !msg_list_)
    {
        return;
    }
    MessageItem item;
    item.ref = row.ref;
    item.delivery = row.delivery;

    // ----- Layout: row + bubble + time + text + status -----
    item.container = chat::ui::layout::create_message_row(msg_list_);
    chat::ui::conversation::styles::apply_message_row(item.container);

    const bool is_self = row.outgoing;
    std::string display_text =
        row.has_team_rich_payload
            ? format_team_rich_payload_text(row.team_rich_payload)
            : std::string(row.text.c_str());
    std::string inferred_sender;
    if (!is_self &&
        conv_.protocol == chat::MeshProtocol::MeshCore &&
        conv_.peer == 0 &&
        row.sender_node_id == 0)
    {
        std::string parsed_sender;
        std::string parsed_body;
        if (split_prefixed_sender_text(display_text, &parsed_sender, &parsed_body))
        {
            inferred_sender = parsed_sender;
            display_text = parsed_body;
        }
    }

    // Compute bubble max width (same logic as original)
    lv_coord_t max_bubble_w = kBubbleMaxWidth;
    lv_coord_t list_w = chat::ui::layout::get_msg_list_content_width(msg_list_);
    if (list_w > 0)
    {
        // original: candidate = (list_w - 2 * kPadX) * 7 / 10
        // kPadX lives in styles; but for behavior parity we replicate formula using known 8px.
        const lv_coord_t kPadX = ::ui::page_profile::is_dense() ? 4 : 8;
        lv_coord_t candidate = (list_w - 2 * kPadX) * 7 / 10;
        if (candidate > 0 && candidate < max_bubble_w)
        {
            max_bubble_w = candidate;
        }
    }

    lv_obj_t* bubble = chat::ui::layout::create_bubble(item.container);
    chat::ui::conversation::styles::apply_bubble(bubble, is_self);
    chat::ui::layout::set_bubble_max_width(bubble, max_bubble_w);

    item.time_label = chat::ui::layout::create_bubble_time(bubble);
    chat::ui::conversation::styles::apply_bubble_time(item.time_label);
    char time_buf[16];
    format_message_time(
        time_buf,
        sizeof(time_buf),
        timestamp_from_presentation_label(row.time_label));
    if (conv_.peer == 0)
    {
        std::string sender;
        if (is_self)
        {
            sender = app::configFacade().getConfig().short_name;
            if (sender.empty() && !row.sender_label.empty())
            {
                sender = row.sender_label.c_str();
            }
            if (sender.empty())
            {
                sender = "Me";
            }
        }
        else if (!row.sender_label.empty())
        {
            sender = inferred_sender.empty() ? row.sender_label.c_str() : inferred_sender;
        }
        else if (row.sender_node_id == 0)
        {
            sender = inferred_sender.empty() ? ::ui::i18n::tr("Unknown") : inferred_sender;
        }
        else
        {
            sender = app::messagingFacade().getContactService().getContactName(
                row.sender_node_id);
            if (sender.empty())
            {
                char buf[16];
                snprintf(buf, sizeof(buf), "%04lX",
                         static_cast<unsigned long>(row.sender_node_id & 0xFFFF));
                sender = buf;
            }
        }
        std::string line = sender + " " + time_buf;
        ::ui::i18n::set_content_label_text_raw(item.time_label, line.c_str());
    }
    else
    {
        ::ui::i18n::set_label_text_raw(item.time_label, time_buf);
        ::ui::fonts::apply_localized_font(
            item.time_label, lv_label_get_text(item.time_label), ::ui::fonts::ui_chrome_font());
    }

    item.text_label = chat::ui::layout::create_bubble_text(bubble);
    chat::ui::conversation::styles::apply_bubble_text(item.text_label);
    if (row.has_team_rich_payload &&
        is_structured_team_payload(row.team_rich_payload))
    {
        lv_obj_t* badge_label = chat::ui::layout::create_bubble_text(bubble);
        chat::ui::conversation::styles::apply_bubble_time(badge_label);
        const std::string badge =
            row.team_rich_payload.badge.empty()
                ? rich_payload_kind_label(row.team_rich_payload.kind)
                : std::string(row.team_rich_payload.badge.c_str());
        lv_label_set_text(badge_label, badge.c_str());
        ::ui::fonts::apply_localized_font(
            badge_label,
            lv_label_get_text(badge_label),
            ::ui::fonts::ui_chrome_font());
        const lv_coord_t max_badge_w =
            std::max<lv_coord_t>(max_bubble_w - 2 * bubble_pad_x(), 24);
        lv_obj_set_width(badge_label, max_badge_w);

        if (!row.team_rich_payload.title.empty())
        {
            lv_obj_t* title_label = chat::ui::layout::create_bubble_text(bubble);
            chat::ui::conversation::styles::apply_bubble_text(title_label);
            lv_label_set_text(title_label,
                              row.team_rich_payload.title.c_str());
            ::ui::fonts::apply_chat_content_font(
                title_label,
                row.team_rich_payload.title.c_str());
            lv_obj_set_width(title_label, max_badge_w);
        }
    }
    lv_label_set_text(item.text_label, display_text.c_str());
    ::ui::fonts::apply_chat_content_font(item.text_label, display_text.c_str());

    const lv_coord_t max_text_w = std::max<lv_coord_t>(max_bubble_w - 2 * bubble_pad_x(), 24);
    lv_obj_update_layout(item.text_label);
    const lv_coord_t natural_text_w = lv_obj_get_width(item.text_label);
    if (natural_text_w > max_text_w)
    {
        lv_obj_set_width(item.text_label, max_text_w);
    }
    else
    {
        lv_obj_set_width(item.text_label, LV_SIZE_CONTENT);
    }

    item.status_label = chat::ui::layout::create_bubble_status(bubble);
    chat::ui::conversation::styles::apply_bubble_status(item.status_label);
    if (row.delivery == ::ui::chat::MessageDeliveryState::Failed)
    {
        ::ui::i18n::set_label_text(item.status_label, "Failed");
        ::ui::fonts::apply_localized_font(
            item.status_label, lv_label_get_text(item.status_label), ::ui::fonts::ui_chrome_font());
        lv_obj_clear_flag(item.status_label, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_label_set_text(item.status_label, "");
        ::ui::fonts::apply_localized_font(
            item.status_label, lv_label_get_text(item.status_label), ::ui::fonts::ui_chrome_font());
        lv_obj_add_flag(item.status_label, LV_OBJ_FLAG_HIDDEN);
    }

    // Align row based on sender (same behavior)
    chat::ui::layout::align_message_row(item.container, is_self);

    messages_.push_back(item);
}

void ChatConversationScreen::action_event_cb(lv_event_t* e)
{
    auto* ctx = static_cast<ActionContext*>(lv_event_get_user_data(e));
    if (!ctx || !ctx->screen)
    {
        return;
    }
    ChatConversationScreen* screen = ctx->screen;
    if (!screen->guard_ || !screen->guard_->alive)
    {
        return;
    }
    if (ctx->intent == ActionIntent::SaveContact)
    {
        // Save is handled entirely inside the screen (own peer + own prompt +
        // ContactService), independent of the reply-enabled gate and the owner's
        // action callback.
        screen->schedule_save_async();
        return;
    }
    // The reply-enabled gate only applies to the Reply action.
    if (ctx->intent == ActionIntent::Reply && !screen->reply_enabled_)
    {
        return;
    }
    screen->schedule_action_async(ctx->intent);
}

void ChatConversationScreen::async_action_cb(void* user_data)
{
    auto* payload = static_cast<ActionPayload*>(user_data);
    if (!payload)
    {
        return;
    }
    LifetimeGuard* guard = payload->guard;
    if (guard && guard->alive && payload->action_cb)
    {
        payload->action_cb(payload->intent, payload->user_data);
    }
    if (guard && guard->pending_async > 0)
    {
        guard->pending_async--;
        if (!guard->alive && guard->pending_async == 0)
        {
            delete guard;
        }
    }
    delete payload;
}

void ChatConversationScreen::on_root_deleted(lv_event_t* e)
{
    auto* screen = static_cast<ChatConversationScreen*>(lv_event_get_user_data(e));
    if (!screen)
    {
        return;
    }
    screen->handle_root_deleted();
}

void ChatConversationScreen::handle_back(void* user_data)
{
    ChatConversationScreen* screen = static_cast<ChatConversationScreen*>(user_data);
    if (!screen || !screen->guard_ || !screen->guard_->alive)
    {
        return;
    }
    screen->schedule_back_async();
}

void ChatConversationScreen::async_back_cb(void* user_data)
{
    auto* payload = static_cast<BackPayload*>(user_data);
    if (!payload)
    {
        return;
    }
    LifetimeGuard* guard = payload->guard;
    if (guard && guard->alive && payload->back_cb)
    {
        payload->back_cb(payload->user_data);
    }
    if (guard && guard->pending_async > 0)
    {
        guard->pending_async--;
        if (!guard->alive && guard->pending_async == 0)
        {
            delete guard;
        }
    }
    delete payload;
}

lv_timer_t* ChatConversationScreen::add_timer(lv_timer_cb_t cb,
                                              uint32_t period_ms,
                                              void* user_data,
                                              TimerDomain domain)
{
    if (!guard_ || !guard_->alive)
    {
        return nullptr;
    }
    lv_timer_t* timer = lv_timer_create(cb, period_ms, user_data);
    if (timer)
    {
        TimerEntry entry;
        entry.timer = timer;
        entry.domain = domain;
        timers_.push_back(entry);
    }
    return timer;
}

void ChatConversationScreen::clear_timers(TimerDomain domain)
{
    if (timers_.empty())
    {
        return;
    }
    for (auto& entry : timers_)
    {
        if (entry.timer && entry.domain == domain)
        {
            lv_timer_del(entry.timer);
            entry.timer = nullptr;
        }
    }
    timers_.erase(
        std::remove_if(timers_.begin(), timers_.end(),
                       [](const TimerEntry& entry)
                       { return entry.timer == nullptr; }),
        timers_.end());
}

void ChatConversationScreen::clear_all_timers()
{
    for (auto& entry : timers_)
    {
        if (entry.timer)
        {
            lv_timer_del(entry.timer);
            entry.timer = nullptr;
        }
    }
    timers_.clear();
}

void ChatConversationScreen::handle_root_deleted()
{
    if ((!guard_ || !guard_->alive) && container_ == nullptr && msg_list_ == nullptr)
    {
        return;
    }

    if (guard_)
    {
        guard_->alive = false;
    }

    // This fires from container_'s own LV_EVENT_DELETE, before LVGL walks and frees
    // its children, so save_modal_ (a child of container_) is still valid here. Close
    // it explicitly to unwind the focus group and null the pointers; the explicit
    // lv_obj_del also keeps the child out of the about-to-run recursive teardown.
    // Do not restore the previous focus group: the owner installs the next screen's
    // group as part of this teardown.
    closeSaveContactPrompt(false);

    action_cb_ = nullptr;
    action_cb_user_data_ = nullptr;
    back_cb_ = nullptr;
    back_cb_user_data_ = nullptr;
    reply_ctx_.screen = nullptr;
    save_ctx_.screen = nullptr;

    chat::ui::conversation::input::cleanup(&input_binding_);
    clear_all_timers();

    if (top_bar_.back_btn)
    {
        ::ui::widgets::top_bar_set_back_callback(top_bar_, nullptr, nullptr);
    }

    container_ = nullptr;
    msg_list_ = nullptr;
    action_bar_ = nullptr;
    reply_btn_ = nullptr;
    save_btn_ = nullptr;
    save_label_ = nullptr;
    compose_btn_ = nullptr;
}

void ChatConversationScreen::schedule_action_async(ActionIntent intent)
{
    if (!guard_ || !guard_->alive || !action_cb_)
    {
        return;
    }
    auto* payload = new ActionPayload();
    payload->guard = guard_;
    payload->action_cb = action_cb_;
    payload->user_data = action_cb_user_data_;
    payload->intent = intent;
    guard_->pending_async++;
    lv_async_call(async_action_cb, payload);
}

void ChatConversationScreen::schedule_back_async()
{
    if (!guard_ || !guard_->alive || !back_cb_)
    {
        return;
    }
    auto* payload = new BackPayload();
    payload->guard = guard_;
    payload->back_cb = back_cb_;
    payload->user_data = back_cb_user_data_;
    guard_->pending_async++;
    lv_async_call(async_back_cb, payload);
}

} // namespace ui
} // namespace chat

#endif

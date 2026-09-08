#include "ui/screens/team/team_page_lvgl_renderer.h"

#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/screens/team/team_page_styles.h"

#include <cstdio>

namespace team
{
namespace ui
{
namespace
{
constexpr int kActionBtnHeight = 28;
constexpr int kActionBtnPadH = 10;

int actionButtonHeight()
{
    const auto& profile = ::ui::page_profile::current();
    if (profile.dense)
    {
        return ::ui::page_profile::resolve_control_button_height();
    }
    return profile.large_touch_hitbox ? profile.list_item_height : kActionBtnHeight;
}

int actionButtonPadH()
{
    return ::ui::page_profile::is_dense() ? 7 : kActionBtnPadH;
}

int listItemHeight()
{
    return ::ui::page_profile::current().list_item_height;
}

} // namespace

TeamPageLvglRenderer::TeamPageLvglRenderer(uint32_t now_s)
    : now_s_(now_s)
{
}

void TeamPageLvglRenderer::render(
    TeamPageLvglRendererContext& context,
    const TeamPageLvglRendererInput& input,
    const TeamPageLvglRendererHandlers& handlers,
    const ITeamPageLvglNameResolver& names) const
{
    clearContent(context);
    if (context.body)
    {
        lv_obj_set_style_translate_y(context.body, 0, 0);
        const lv_coord_t pad = ::ui::page_profile::is_dense() ? 3 : 6;
        const lv_coord_t row = ::ui::page_profile::is_dense() ? 2 : 6;
        lv_obj_set_style_pad_top(context.body, pad, 0);
        lv_obj_set_style_pad_bottom(context.body, pad, 0);
        lv_obj_set_style_pad_left(context.body, pad, 0);
        lv_obj_set_style_pad_right(context.body, pad, 0);
        lv_obj_set_style_pad_row(context.body, row, 0);
    }

    switch (input.page)
    {
    case TeamPage::StatusNotInTeam:
        renderStatusNotInTeam(context, handlers, input.actions_enabled);
        break;
    case TeamPage::StatusInTeam:
        renderStatusInTeam(context, input.read_model, handlers);
        break;
    case TeamPage::TeamHome:
        renderTeamHome(context, input.read_model, handlers);
        break;
    case TeamPage::JoinPending:
        renderJoinPending(context, input, handlers, names);
        break;
    case TeamPage::Members:
        renderMembers(context, input.read_model, handlers, names);
        break;
    case TeamPage::MemberDetail:
        renderMemberDetail(context, input.read_model, handlers);
        break;
    case TeamPage::KickConfirm:
        renderKickConfirm(context, input.read_model, handlers);
        break;
    case TeamPage::KickedOut:
        renderKickedOut(context, handlers);
        break;
    default:
        renderStatusNotInTeam(context, handlers, input.actions_enabled);
        break;
    }
}

void TeamPageLvglRenderer::clearContent(
    TeamPageLvglRendererContext& context) const
{
    if (context.body)
    {
        lv_obj_clean(context.body);
    }
    if (context.actions)
    {
        lv_obj_clean(context.actions);
    }
    if (context.list_items)
    {
        context.list_items->clear();
    }
    for (std::size_t i = 0; context.action_btns && i < context.action_btn_count; ++i)
    {
        context.action_btns[i] = nullptr;
    }
    for (std::size_t i = 0; context.action_labels && i < context.action_label_count; ++i)
    {
        context.action_labels[i] = nullptr;
    }
    if (context.detail_label)
    {
        *context.detail_label = nullptr;
    }
    if (context.focusables)
    {
        context.focusables->clear();
    }
    if (context.default_focus)
    {
        *context.default_focus = nullptr;
    }
}

void TeamPageLvglRenderer::registerFocus(
    TeamPageLvglRendererContext& context,
    lv_obj_t* obj,
    bool is_default) const
{
    if (!obj || !context.focusables)
    {
        return;
    }
    context.focusables->push_back(obj);
    if (context.default_focus &&
        (is_default || *context.default_focus == nullptr))
    {
        *context.default_focus = obj;
    }
}

void TeamPageLvglRenderer::updateTopBarTitle(
    TeamPageLvglRendererContext& context,
    const char* title) const
{
    if (!context.top_bar || !title)
    {
        return;
    }
    ::ui::widgets::top_bar_set_title(*context.top_bar, title);
}

lv_obj_t* TeamPageLvglRenderer::addLabel(lv_obj_t* parent,
                                         const char* text,
                                         bool section,
                                         bool meta) const
{
    lv_obj_t* label = lv_label_create(parent);
    ::ui::i18n::set_label_text_raw(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    if (section)
    {
        style::apply_section_label(label);
    }
    else if (meta)
    {
        style::apply_meta_label(label);
    }
    return label;
}

lv_obj_t* TeamPageLvglRenderer::createFittedButton(
    lv_obj_t* parent,
    const char* text,
    lv_event_cb_t cb) const
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_height(btn, actionButtonHeight());
    style::apply_button_secondary(btn);
    const lv_coord_t pad_h = actionButtonPadH();
    lv_obj_set_style_pad_hor(btn, pad_h, LV_PART_MAIN);
    lv_obj_t* label = lv_label_create(btn);
    ::ui::i18n::set_label_text(label, text);
    lv_obj_update_layout(label);
    lv_coord_t width = lv_obj_get_width(label) + (pad_h * 2);
    if (width < ::ui::page_profile::resolve_compact_button_min_width())
    {
        width = ::ui::page_profile::resolve_compact_button_min_width();
    }
    lv_obj_set_width(btn, width);
    lv_obj_center(label);
    if (cb)
    {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    }
    return btn;
}

lv_obj_t* TeamPageLvglRenderer::createActionButton(
    TeamPageLvglRendererContext& context,
    const char* text,
    lv_event_cb_t cb) const
{
    return createFittedButton(context.actions, text, cb);
}

lv_obj_t* TeamPageLvglRenderer::createListItem(
    TeamPageLvglRendererContext& context,
    const char* left,
    const char* right) const
{
    lv_obj_t* btn = lv_btn_create(context.body);
    lv_obj_set_size(btn, LV_PCT(100), listItemHeight());
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    style::apply_list_item(btn);

    lv_obj_t* left_label = lv_label_create(btn);
    ::ui::i18n::set_content_label_text_raw(left_label, left);
    lv_obj_set_width(left_label, LV_PCT(70));
    lv_label_set_long_mode(left_label, LV_LABEL_LONG_CLIP);

    lv_obj_t* right_label = lv_label_create(btn);
    ::ui::i18n::set_label_text(right_label, right);
    lv_obj_set_width(right_label, LV_PCT(30));
    lv_label_set_long_mode(right_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(right_label, LV_TEXT_ALIGN_RIGHT, 0);

    if (context.list_items)
    {
        context.list_items->push_back(btn);
    }
    return btn;
}

void TeamPageLvglRenderer::renderMemberChipRow(
    TeamPageLvglRendererContext& context,
    const std::vector<TeamMemberRowView>& rows) const
{
    lv_obj_t* row = lv_obj_create(context.body);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    for (const auto& member : rows)
    {
        lv_obj_t* label = lv_label_create(row);
        ::ui::i18n::set_content_label_text_raw(label, member.name.c_str());
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(label, LV_PCT(24));
        lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(
            label,
            lv_color_hex(team_color_from_index(member.color_index)),
            0);
        lv_obj_set_style_pad_hor(label, 4, 0);
        lv_obj_set_style_pad_ver(label, 3, 0);
        lv_obj_set_style_radius(label, 6, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        if (member.color_index == 3)
        {
            lv_obj_set_style_text_color(label, lv_color_black(), 0);
        }
        else
        {
            lv_obj_set_style_text_color(label, lv_color_white(), 0);
        }
    }
}

void TeamPageLvglRenderer::renderMemberChipsOrEmpty(
    TeamPageLvglRendererContext& context,
    const TeamPageReadModelInput& input) const
{
    const auto rows = TeamPageReadModel(now_s_).buildMemberRows(input.members);
    if (rows.empty())
    {
        addLabel(context.body, ::ui::i18n::tr("No members yet"), false, true);
        return;
    }
    renderMemberChipRow(context, rows);
}

void TeamPageLvglRenderer::renderStatusNotInTeam(
    TeamPageLvglRendererContext& context,
    const TeamPageLvglRendererHandlers& handlers,
    bool actions_enabled) const
{
    updateTopBarTitle(context, ::ui::i18n::tr("Team"));

    addLabel(context.body, ::ui::i18n::tr("You are not in a team"), true, false);
    addLabel(context.body, ::ui::i18n::tr("- No shared map\n- No team awareness"), false, true);
    addLabel(context.body, ::ui::i18n::tr("Both units on the same channel"), false, false);

    // When the live team controller/pairing backend is absent (the IDF
    // safe-screen bind), Create/Join have nothing functional to drive, so present
    // them as clearly-disabled "(soon)" controls rather than tappable-but-dead
    // buttons. This mirrors the management_actions_enabled gating used for the
    // Kick / Transfer Leader buttons on the member-detail page.
    if (!actions_enabled)
    {
        addLabel(context.body,
                 ::ui::i18n::tr("Team pairing is not available on this build yet"),
                 false,
                 true);
    }

    if (context.action_btns && context.action_btn_count > 0)
    {
        context.action_btns[0] = createActionButton(
            context,
            actions_enabled ? "Create Team" : "Create Team (soon)",
            actions_enabled ? handlers.create_team : nullptr);
        if (!actions_enabled)
        {
            lv_obj_add_state(context.action_btns[0], LV_STATE_DISABLED);
        }
        registerFocus(context, context.action_btns[0], true);
    }
    if (context.action_btns && context.action_btn_count > 1)
    {
        context.action_btns[1] = createActionButton(
            context,
            actions_enabled ? "Join Team" : "Join Team (soon)",
            actions_enabled ? handlers.join_team : nullptr);
        if (!actions_enabled)
        {
            lv_obj_add_state(context.action_btns[1], LV_STATE_DISABLED);
        }
        registerFocus(context, context.action_btns[1]);
    }
}

void TeamPageLvglRenderer::renderStatusInTeam(
    TeamPageLvglRendererContext& context,
    const TeamPageReadModelInput& input,
    const TeamPageLvglRendererHandlers& handlers) const
{
    updateTopBarTitle(context, ::ui::i18n::tr("Team Status"));

    const auto summary = TeamPageReadModel(now_s_).buildSummary(input);
    addLabel(context.body,
             ::ui::i18n::format("Team: %s", summary.team_name.c_str()).c_str(),
             true,
             false);
    addLabel(context.body,
             ::ui::i18n::format("Role: %s",
                                ::ui::i18n::tr(summary.self_is_leader ? "Leader" : "Member"))
                 .c_str(),
             false,
             true);
    addLabel(context.body,
             ::ui::i18n::format("Members: %u",
                                static_cast<unsigned>(summary.member_count))
                 .c_str(),
             false,
             true);
    addLabel(context.body,
             ::ui::i18n::format("Online: %u",
                                static_cast<unsigned>(summary.online_count))
                 .c_str(),
             false,
             true);
    if (!summary.has_security_round)
    {
        addLabel(context.body, ::ui::i18n::tr("KeyId: --"), false, true);
        addLabel(context.body, ::ui::i18n::tr("Key round: --"), false, true);
    }
    else
    {
        addLabel(context.body,
                 ::ui::i18n::format("KeyId: %u",
                                    static_cast<unsigned>(summary.security_round))
                     .c_str(),
                 false,
                 true);
        addLabel(context.body,
                 ::ui::i18n::format("Key round: %u",
                                    static_cast<unsigned>(summary.security_round))
                     .c_str(),
                 false,
                 true);
    }
    if (summary.waiting_new_keys)
    {
        addLabel(context.body, ::ui::i18n::tr("Key recovery unavailable"), false, true);
    }

    addLabel(context.body, ::ui::i18n::tr("Team Health"), true, false);
    const std::string last_update = formatLastUpdate(summary.last_update);
    const unsigned online_members = static_cast<unsigned>(summary.online_count);
    const unsigned total_members = static_cast<unsigned>(summary.member_count);
    const unsigned stale_members =
        (total_members > online_members) ? (total_members - online_members) : 0u;
    const std::string team_health =
        std::string("- ") +
        ::ui::i18n::format("%u/%u online", online_members, total_members) +
        "\n- " + last_update +
        "\n- " + ::ui::i18n::format("%u stale", stale_members);
    addLabel(context.body, team_health.c_str(), false, true);

    if (context.action_btns && context.action_btn_count > 0)
    {
        context.action_btns[0] =
            createActionButton(context, "View Team", handlers.view_team);
        registerFocus(context, context.action_btns[0], true);
    }
    if (context.action_btns && context.action_btn_count > 1)
    {
        context.action_btns[1] =
            input.self_is_leader
                ? createActionButton(context, "Pair Member", handlers.invite)
                : createActionButton(context, "Key Recovery", handlers.request_keys);
        registerFocus(context, context.action_btns[1]);
    }
    if (context.action_btns && context.action_btn_count > 2)
    {
        context.action_btns[2] =
            createActionButton(context, "Leave", handlers.leave);
        registerFocus(context, context.action_btns[2]);
    }
}

void TeamPageLvglRenderer::renderTeamHome(
    TeamPageLvglRendererContext& context,
    const TeamPageReadModelInput& input,
    const TeamPageLvglRendererHandlers& handlers) const
{
    const auto summary = TeamPageReadModel(now_s_).buildSummary(input);
    const std::string title = ::ui::i18n::format(
        "Team / %s",
        ::ui::i18n::tr(summary.self_is_leader ? "Leader" : "Member"));
    updateTopBarTitle(context, title.c_str());

    addLabel(context.body,
             ::ui::i18n::format("Team: %s", summary.team_name.c_str()).c_str(),
             true,
             false);
    addLabel(context.body,
             ::ui::i18n::format("Members: %u  Online: %u",
                                static_cast<unsigned>(summary.member_count),
                                static_cast<unsigned>(summary.online_count))
                 .c_str(),
             false,
             true);
    if (!summary.has_security_round)
    {
        addLabel(context.body, ::ui::i18n::tr("Security Round: --"), false, true);
    }
    else
    {
        addLabel(context.body,
                 ::ui::i18n::format("Security Round: %u",
                                    static_cast<unsigned>(summary.security_round))
                     .c_str(),
                 false,
                 true);
    }

    addLabel(context.body, ::ui::i18n::tr("Members"), true, false);
    renderMemberChipsOrEmpty(context, input);

    if (context.action_btns && context.action_btn_count > 0)
    {
        context.action_btns[0] =
            createActionButton(context, "Pair Member", handlers.invite);
        registerFocus(context, context.action_btns[0],
                      context.default_focus && *context.default_focus == nullptr);
    }
    if (context.action_btns && context.action_btn_count > 1)
    {
        context.action_btns[1] =
            createActionButton(context, "Manage", handlers.manage);
        registerFocus(context, context.action_btns[1]);
    }
    if (context.action_btns && context.action_btn_count > 2)
    {
        context.action_btns[2] =
            createActionButton(context, "Leave", handlers.leave);
        registerFocus(context, context.action_btns[2]);
    }
}

void TeamPageLvglRenderer::renderJoinPending(
    TeamPageLvglRendererContext& context,
    const TeamPageLvglRendererInput& input,
    const TeamPageLvglRendererHandlers& handlers,
    const ITeamPageLvglNameResolver& names) const
{
    lv_obj_set_style_translate_y(context.body, -5, 0);
    lv_obj_set_style_pad_top(context.body, 2, 0);
    lv_obj_set_style_pad_bottom(context.body, 2, 0);
    lv_obj_set_style_pad_row(context.body, 2, 0);

    const auto pending =
        TeamPageReadModel(now_s_).buildJoinPending(input.read_model);
    updateTopBarTitle(context, ::ui::i18n::tr(pending.title));

    addLabel(context.body, ::ui::i18n::tr("Pairing in progress"), true, false);
    addLabel(context.body, ::ui::i18n::tr("Both units on the same channel"), false, true);
    if (pending.show_leader_members)
    {
        addLabel(context.body, ::ui::i18n::tr("Members"), true, false);
        renderMemberChipsOrEmpty(context, input.read_model);
        if (input.pairing_peer_id != 0)
        {
            const std::string name = names.resolveNodeName(input.pairing_peer_id);
            const std::string paired =
                ::ui::i18n::format("Last paired: %s", name.c_str());
            addLabel(context.body, paired.c_str(), false, true);
        }
    }
    if (!pending.target_team_name.empty())
    {
        const std::string line =
            ::ui::i18n::format("Target: %s", pending.target_team_name.c_str());
        addLabel(context.body, line.c_str(), false, true);
    }

    addLabel(context.body, ::ui::i18n::tr(pending.state_line), false, true);

    if (context.action_btns && context.action_btn_count > 0)
    {
        context.action_btns[0] =
            createActionButton(context, "Cancel", handlers.join_cancel);
        registerFocus(context, context.action_btns[0], true);
    }
    if (context.action_btns && context.action_btn_count > 1)
    {
        context.action_btns[1] =
            createActionButton(context, "Retry", handlers.join_retry);
        registerFocus(context, context.action_btns[1]);
    }
}

void TeamPageLvglRenderer::renderMembers(
    TeamPageLvglRendererContext& context,
    const TeamPageReadModelInput& input,
    const TeamPageLvglRendererHandlers& handlers,
    const ITeamPageLvglNameResolver& names) const
{
    updateTopBarTitle(context, ::ui::i18n::tr("Members"));
    const auto rows = TeamPageReadModel(now_s_).buildMemberRows(input.members);

    if (rows.empty())
    {
        addLabel(context.body, ::ui::i18n::tr("No members yet"), false, true);
    }
    for (const auto& member : rows)
    {
        // ASCII status marker. The previous U+25CF/U+25CB dots are not in the
        // compiled Montserrat fonts (only 14/20/24: Latin + the LV_SYMBOL set), so
        // they rendered as a missing-glyph "tofu" box.
        const char* marker = member.online ? "* " : "o ";
        // A LoRa-paired peer often has no NodeInfo-exchanged roster name, and a later
        // presence re-sync can blank it, so member.name may be empty -> the row would
        // render blank. Fall back to the SAME resolver the pairing screen uses
        // (contact name, else an 8-hex node id), which is never empty, so a member is
        // always identifiable.
        // Treat empty OR whitespace-only roster names as blank and fall back to the
        // resolver (contact name, else 8-hex node id), which is never empty.
        const bool name_blank =
            member.name.find_first_not_of(" \t\r\n") == std::string::npos;
        const std::string display_name =
            name_blank ? names.resolveNodeName(member.node_id) : member.name;
        std::string left = std::string(marker) + display_name;
        if (member.leader)
        {
            left += " (" + std::string(::ui::i18n::tr("Leader")) + ")";
        }
        lv_obj_t* item = createListItem(context, left.c_str(), "Select");
        lv_obj_set_user_data(item, (void*)(intptr_t)member.source_index);
        lv_obj_add_event_cb(item, handlers.member_clicked, LV_EVENT_CLICKED, nullptr);
        registerFocus(context,
                      item,
                      context.default_focus && *context.default_focus == nullptr);
    }
}

void TeamPageLvglRenderer::renderMemberDetail(
    TeamPageLvglRendererContext& context,
    const TeamPageReadModelInput& input,
    const TeamPageLvglRendererHandlers& handlers) const
{
    const auto detail = TeamPageReadModel(now_s_).buildSelectedMember(input);
    if (!detail.valid)
    {
        return;
    }

    const std::string title =
        ::ui::i18n::format("Member: %s", detail.member.name.c_str());
    updateTopBarTitle(context, title.c_str());

    const std::string status =
        detail.member.online ? std::string(::ui::i18n::tr("Online"))
                             : formatLastSeen(detail.last_seen);
    char line[64];
    std::snprintf(line,
                  sizeof(line),
                  "%s",
                  ::ui::i18n::format("Status: %s",
                                     ::ui::i18n::tr(status.c_str()))
                      .c_str());
    addLabel(context.body, line, true, false);
    std::snprintf(line,
                  sizeof(line),
                  "%s",
                  ::ui::i18n::format(
                      "Role: %s",
                      ::ui::i18n::tr(detail.member.leader ? "Leader" : "Member"))
                      .c_str());
    addLabel(context.body, line, false, true);
    addLabel(context.body,
             ::ui::i18n::format("Device: %s", "Pager").c_str(),
             false,
             true);
    addLabel(context.body, ::ui::i18n::tr("Capability:"), true, false);
    const std::string capabilities =
        std::string("- ") + ::ui::i18n::tr("Position") +
        "\n- " + ::ui::i18n::tr("Waypoint");
    addLabel(context.body, capabilities.c_str(), false, true);

    if (context.action_btns && context.action_btn_count > 0)
    {
        context.action_btns[0] =
            createActionButton(context, "Kick", handlers.kick);
        if (!detail.management_actions_enabled)
        {
            lv_obj_add_state(context.action_btns[0], LV_STATE_DISABLED);
        }
        registerFocus(context, context.action_btns[0], true);
    }
    if (context.action_btns && context.action_btn_count > 1)
    {
        context.action_btns[1] =
            createActionButton(context, "Transfer Leader", handlers.transfer_leader);
        if (!detail.management_actions_enabled)
        {
            lv_obj_add_state(context.action_btns[1], LV_STATE_DISABLED);
        }
        registerFocus(context, context.action_btns[1]);
    }
}

void TeamPageLvglRenderer::renderKickConfirm(
    TeamPageLvglRendererContext& context,
    const TeamPageReadModelInput& input,
    const TeamPageLvglRendererHandlers& handlers) const
{
    updateTopBarTitle(context, ::ui::i18n::tr("Kick Member"));

    std::string name = ::ui::i18n::tr("member");
    if (input.selected_member_index >= 0 &&
        input.selected_member_index < static_cast<int>(input.members.size()))
    {
        name = input.members[static_cast<std::size_t>(input.selected_member_index)].name;
    }

    const std::string line =
        ::ui::i18n::format("Remove %s from team?", name.c_str());
    addLabel(context.body, line.c_str(), true, false);
    addLabel(context.body,
             ::ui::i18n::tr("This will update the security round.\nThe member will no longer receive\nteam messages or waypoints."),
             false,
             true);

    if (context.action_btns && context.action_btn_count > 0)
    {
        context.action_btns[0] =
            createActionButton(context, "Cancel", handlers.kick_cancel);
        registerFocus(context, context.action_btns[0], true);
    }
    if (context.action_btns && context.action_btn_count > 1)
    {
        context.action_btns[1] =
            createActionButton(context, "Confirm Kick", handlers.kick_confirm);
        registerFocus(context, context.action_btns[1]);
    }
}

void TeamPageLvglRenderer::renderKickedOut(
    TeamPageLvglRendererContext& context,
    const TeamPageLvglRendererHandlers& handlers) const
{
    updateTopBarTitle(context, ::ui::i18n::tr("Team"));

    addLabel(context.body, ::ui::i18n::tr("You are no longer in this team"), true, false);
    addLabel(context.body, ::ui::i18n::tr("Access to team data revoked"), false, true);

    if (context.action_btns && context.action_btn_count > 0)
    {
        context.action_btns[0] =
            createActionButton(context, "Pair Again", handlers.kicked_join);
        registerFocus(context, context.action_btns[0], true);
    }
    if (context.action_btns && context.action_btn_count > 1)
    {
        context.action_btns[1] =
            createActionButton(context, "OK", handlers.kicked_ok);
        registerFocus(context, context.action_btns[1]);
    }
}

std::string TeamPageLvglRenderer::formatLastSeen(
    TeamRelativeTimeView view) const
{
    switch (view.kind)
    {
    case TeamRelativeTimeKind::Online:
        return ::ui::i18n::tr("Online");
    case TeamRelativeTimeKind::MinutesAgo:
        return ::ui::i18n::format("Last seen %um ago",
                                  static_cast<unsigned>(view.value));
    case TeamRelativeTimeKind::HoursAgo:
        return ::ui::i18n::format("Last seen %uh ago",
                                  static_cast<unsigned>(view.value));
    case TeamRelativeTimeKind::Unknown:
    default:
        break;
    }
    return ::ui::i18n::tr("Last seen --");
}

std::string TeamPageLvglRenderer::formatLastUpdate(
    TeamRelativeTimeView view) const
{
    if (view.kind != TeamRelativeTimeKind::SecondsAgo)
    {
        return ::ui::i18n::tr("Last update --");
    }
    return ::ui::i18n::format("Last update %us ago",
                              static_cast<unsigned>(view.value));
}

} // namespace ui
} // namespace team

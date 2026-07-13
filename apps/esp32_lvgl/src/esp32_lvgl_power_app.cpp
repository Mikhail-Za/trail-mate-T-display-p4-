// Battery / Power: a read-only diagnostics screen for the fuel gauge.
//
// Shows charge percent (with an at-a-glance bar), charge/discharge state, the
// power tier, and -- on boards that expose it (T-Display P4) -- the numeric pack
// voltage and signed current straight from the gauge. The topbar battery poll uses
// the cheap battery_info() path; only this app calls battery_power_detail(), which
// does the extra gauge reads, so opening it is the only time that cost is paid.
//
// Self-contained inline app, same pattern as Translate/Field Guide: file-static
// state, enter()/exit() build and tear down everything, a ~2s refresh timer, back
// routes through ui_request_exit_to_menu().

#include "lvgl.h"
#include "ui/widgets/back_button.h"
#include "ui/app_runtime.h"
#include "ui/callback_app_screen.h"
#include "platform/ui/device_runtime.h"

#include <cstdio>

namespace
{

// Rough-estimate only: the P4 gauge does not report design capacity here, so a
// precise runtime figure is impossible. This assumed pack size lets the app show a
// clearly-labelled coarse "est." while a real capacity is unknown; it is a stated
// assumption, not a measured value. Shown only when discharging with a meaningful
// draw so a near-zero current cannot blow the estimate up to a fake huge number.
constexpr int kAssumedCapacityMah = 2000;
constexpr int kMinEstimateCurrentMa = 15;  // below this |current|, hide the estimate

struct PowerAppState
{
    lv_obj_t* root = nullptr;
    lv_obj_t* percent_label = nullptr;
    lv_obj_t* bar = nullptr;
    lv_obj_t* status_label = nullptr;
    lv_obj_t* tier_label = nullptr;
    lv_obj_t* voltage_label = nullptr;
    lv_obj_t* current_label = nullptr;
    lv_obj_t* estimate_label = nullptr;
    lv_timer_t* timer = nullptr;
};

PowerAppState s_state;

lv_obj_t* make_line(lv_obj_t* parent, const lv_font_t* font)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x202020), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return label;
}

void power_refresh(PowerAppState* st)
{
    if (!st)
    {
        return;
    }

    const platform::ui::device::BatteryInfo info = platform::ui::device::battery_info();
    const platform::ui::device::BatteryPowerDetail detail =
        platform::ui::device::battery_power_detail();
    const int tier = platform::ui::device::power_tier();

    char buf[96];

    // Percent + bar.
    const int level = info.level;
    if (st->percent_label && lv_obj_is_valid(st->percent_label))
    {
        if (level < 0)
        {
            lv_label_set_text(st->percent_label, "--%");
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "%d%%", level);
            lv_label_set_text(st->percent_label, buf);
        }
    }
    if (st->bar && lv_obj_is_valid(st->bar))
    {
        const int shown = (level < 0) ? 0 : level;
        lv_bar_set_value(st->bar, shown, LV_ANIM_OFF);
        // Low battery reads red, otherwise green (mirrors power_tier's 15% threshold).
        const lv_color_t fill =
            (level >= 0 && level <= 15) ? lv_color_hex(0xD03030) : lv_color_hex(0x2FA84F);
        lv_obj_set_style_bg_color(st->bar, fill, LV_PART_INDICATOR);
    }

    // Charge/discharge state. Prefer the signed current only when the current read
    // actually succeeded (current_valid); a failed current read returns 0 mA and must
    // NOT be read as "discharging". Otherwise fall back to battery_info().charging.
    const bool charging =
        detail.current_valid ? (detail.current_ma > 0) : info.charging;
    if (st->status_label && lv_obj_is_valid(st->status_label))
    {
        const char* status;
        if (level < 0 && !charging)
        {
            // Gauge level never read (cold boot / probe failure) and no positive
            // charging signal: mirror the "--%" percent path's honesty instead of
            // falsely claiming "Discharging". A known-charging device still reads
            // "Charging" below even when the level is unknown.
            status = "Unknown";
        }
        else if (level >= 100)
        {
            status = "Full";
        }
        else if (charging)
        {
            status = "Charging";
        }
        else
        {
            status = "Discharging";
        }
        lv_label_set_text(st->status_label, status);
    }

    // Power tier (0 = normal, 1 = low-power). Annotate when the low tier is active.
    if (st->tier_label && lv_obj_is_valid(st->tier_label))
    {
        std::snprintf(buf, sizeof(buf), "Power tier: %d%s", tier, tier >= 1 ? " (low)" : "");
        lv_label_set_text(st->tier_label, buf);
    }

    // Numeric voltage (V, 3 decimals) and signed current (mA), or n/a when the board
    // does not expose them.
    if (st->voltage_label && lv_obj_is_valid(st->voltage_label))
    {
        if (detail.valid)
        {
            std::snprintf(buf, sizeof(buf), "Voltage: %d.%03d V", detail.voltage_mv / 1000,
                          detail.voltage_mv % 1000);
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "Voltage: n/a");
        }
        lv_label_set_text(st->voltage_label, buf);
    }
    if (st->current_label && lv_obj_is_valid(st->current_label))
    {
        if (!detail.current_valid)
        {
            std::snprintf(buf, sizeof(buf), "Current: n/a");
        }
        else if (detail.current_ma > 0)
        {
            std::snprintf(buf, sizeof(buf), "Current: +%d mA (charging)", detail.current_ma);
        }
        else if (detail.current_ma < 0)
        {
            std::snprintf(buf, sizeof(buf), "Current: %d mA (draining)", detail.current_ma);
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "Current: 0 mA");
        }
        lv_label_set_text(st->current_label, buf);
    }

    // Coarse runtime estimate: shown ONLY while discharging with a real draw, and
    // explicitly labelled as a rough figure against an assumed pack capacity (the
    // gauge does not report capacity here). Blanked otherwise so nothing fabricated
    // is presented. See kAssumedCapacityMah above.
    if (st->estimate_label && lv_obj_is_valid(st->estimate_label))
    {
        const int draw_ma = detail.valid ? -detail.current_ma : 0;  // >0 while draining
        if (detail.valid && level > 0 && draw_ma >= kMinEstimateCurrentMa)
        {
            const long remaining_mah =
                static_cast<long>(kAssumedCapacityMah) * static_cast<long>(level) / 100L;
            long minutes = remaining_mah * 60L / static_cast<long>(draw_ma);
            if (minutes > 99L * 60L)
            {
                minutes = 99L * 60L;  // cap so a tiny draw never prints an absurd number
            }
            std::snprintf(buf, sizeof(buf),
                          "Est. runtime: ~%ldh %02ldm\n(rough, assumes ~%d mAh pack)",
                          minutes / 60L, minutes % 60L, kAssumedCapacityMah);
            lv_label_set_text(st->estimate_label, buf);
        }
        else
        {
            lv_label_set_text(st->estimate_label, "");
        }
    }
}

void power_tick(lv_timer_t* timer)
{
    auto* st = static_cast<PowerAppState*>(lv_timer_get_user_data(timer));
    if (!st || !st->root || !lv_obj_is_valid(st->root))
    {
        return;
    }
    power_refresh(st);
}

void power_enter(void* user_data, lv_obj_t* parent)
{
    auto* st = static_cast<PowerAppState*>(user_data);
    if (!st || !parent || (st->root && lv_obj_is_valid(st->root)))
    {
        return;
    }

    st->percent_label = nullptr;
    st->bar = nullptr;
    st->status_label = nullptr;
    st->tier_label = nullptr;
    st->voltage_label = nullptr;
    st->current_label = nullptr;
    st->estimate_label = nullptr;
    st->timer = nullptr;

    st->root = lv_obj_create(parent);
    lv_obj_set_size(st->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(st->root, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(st->root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(st->root, 0, 0);
    lv_obj_set_style_radius(st->root, 0, 0);
    lv_obj_set_style_pad_all(st->root, 18, 0);
    lv_obj_set_style_pad_row(st->root, 12, 0);
    lv_obj_set_flex_flow(st->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(st->root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    // Vertical scroll as a safety net: the panels differ in height (1168 vs 1232) and
    // the estimate line can wrap, so the column can always be dragged into view.
    lv_obj_set_scroll_dir(st->root, LV_DIR_VER);
    lv_obj_add_flag(st->root, LV_OBJ_FLAG_SCROLLABLE);

    // Back button (top-left), routed to the launcher menu like the other apps.
    ::ui::make_back_button_row(st->root, [](lv_event_t*) { ::ui_request_exit_to_menu(); });

    lv_obj_t* title = make_line(st->root, &lv_font_montserrat_24);
    lv_label_set_text(title, "Battery");

    // Big percent readout + the at-a-glance bar.
    st->percent_label = make_line(st->root, &lv_font_montserrat_24);
    lv_label_set_text(st->percent_label, "--%");

    st->bar = lv_bar_create(st->root);
    lv_obj_set_width(st->bar, LV_PCT(80));
    lv_obj_set_height(st->bar, 26);
    lv_bar_set_range(st->bar, 0, 100);
    lv_bar_set_value(st->bar, 0, LV_ANIM_OFF);

    st->status_label = make_line(st->root, &lv_font_montserrat_20);
    lv_label_set_text(st->status_label, "");

    st->tier_label = make_line(st->root, &lv_font_montserrat_14);
    lv_label_set_text(st->tier_label, "");

    st->voltage_label = make_line(st->root, &lv_font_montserrat_20);
    lv_label_set_text(st->voltage_label, "Voltage: n/a");

    st->current_label = make_line(st->root, &lv_font_montserrat_20);
    lv_label_set_text(st->current_label, "Current: n/a");

    st->estimate_label = make_line(st->root, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(st->estimate_label, lv_color_hex(0x808080), 0);
    lv_label_set_text(st->estimate_label, "");

    // Render once now, then refresh every ~2s.
    power_refresh(st);
    st->timer = lv_timer_create(power_tick, 2000, st);
}

void power_exit(void* user_data, lv_obj_t* parent)
{
    (void)parent;
    auto* st = static_cast<PowerAppState*>(user_data);
    if (!st)
    {
        return;
    }
    // Delete the refresh timer first so it can never fire against a freed root.
    if (st->timer)
    {
        lv_timer_del(st->timer);
        st->timer = nullptr;
    }
    if (st->root && lv_obj_is_valid(st->root))
    {
        lv_obj_del(st->root);
    }
    st->root = nullptr;
    st->percent_label = nullptr;
    st->bar = nullptr;
    st->status_label = nullptr;
    st->tier_label = nullptr;
    st->voltage_label = nullptr;
    st->current_label = nullptr;
    st->estimate_label = nullptr;  // owned by (and deleted with) the root tree
}

extern "C"
{
    extern const lv_image_dsc_t Setting;
}

} // namespace

ui::CallbackAppScreen g_power_app("power",
                                  "Battery",
                                  &Setting,
                                  power_enter,
                                  power_exit,
                                  &s_state);

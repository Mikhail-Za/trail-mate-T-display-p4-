// Field Guide: offline survival handbook, foraging/plant guide, and nature reference.
//
// Content lives on the SD card as plain ASCII text (readable with the builtin fonts,
// no glyph dependencies), loaded via the LVGL 'A:' POSIX driver:
//   A:/guides/index.tsv          category \t title \t relative/path.txt
//   A:/guides/<path>.txt         line 1 = title, blank line, body text
//
// Flow: category list -> article list -> scrollable article. Everything works with
// zero connectivity; article text is read into PSRAM per view and released on leave.
//
// Self-contained inline app, same pattern as Flashlight/Node Radar/Translate:
// file-static state, enter()/exit() build and tear down everything, back routes
// through ui_request_exit_to_menu().

#include "lvgl.h"
#include "ui/app_runtime.h"
#include "ui/callback_app_screen.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{

constexpr char kIndexPath[] = "A:/guides/index.tsv";
constexpr char kGuidesDir[] = "A:/guides/";

struct GuideEntry
{
    std::string category;
    std::string title;
    std::string path; // relative to kGuidesDir
};

struct FieldGuideAppState
{
    lv_obj_t* root = nullptr;
    lv_obj_t* title_label = nullptr;
    lv_obj_t* body = nullptr;
    std::vector<GuideEntry> entries;
    std::vector<std::string> categories; // first-seen order
    int cat_idx = -1;
    int entry_idx = -1;
};

FieldGuideAppState s_state;

bool read_text_file(const char* path, std::string& out)
{
    out.clear();
    lv_fs_file_t f;
    if (lv_fs_open(&f, path, LV_FS_MODE_RD) != LV_FS_RES_OK)
    {
        return false;
    }
    char buf[512];
    uint32_t br = 0;
    while (lv_fs_read(&f, buf, sizeof(buf), &br) == LV_FS_RES_OK && br > 0)
    {
        out.append(buf, br);
        if (br < sizeof(buf))
        {
            break;
        }
    }
    lv_fs_close(&f);
    return !out.empty();
}

bool load_index(FieldGuideAppState* st)
{
    std::string text;
    if (!read_text_file(kIndexPath, text))
    {
        return false;
    }
    st->entries.clear();
    st->categories.clear();
    size_t pos = 0;
    while (pos < text.size())
    {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos)
        {
            eol = text.size();
        }
        size_t len = eol - pos;
        if (len > 0 && text[pos + len - 1] == '\r')
        {
            --len;
        }
        if (len > 0)
        {
            const std::string line = text.substr(pos, len);
            const size_t t1 = line.find('\t');
            const size_t t2 = t1 == std::string::npos ? std::string::npos : line.find('\t', t1 + 1);
            if (t2 != std::string::npos)
            {
                GuideEntry e;
                e.category = line.substr(0, t1);
                e.title = line.substr(t1 + 1, t2 - t1 - 1);
                e.path = line.substr(t2 + 1);
                bool seen = false;
                for (const auto& c : st->categories)
                {
                    if (c == e.category)
                    {
                        seen = true;
                        break;
                    }
                }
                if (!seen)
                {
                    st->categories.push_back(e.category);
                }
                st->entries.push_back(std::move(e));
            }
        }
        pos = eol + 1;
    }
    return !st->entries.empty();
}

void show_category_screen(FieldGuideAppState* st);
void show_entry_list_screen(FieldGuideAppState* st);
void show_article_screen(FieldGuideAppState* st);

void clear_body(FieldGuideAppState* st)
{
    if (st->body && lv_obj_is_valid(st->body))
    {
        lv_obj_clean(st->body);
        lv_obj_scroll_to_y(st->body, 0, LV_ANIM_OFF);
    }
}

void set_title(FieldGuideAppState* st, const char* text)
{
    if (st->title_label && lv_obj_is_valid(st->title_label))
    {
        lv_label_set_text(st->title_label, text);
    }
}

lv_obj_t* make_list_button(lv_obj_t* parent, lv_event_cb_t cb, uintptr_t index)
{
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_width(btn, LV_PCT(100));
    lv_obj_set_height(btn, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(btn, 14, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(index));
    return btn;
}

void show_category_screen(FieldGuideAppState* st)
{
    clear_body(st);
    set_title(st, "Field Guide");

    if (st->entries.empty())
    {
        lv_obj_t* msg = lv_label_create(st->body);
        lv_obj_set_width(msg, LV_PCT(100));
        lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_20, 0);
        lv_label_set_text(msg,
                          "No guide data found.\n\n"
                          "Expected A:/guides/index.tsv on the SD card.");
        return;
    }

    for (size_t i = 0; i < st->categories.size(); ++i)
    {
        int count = 0;
        for (const auto& e : st->entries)
        {
            if (e.category == st->categories[i])
            {
                ++count;
            }
        }
        lv_obj_t* btn = make_list_button(
            st->body,
            [](lv_event_t* e)
            {
                auto idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
                s_state.cat_idx = static_cast<int>(idx);
                show_entry_list_screen(&s_state);
            },
            i);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        char text[80];
        std::snprintf(text, sizeof(text), "%s  (%d)", st->categories[i].c_str(), count);
        lv_label_set_text(lbl, text);
        lv_obj_center(lbl);
    }
}

void show_entry_list_screen(FieldGuideAppState* st)
{
    clear_body(st);
    set_title(st, st->categories[st->cat_idx].c_str());

    for (size_t i = 0; i < st->entries.size(); ++i)
    {
        if (st->entries[i].category != st->categories[st->cat_idx])
        {
            continue;
        }
        lv_obj_t* btn = make_list_button(
            st->body,
            [](lv_event_t* e)
            {
                auto idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
                s_state.entry_idx = static_cast<int>(idx);
                show_article_screen(&s_state);
            },
            i);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_obj_set_width(lbl, LV_PCT(100));
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_label_set_text(lbl, st->entries[i].title.c_str());
    }
}

void show_article_screen(FieldGuideAppState* st)
{
    clear_body(st);
    const GuideEntry& entry = st->entries[st->entry_idx];
    set_title(st, entry.category.c_str());

    std::string text;
    const std::string path = std::string(kGuidesDir) + entry.path;
    if (!read_text_file(path.c_str(), text))
    {
        lv_obj_t* msg = lv_label_create(st->body);
        lv_obj_set_width(msg, LV_PCT(100));
        lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_20, 0);
        lv_label_set_text_fmt(msg, "Could not read:\n%s", path.c_str());
        return;
    }

    // Line 1 is the title; the rest is the body.
    size_t first_eol = text.find('\n');
    std::string heading = first_eol == std::string::npos ? entry.title : text.substr(0, first_eol);
    while (!heading.empty() && (heading.back() == '\r' || heading.back() == '\n'))
    {
        heading.pop_back();
    }
    std::string body_text =
        first_eol == std::string::npos ? std::string() : text.substr(first_eol + 1);
    // Trim leading blank lines.
    size_t start = body_text.find_first_not_of("\r\n");
    if (start != std::string::npos)
    {
        body_text.erase(0, start);
    }

    lv_obj_t* head = lv_label_create(st->body);
    lv_obj_set_width(head, LV_PCT(100));
    lv_label_set_long_mode(head, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(head, &lv_font_montserrat_24, 0);
    lv_label_set_text(head, heading.c_str());

    // Photos: convention-based, no index needed. For article <section>/<name>.txt the
    // pipeline stages A:/guides/photos/<section>/<name>/1.jpg .. N.jpg (baseline JPEG,
    // pre-sized to 500px wide so no on-device scaling). Probe and show what exists;
    // decoding happens via the TJPGD decoder when the widget first renders.
    {
        std::string stem = entry.path; // "<section>/<name>.txt"
        const size_t dot = stem.rfind(".txt");
        if (dot != std::string::npos)
        {
            stem.erase(dot);
        }
        for (int i = 1; i <= 6; ++i)
        {
            char photo_path[160];
            std::snprintf(photo_path, sizeof(photo_path), "A:/guides/photos/%s/%d.jpg",
                          stem.c_str(), i);
            lv_fs_file_t probe;
            if (lv_fs_open(&probe, photo_path, LV_FS_MODE_RD) != LV_FS_RES_OK)
            {
                break;
            }
            lv_fs_close(&probe);
            lv_obj_t* img = lv_image_create(st->body);
            lv_image_set_src(img, photo_path);
            lv_obj_set_style_pad_top(img, 6, 0);
        }
    }

    lv_obj_t* body_lbl = lv_label_create(st->body);
    lv_obj_set_width(body_lbl, LV_PCT(100));
    lv_label_set_long_mode(body_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(body_lbl, &lv_font_montserrat_20, 0);
    lv_label_set_text(body_lbl, body_text.c_str());
}

void go_back(FieldGuideAppState* st)
{
    if (st->entry_idx >= 0)
    {
        st->entry_idx = -1;
        show_entry_list_screen(st);
        return;
    }
    if (st->cat_idx >= 0)
    {
        st->cat_idx = -1;
        show_category_screen(st);
        return;
    }
    ::ui_request_exit_to_menu();
}

void field_guide_enter(void* user_data, lv_obj_t* parent)
{
    auto* st = static_cast<FieldGuideAppState*>(user_data);
    if (!st || !parent || (st->root && lv_obj_is_valid(st->root)))
    {
        return;
    }
    st->cat_idx = -1;
    st->entry_idx = -1;

    st->root = lv_obj_create(parent);
    lv_obj_set_size(st->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(st->root, 0, 0);
    lv_obj_set_style_border_width(st->root, 0, 0);
    lv_obj_set_style_radius(st->root, 0, 0);
    lv_obj_clear_flag(st->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(st->root, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* bar = lv_obj_create(st->root);
    lv_obj_set_size(bar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 10, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 12, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* back_btn = lv_button_create(bar);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(
        back_btn, [](lv_event_t*) { go_back(&s_state); }, LV_EVENT_CLICKED, nullptr);

    st->title_label = lv_label_create(bar);
    lv_obj_set_style_text_font(st->title_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(st->title_label, "Field Guide");

    st->body = lv_obj_create(st->root);
    lv_obj_set_width(st->body, LV_PCT(100));
    lv_obj_set_flex_grow(st->body, 1);
    lv_obj_set_style_border_width(st->body, 0, 0);
    lv_obj_set_style_radius(st->body, 0, 0);
    lv_obj_set_style_pad_all(st->body, 12, 0);
    lv_obj_set_style_pad_row(st->body, 10, 0);
    lv_obj_set_flex_flow(st->body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(st->body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(st->body, LV_SCROLLBAR_MODE_AUTO);

    load_index(st);
    show_category_screen(st);
}

void field_guide_exit(void* user_data, lv_obj_t* parent)
{
    (void)parent;
    auto* st = static_cast<FieldGuideAppState*>(user_data);
    if (!st)
    {
        return;
    }
    st->entries.clear();
    st->categories.clear();
    st->cat_idx = -1;
    st->entry_idx = -1;
    if (!st->root || !lv_obj_is_valid(st->root))
    {
        st->root = nullptr;
        st->title_label = nullptr;
        st->body = nullptr;
        return;
    }
    lv_obj_del(st->root);
    st->root = nullptr;
    st->title_label = nullptr;
    st->body = nullptr;
}

extern "C"
{
    extern const lv_image_dsc_t Setting;
}

} // namespace

ui::CallbackAppScreen g_field_guide_app("field_guide",
                                        "Field Guide",
                                        &Setting,
                                        field_guide_enter,
                                        field_guide_exit,
                                        &s_state);

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
#include "src/draw/lv_image_decoder_private.h"     // lv_image_decoder_dsc_t body (.decoded)
#include "src/misc/cache/instance/lv_image_cache.h" // lv_image_cache_drop
#include "ui/app_runtime.h"
#include "ui/callback_app_screen.h"
#include "ui/support/lvgl_fs_utils.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
    // App-owned decoded photo descriptors for the current article view. Each
    // entry's ->data is a separate lv_malloc'd pixel buffer. Kept so the VARIABLE
    // image sources outlive their lv_image objects and are freed together on
    // teardown (see free_photo_dscs).
    std::vector<lv_image_dsc_t*> photo_dscs;
};

FieldGuideAppState s_state;

// Slurp an SD file via the shared ui::fs helper (loops to true EOF; a local
// break-on-short-read would silently truncate an article since the LVGL POSIX
// read is a single read() that can return a partial count mid-file).
bool read_text_file(const char* path, std::string& out)
{
    return ::ui::fs::read_text_file(path, out);
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

// Free the app-owned decoded photo buffers built for the article view. MUST be
// called only AFTER the lv_image objects that referenced them are deleted (every
// caller deletes/cleans the body first). Mirrors map_tiles.cpp's teardown order:
// drop LVGL's image cache so no draw/cache reference survives, then free each
// descriptor's pixel buffer and the descriptor itself. Idempotent: safe on an
// empty vector and on re-entry, so every navigation/exit path may call it.
void free_photo_dscs(FieldGuideAppState* st)
{
    if (st->photo_dscs.empty())
    {
        return;
    }
    lv_image_cache_drop(NULL);
    for (lv_image_dsc_t* dsc : st->photo_dscs)
    {
        if (dsc != nullptr)
        {
            lv_free((void*)dsc->data);
            lv_free(dsc);
        }
    }
    st->photo_dscs.clear();
}

void clear_body(FieldGuideAppState* st)
{
    if (st->body && lv_obj_is_valid(st->body))
    {
        lv_obj_clean(st->body);   // delete photo lv_image objects first
        free_photo_dscs(st);      // then release their owned buffers (no leak/UAF)
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

// Show the photo attribution required by the CC-BY / CC-BY-SA licenses. The staging
// pipeline merges every per-photo credit into A:/guides/photos/CREDITS.tsv as rows
// "<section>/<stem>\t<n>.jpg\t<species>\t<artist>\t<license>\t<url>". We list the
// unique "artist (license)" pairs for this article; without this on-device credit the
// firmware would ship the images in breach of their license terms.
void append_photo_credits(FieldGuideAppState* st, const std::string& stem)
{
    std::string text;
    if (!read_text_file("A:/guides/photos/CREDITS.tsv", text))
    {
        return;
    }
    std::string credit; // "Artist (License); Artist2 (License2)"
    size_t pos = 0;
    while (pos < text.size())
    {
        size_t eol = text.find('\n', pos);
        const size_t line_end = (eol == std::string::npos) ? text.size() : eol;
        const std::string line = text.substr(pos, line_end - pos);
        pos = line_end + 1;
        // Fields: stem, n.jpg, species, artist, license, url
        size_t t0 = line.find('\t');
        if (t0 == std::string::npos || line.compare(0, t0, stem) != 0)
        {
            continue;
        }
        size_t t2 = line.find('\t', line.find('\t', t0 + 1) + 1); // end of species
        if (t2 == std::string::npos)
        {
            continue;
        }
        size_t t3 = line.find('\t', t2 + 1); // end of artist
        if (t3 == std::string::npos)
        {
            continue;
        }
        size_t t4 = line.find('\t', t3 + 1); // end of license
        const std::string artist = line.substr(t2 + 1, t3 - t2 - 1);
        const std::string license =
            line.substr(t3 + 1, (t4 == std::string::npos ? line.size() : t4) - t3 - 1);
        std::string pair = artist + " (" + license + ")";
        if (credit.find(pair) == std::string::npos) // dedup
        {
            if (!credit.empty())
            {
                credit += "; ";
            }
            credit += pair;
        }
    }
    if (credit.empty())
    {
        return;
    }
    std::string full = "Photos: " + credit + " / Wikimedia Commons";
    lv_obj_t* cred = lv_label_create(st->body);
    lv_obj_set_width(cred, LV_PCT(100));
    lv_label_set_long_mode(cred, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(cred, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(cred, lv_color_hex(0x9A9A9A), 0);
    lv_obj_set_style_pad_top(cred, 4, 0);
    lv_label_set_text(cred, full.c_str());
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
    // Strip every remaining CR: source guide files are CRLF, and LVGL has no glyph
    // for \r, so an unstripped body draws a tofu box at the end of every line.
    body_text.erase(std::remove(body_text.begin(), body_text.end(), '\r'), body_text.end());

    lv_obj_t* head = lv_label_create(st->body);
    lv_obj_set_width(head, LV_PCT(100));
    lv_label_set_long_mode(head, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(head, &lv_font_montserrat_24, 0);
    lv_label_set_text(head, heading.c_str());

    // Photos: convention-based, no index needed. For article <section>/<name>.txt the
    // pipeline stages A:/guides/photos/<section>/<name>/1.jpg .. N.jpg (baseline JPEG,
    // pre-sized to 500px wide) numbered CONTIGUOUSLY from 1; probe stops at the first
    // gap. lv_image copies the path string (lv_strdup), so a stack buffer is fine.
    std::string photo_stem = entry.path; // "<section>/<name>.txt"
    const size_t dot = photo_stem.rfind(".txt");
    if (dot != std::string::npos)
    {
        photo_stem.erase(dot);
    }
    int photos_shown = 0;
    for (int i = 1; i <= 6; ++i)
    {
        char photo_path[160];
        std::snprintf(photo_path, sizeof(photo_path), "A:/guides/photos/%s/%d.jpg",
                      photo_stem.c_str(), i);
        if (!::ui::fs::file_exists(photo_path))
        {
            break;
        }
        lv_obj_t* img = lv_image_create(st->body);

        // Decode the JPEG ONCE into an app-owned descriptor so the image source is
        // LV_IMAGE_SRC_VARIABLE (in-memory) rather than LV_IMAGE_SRC_FILE. Only the
        // VARIABLE path is eligible for the P4 hardware PPA blit; a file src stays on
        // the software renderer, which is why photo scroll was slow. This mirrors the
        // decode-to-owned-img_dsc tile cache in map_tiles.cpp exactly: copy the header
        // (w/h/cf/stride/flags/magic) and pixel bytes into buffers we own, tracked in
        // st->photo_dscs and freed together in free_photo_dscs on teardown.
        lv_image_decoder_dsc_t dsc;
        std::memset(&dsc, 0, sizeof(dsc));
        lv_result_t r = lv_image_decoder_open(&dsc, photo_path, NULL);
        bool owned_set = false;
        if (r == LV_RESULT_OK && dsc.decoded != NULL)
        {
            const lv_draw_buf_t* decoded = dsc.decoded;
            lv_image_dsc_t* owned = (lv_image_dsc_t*)lv_malloc(sizeof(lv_image_dsc_t));
            if (owned != nullptr)
            {
                uint8_t* data = (uint8_t*)lv_malloc(decoded->data_size);
                if (data != nullptr)
                {
                    std::memcpy(data, decoded->data, decoded->data_size);
                    owned->header = decoded->header; // w/h/cf/stride/flags/magic
                    owned->data_size = decoded->data_size;
                    owned->data = data;
                    st->photo_dscs.push_back(owned);
                    lv_image_set_src(img, owned); // VARIABLE src -> PPA-eligible
                    owned_set = true;
                }
                else
                {
                    lv_free(owned);
                }
            }
        }
        lv_image_decoder_close(&dsc); // safe after a failed open (dsc memset to 0)
        if (!owned_set)
        {
            // Decode/alloc failed: fall back to the file src (software render, but
            // the photo is never lost). lv_image copies the path via lv_strdup, so
            // the stack buffer is fine.
            lv_image_set_src(img, photo_path);
        }
        lv_obj_set_style_pad_top(img, 6, 0);
        ++photos_shown;
    }
    if (photos_shown > 0)
    {
        append_photo_credits(st, photo_stem);
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
        // Root (and thus every photo lv_image) already deleted elsewhere; still
        // release the owned photo buffers so they never leak across exit.
        free_photo_dscs(st);
        st->root = nullptr;
        st->title_label = nullptr;
        st->body = nullptr;
        return;
    }
    lv_obj_del(st->root);  // deletes body + photo lv_image objects first
    free_photo_dscs(st);   // then free the owned buffers (objects, cache drop, free)
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

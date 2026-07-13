// Translate: offline two-way traveler phrasebook.
//
// Content lives on the SD card (loaded via the LVGL 'A:' POSIX driver, same as map
// tiles), so languages ship preinstalled with zero connectivity:
//   A:/translate/manifest.tsv          code \t english_name \t native_name \t rtl \t font
//   A:/translate/<code>.tsv            category \t id \t english \t translation \t roman
//   A:/translate/fonts/<font>          LVGL binfont, glyph-subset to exactly the content
//
// Flow: language list -> category list (or a flat reverse-mode list in the OTHER
// person's language so they can tap their phrase) -> full-screen card that shows the
// translation in large type to hand the device over. Non-Latin fonts are loaded from
// SD into PSRAM per language (lv_binfont_create) and destroyed on leave, so flash
// carries no glyph cost. Arabic relies on LV_USE_BIDI + LV_USE_ARABIC_PERSIAN_CHARS.
//
// Self-contained inline app, same pattern as Flashlight/Node Radar: file-static
// state, enter()/exit() build and tear down everything, back routes through
// ui_request_exit_to_menu().

#include "lvgl.h"
#include "ui/widgets/back_button.h"
#include "ui/app_runtime.h"
#include "ui/callback_app_screen.h"
#include "ui/support/lvgl_fs_utils.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

constexpr char kManifestPath[] = "A:/translate/manifest.tsv";
constexpr char kContentDir[] = "A:/translate/";
constexpr char kFontDir[] = "A:/translate/fonts/";

struct LangInfo
{
    std::string code;
    std::string english_name;
    std::string native_name;
    bool rtl = false;
    std::string font_file;
};

struct Phrase
{
    std::string id;
    std::string category;
    std::string english;
    std::string translation;
    std::string roman;
};

struct TranslateAppState
{
    lv_obj_t* root = nullptr;
    lv_obj_t* title_label = nullptr;
    lv_obj_t* body = nullptr; // screen content container, rebuilt per screen
    std::vector<LangInfo> langs;
    std::vector<Phrase> phrases;
    std::vector<std::string> categories; // first-seen order
    lv_font_t* font = nullptr;           // SD-loaded binfont for the active language
    bool font_missing = false;           // selected language has non-Latin script but no SD font
    int lang_idx = -1;
    int cat_idx = -1;
    int phrase_idx = -1;
    bool reverse = false; // list in THEIR language; card leads with English
};

TranslateAppState s_state;

// ---- SD helpers -----------------------------------------------------------

// Slurp an SD file. Delegates to the shared ui::fs helper, which loops until a
// zero-length read (true EOF) instead of stopping on the first short read -- the
// LVGL POSIX driver's read is a single read() that can return a partial count
// mid-file, so a local "break on br < bufsize" would silently truncate content.
bool read_text_file(const char* path, std::string& out)
{
    return ::ui::fs::read_text_file(path, out);
}

// Split `text` into lines, then each line into tab-separated fields; calls
// row_cb(fields) for every line with at least `min_fields` fields.
template <typename RowFn>
void for_each_tsv_row(const std::string& text, size_t min_fields, RowFn row_cb)
{
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
            std::vector<std::string> fields;
            size_t fstart = pos;
            const size_t line_end = pos + len;
            while (fstart <= line_end)
            {
                size_t tab = text.find('\t', fstart);
                if (tab == std::string::npos || tab > line_end)
                {
                    tab = line_end;
                }
                fields.emplace_back(text.substr(fstart, tab - fstart));
                if (tab >= line_end)
                {
                    break;
                }
                fstart = tab + 1;
            }
            if (fields.size() >= min_fields)
            {
                row_cb(fields);
            }
        }
        pos = eol + 1;
    }
}

bool load_manifest(TranslateAppState* st)
{
    std::string text;
    if (!read_text_file(kManifestPath, text))
    {
        return false;
    }
    st->langs.clear();
    for_each_tsv_row(text, 5,
                     [st](const std::vector<std::string>& f)
                     {
                         LangInfo li;
                         li.code = f[0];
                         li.english_name = f[1];
                         li.native_name = f[2];
                         li.rtl = (f[3] == "1");
                         li.font_file = f[4];
                         st->langs.push_back(li);
                     });
    return !st->langs.empty();
}

bool load_language(TranslateAppState* st, int idx)
{
    if (idx < 0 || idx >= static_cast<int>(st->langs.size()))
    {
        return false;
    }
    std::string path = std::string(kContentDir) + st->langs[idx].code + ".tsv";
    std::string text;
    if (!read_text_file(path.c_str(), text))
    {
        return false;
    }
    std::vector<Phrase> parsed;
    std::vector<std::string> cats;
    for_each_tsv_row(text, 4,
                     [&parsed, &cats](const std::vector<std::string>& f)
                     {
                         Phrase p;
                         p.id = f[1];
                         p.category = f[0];
                         p.english = f[2];
                         p.translation = f[3];
                         p.roman = f.size() >= 5 ? f[4] : std::string();
                         bool seen = false;
                         for (const auto& c : cats)
                         {
                             if (c == p.category)
                             {
                                 seen = true;
                                 break;
                             }
                         }
                         if (!seen)
                         {
                             cats.push_back(p.category);
                         }
                         parsed.push_back(std::move(p));
                     });

    // A present-but-empty/malformed language file: leave state untouched and fail so the
    // caller stays on the language list rather than committing a half-loaded language.
    if (parsed.empty())
    {
        return false;
    }
    st->phrases = std::move(parsed);
    st->categories = std::move(cats);

    // Load the language's display font from SD (glyphs beyond ASCII live only there).
    if (st->font)
    {
        lv_binfont_destroy(st->font);
        st->font = nullptr;
    }
    std::string font_path = std::string(kFontDir) + st->langs[idx].font_file;
    st->font = lv_binfont_create(font_path.c_str());
    // Latin/Cyrillic-only fallback (montserrat) is legible for Latin scripts, but a
    // missing font for an RTL or CJK/Cyrillic language means the translations render as
    // empty boxes -- flag it so the UI can warn the user instead of failing silently.
    st->font_missing = (st->font == nullptr) && st->langs[idx].rtl;
    if (st->font == nullptr && !st->langs[idx].rtl)
    {
        // Non-Latin non-RTL languages (zh/ja/ko/ru) also need their SD font; treat a
        // missing font as needing a warning for any language whose native name is
        // outside ASCII (i.e. it relies on the SD glyphs).
        for (unsigned char c : st->langs[idx].native_name)
        {
            if (c >= 0x80)
            {
                st->font_missing = true;
                break;
            }
        }
    }

    st->lang_idx = idx;
    return true;
}

// The font every translated string is drawn with; falls back to the builtin font if
// the SD font is missing (Latin text stays legible; CJK would need the SD font).
const lv_font_t* lang_font(const TranslateAppState* st)
{
    return st->font ? st->font : &lv_font_montserrat_24;
}

void apply_lang_text_dir(const TranslateAppState* st, lv_obj_t* label)
{
#if LV_USE_BIDI
    if (st->lang_idx >= 0 && st->langs[st->lang_idx].rtl)
    {
        lv_obj_set_style_base_dir(label, LV_BASE_DIR_RTL, 0);
    }
#else
    (void)st;
    (void)label;
#endif
}

// ---- UI -------------------------------------------------------------------

void show_language_screen(TranslateAppState* st);
void show_category_screen(TranslateAppState* st);
void show_phrase_list_screen(TranslateAppState* st);
void show_card_screen(TranslateAppState* st);

lv_obj_t* make_list_button(lv_obj_t* parent, lv_event_cb_t cb, uintptr_t index)
{
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_width(btn, LV_PCT(100));
    lv_obj_set_height(btn, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(btn, 14, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(index));
    return btn;
}

void clear_body(TranslateAppState* st)
{
    if (st->body && lv_obj_is_valid(st->body))
    {
        lv_obj_clean(st->body);
        lv_obj_scroll_to_y(st->body, 0, LV_ANIM_OFF);
    }
}

void set_title(TranslateAppState* st, const char* text)
{
    if (st->title_label && lv_obj_is_valid(st->title_label))
    {
        lv_label_set_text(st->title_label, text);
    }
}

// Brief, auto-dismissing message floated over the body (e.g. an SD load failure),
// so a tap that can't proceed says why instead of dying silently. FLOATING keeps it
// out of the column flex layout; it self-deletes, and is torn down with the tree on
// exit (LVGL cancels the pending delete anim when the object is deleted first).
void show_toast(TranslateAppState* st, const char* text)
{
    if (!st->root || !lv_obj_is_valid(st->root))
    {
        return;
    }
    lv_obj_t* toast = lv_label_create(st->root);
    lv_obj_add_flag(toast, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_width(toast, LV_PCT(90));
    lv_label_set_long_mode(toast, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_bg_color(toast, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(toast, 8, 0);
    lv_obj_set_style_pad_all(toast, 14, 0);
    lv_obj_set_style_text_color(toast, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(toast, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(toast, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(toast, text);
    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_delete_delayed(toast, 2500);
}

void show_language_screen(TranslateAppState* st)
{
    clear_body(st);
    set_title(st, "Translate");
    st->reverse = false;

    if (st->langs.empty())
    {
        lv_obj_t* msg = lv_label_create(st->body);
        lv_obj_set_width(msg, LV_PCT(100));
        lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_20, 0);
        lv_label_set_text(msg,
                          "No translation data found.\n\n"
                          "Expected A:/translate/manifest.tsv on the SD card.");
        return;
    }

    for (size_t i = 0; i < st->langs.size(); ++i)
    {
        lv_obj_t* btn = make_list_button(
            st->body,
            [](lv_event_t* e)
            {
                auto idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
                if (load_language(&s_state, static_cast<int>(idx)))
                {
                    show_category_screen(&s_state);
                }
                else
                {
                    show_toast(&s_state, "Language data missing on SD card");
                }
            },
            i);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_label_set_text(lbl, st->langs[i].english_name.c_str());
        lv_obj_center(lbl);
    }
}

void show_category_screen(TranslateAppState* st)
{
    clear_body(st);
    // Header: "English name  (native name)" -- the native half needs the SD font.
    set_title(st, st->langs[st->lang_idx].english_name.c_str());

    lv_obj_t* native = lv_label_create(st->body);
    lv_obj_set_width(native, LV_PCT(100));
    lv_obj_set_style_text_font(native, lang_font(st), 0);
    lv_obj_set_style_text_align(native, LV_TEXT_ALIGN_CENTER, 0);
    apply_lang_text_dir(st, native);
    lv_label_set_text(native, st->langs[st->lang_idx].native_name.c_str());

    // If this language needs an SD font that failed to load, its translations would
    // render as blank boxes. Say so plainly rather than fail silently -- this is meant
    // to work when handed to a stranger in an emergency.
    if (st->font_missing)
    {
        lv_obj_t* warn = lv_label_create(st->body);
        lv_obj_set_width(warn, LV_PCT(100));
        lv_label_set_long_mode(warn, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(warn, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(warn, lv_color_hex(0xD08010), 0);
        lv_obj_set_style_text_align(warn, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(warn,
                          LV_SYMBOL_WARNING
                          " Font for this language is missing on the SD card; text may "
                          "show as boxes. Romanization still works.");
    }

    // Hand-over mode: flat list of phrases in THEIR language, for them to browse.
    lv_obj_t* rev_btn = make_list_button(
        st->body,
        [](lv_event_t*)
        {
            s_state.reverse = true;
            s_state.cat_idx = -1;
            show_phrase_list_screen(&s_state);
        },
        0);
    lv_obj_set_style_bg_color(rev_btn, lv_color_hex(0x2F6FD6), 0);
    lv_obj_t* rev_lbl = lv_label_create(rev_btn);
    lv_obj_set_style_text_font(rev_lbl, &lv_font_montserrat_20, 0);
    lv_label_set_text(rev_lbl, LV_SYMBOL_LOOP " Hand device to them");
    lv_obj_center(rev_lbl);

    for (size_t i = 0; i < st->categories.size(); ++i)
    {
        int count = 0;
        for (const auto& p : st->phrases)
        {
            if (p.category == st->categories[i])
            {
                ++count;
            }
        }
        lv_obj_t* btn = make_list_button(
            st->body,
            [](lv_event_t* e)
            {
                auto idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
                s_state.reverse = false;
                s_state.cat_idx = static_cast<int>(idx);
                show_phrase_list_screen(&s_state);
            },
            i);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_set_width(lbl, LV_PCT(100));
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        // Build with std::string so a long category name wraps to fit instead of being
        // silently truncated by a fixed stack buffer.
        std::string text = st->categories[i] + "  (" + std::to_string(count) + ")";
        lv_label_set_text(lbl, text.c_str());
    }
}

void show_phrase_list_screen(TranslateAppState* st)
{
    clear_body(st);
    // Defensive: a forward (non-reverse) phrase list requires a valid category index.
    // If it is somehow stale/unset, fall back to the category screen rather than index
    // st->categories out of bounds.
    if (!st->reverse &&
        (st->cat_idx < 0 || st->cat_idx >= static_cast<int>(st->categories.size())))
    {
        show_category_screen(st);
        return;
    }
    if (st->reverse)
    {
        set_title(st, "Tap your phrase");
    }
    else
    {
        set_title(st, st->categories[st->cat_idx].c_str());
    }

    // In hand-over mode a short instruction in THEIR language sits on top (phrase id
    // c03: "This device translates. Please tap a phrase in your language."). Keyed on
    // the stable id, not the English text, so rewording the phrase can't silently drop it.
    if (st->reverse)
    {
        for (const auto& p : st->phrases)
        {
            if (p.id == "c03")
            {
                lv_obj_t* hint = lv_label_create(st->body);
                lv_obj_set_width(hint, LV_PCT(100));
                lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
                lv_obj_set_style_text_font(hint, lang_font(st), 0);
                lv_obj_set_style_text_color(hint, lv_color_hex(0x9A9A9A), 0);
                apply_lang_text_dir(st, hint);
                lv_label_set_text(hint, p.translation.c_str());
                break;
            }
        }
    }

    for (size_t i = 0; i < st->phrases.size(); ++i)
    {
        const Phrase& p = st->phrases[i];
        if (!st->reverse && p.category != st->categories[st->cat_idx])
        {
            continue;
        }
        // In hand-over mode c03 is already shown as the hint banner above; skip it here
        // so it isn't duplicated as a tappable row.
        if (st->reverse && p.id == "c03")
        {
            continue;
        }
        lv_obj_t* btn = make_list_button(
            st->body,
            [](lv_event_t* e)
            {
                auto idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
                s_state.phrase_idx = static_cast<int>(idx);
                show_card_screen(&s_state);
            },
            i);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_obj_set_width(lbl, LV_PCT(100));
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        if (st->reverse)
        {
            lv_obj_set_style_text_font(lbl, lang_font(st), 0);
            apply_lang_text_dir(st, lbl);
            lv_label_set_text(lbl, p.translation.c_str());
        }
        else
        {
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
            lv_label_set_text(lbl, p.english.c_str());
        }
    }
}

void show_card_screen(TranslateAppState* st)
{
    clear_body(st);
    // Defensive: never index st->phrases out of bounds if phrase_idx is stale/unset.
    if (st->phrase_idx < 0 || st->phrase_idx >= static_cast<int>(st->phrases.size()))
    {
        show_category_screen(st);
        return;
    }
    const Phrase& p = st->phrases[st->phrase_idx];
    set_title(st, st->reverse ? "They said" : st->langs[st->lang_idx].english_name.c_str());

    // Large line: what the OTHER party reads. Forward mode: the translation (hand
    // them the screen). Hand-over mode: the English of the phrase THEY tapped (you
    // read it).
    lv_obj_t* big = lv_label_create(st->body);
    lv_obj_set_width(big, LV_PCT(100));
    lv_label_set_long_mode(big, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(big, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(big, 24, 0);
    if (st->reverse)
    {
        lv_obj_set_style_text_font(big, &lv_font_montserrat_24, 0);
        lv_label_set_text(big, p.english.c_str());
    }
    else
    {
        lv_obj_set_style_text_font(big, lang_font(st), 0);
        apply_lang_text_dir(st, big);
        lv_label_set_text(big, p.translation.c_str());
    }

    // Romanization (pinyin/romaji/etc.), drawn with the SD font because tone marks
    // and macrons are not in the builtin ASCII font.
    if (!p.roman.empty())
    {
        lv_obj_t* roman = lv_label_create(st->body);
        lv_obj_set_width(roman, LV_PCT(100));
        lv_label_set_long_mode(roman, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(roman, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(roman, lang_font(st), 0);
        lv_obj_set_style_text_color(roman, lv_color_hex(0x9A9A9A), 0);
        lv_label_set_text(roman, p.roman.c_str());
    }

    // Small line: the same phrase in the device owner's language (the counterpart of
    // the big line), so both parties see both sides.
    lv_obj_t* small = lv_label_create(st->body);
    lv_obj_set_width(small, LV_PCT(100));
    lv_label_set_long_mode(small, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(small, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(small, lv_color_hex(0x9A9A9A), 0);
    lv_obj_set_style_pad_top(small, 16, 0);
    if (st->reverse)
    {
        lv_obj_set_style_text_font(small, lang_font(st), 0);
        apply_lang_text_dir(st, small);
        lv_label_set_text(small, p.translation.c_str());
    }
    else
    {
        lv_obj_set_style_text_font(small, &lv_font_montserrat_20, 0);
        lv_label_set_text(small, p.english.c_str());
    }
}

// Back navigation across the screen stack: card -> list -> categories -> languages.
void go_back(TranslateAppState* st)
{
    if (st->phrase_idx >= 0)
    {
        st->phrase_idx = -1;
        show_phrase_list_screen(st);
        return;
    }
    if (st->reverse || st->cat_idx >= 0)
    {
        st->reverse = false;
        st->cat_idx = -1;
        show_category_screen(st);
        return;
    }
    if (st->lang_idx >= 0)
    {
        st->lang_idx = -1;
        st->phrases.clear();
        st->categories.clear();
        // Delete the labels that reference the SD font BEFORE destroying it (the
        // language list uses the builtin font). LVGL does not copy a style font, so a
        // live label pointing at a freed font is a use-after-free on the next redraw.
        clear_body(st);
        if (st->font)
        {
            lv_binfont_destroy(st->font);
            st->font = nullptr;
        }
        st->font_missing = false;
        show_language_screen(st);
        return;
    }
    ::ui_request_exit_to_menu();
}

void translate_enter(void* user_data, lv_obj_t* parent)
{
    auto* st = static_cast<TranslateAppState*>(user_data);
    if (!st || !parent || (st->root && lv_obj_is_valid(st->root)))
    {
        return;
    }
    st->lang_idx = -1;
    st->cat_idx = -1;
    st->phrase_idx = -1;
    st->reverse = false;

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
    ::ui::style_back_button(back_btn, back_lbl);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(
        back_btn, [](lv_event_t*) { go_back(&s_state); }, LV_EVENT_CLICKED, nullptr);

    st->title_label = lv_label_create(bar);
    lv_obj_set_style_text_font(st->title_label, &lv_font_montserrat_24, 0);
    // Take the remaining row width and ellipsize, so a long SD-sourced title (e.g. a
    // language/category name) can't overflow the non-wrapping top bar.
    lv_obj_set_flex_grow(st->title_label, 1);
    lv_label_set_long_mode(st->title_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(st->title_label, "Translate");

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

    load_manifest(st);
    show_language_screen(st);
}

void translate_exit(void* user_data, lv_obj_t* parent)
{
    (void)parent;
    auto* st = static_cast<TranslateAppState*>(user_data);
    if (!st)
    {
        return;
    }
    // Delete the widget tree FIRST so no live label still references the SD font, THEN
    // destroy the font (LVGL keeps a raw pointer to a style font; freeing it under a
    // live label is a use-after-free).
    if (st->root && lv_obj_is_valid(st->root))
    {
        lv_obj_del(st->root);
    }
    st->root = nullptr;
    st->title_label = nullptr;
    st->body = nullptr;
    if (st->font)
    {
        lv_binfont_destroy(st->font);
        st->font = nullptr;
    }
    st->font_missing = false;
    st->langs.clear();
    st->phrases.clear();
    st->categories.clear();
    st->lang_idx = -1;
    st->cat_idx = -1;
    st->phrase_idx = -1;
}

extern "C"
{
    extern const lv_image_dsc_t Chat;
}

} // namespace

ui::CallbackAppScreen g_translate_app("translate",
                                      "Translate",
                                      &Chat,
                                      translate_enter,
                                      translate_exit,
                                      &s_state);

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
#include "platform/ui/gps_runtime.h"          // platform::ui::gps::get_data() for "Near me"
#include "ui/app_runtime.h"
#include "ui/callback_app_screen.h"
#include "ui/support/lvgl_fs_utils.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
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
    // App-owned decoded photo descriptors for the current view. Each entry's ->data
    // is a separate lv_malloc'd pixel buffer. Kept so the VARIABLE image sources
    // outlive their lv_image objects and are freed together on teardown (see
    // free_photo_dscs). The photo viewer decodes at most ONE photo at a time, so
    // this vector holds 0 or 1 entry while the viewer is open.
    std::vector<lv_image_dsc_t*> photo_dscs;
    // Photo viewer state. Photos were split out of the article body (rendering a
    // stack of large images in a scroll stutters on this device); they are viewed
    // one-at-a-time on a dedicated screen. photo_idx < 0 means the viewer is not
    // open (we are on the category/list/article level). photo_stem/photo_count are
    // filled in when the article is built and read back when the viewer is shown.
    std::string photo_stem; // "<section>/<name>" for A:/guides/photos/<stem>/<n>.jpg
    int photo_idx = -1;     // 0-based index of the photo on screen; -1 = viewer closed
    int photo_count = 0;    // N contiguous photos for the current article
    // GPS "Near me" regional filter. region_tags maps an article path
    // ("section/file.txt", matches GuideEntry.path) to the region ids it is tagged
    // for (the token "all" is expanded to all 8 ids at load time). Loaded from
    // A:/guides/regions.tsv; a missing file just leaves this empty (no matches).
    std::map<std::string, std::vector<std::string>> region_tags;
    // True while the current screen is the Near-me list, OR an article/photo opened
    // FROM the Near-me list. Selects which parent go_back returns to (Near-me list
    // vs the category/entry-list path) without touching cat_idx/entry_idx meaning.
    bool from_near_me = false;
};

FieldGuideAppState s_state;

// 8 US regions for the GPS "Near me" filter. id matches the tokens in regions.tsv;
// name is the on-screen label; the bbox (lat_min,lat_max,lon_min,lon_max) maps a GPS
// fix to a region. Ids/names/bboxes mirror the shared Field Guide expansion spec.
struct RegionInfo
{
    const char* id;
    const char* name;
    double lat_min;
    double lat_max;
    double lon_min;
    double lon_max;
};

constexpr RegionInfo kRegions[] = {
    {"pnw", "Pacific Northwest", 42.0, 49.0, -125.0, -116.0},
    {"cal", "California", 32.0, 42.0, -124.0, -114.0},
    {"swdesert", "Southwest & Desert", 31.0, 37.0, -115.0, -103.0},
    {"rockies", "Rocky Mountains", 36.0, 49.0, -116.0, -104.0},
    {"plains", "Great Plains", 29.0, 49.0, -104.0, -95.0},
    {"midwest", "Upper Midwest & Lakes", 36.0, 49.0, -97.0, -80.0},
    {"northeast", "Northeast", 39.0, 47.0, -80.0, -67.0},
    {"southeast", "Southeast & Gulf", 24.0, 39.0, -95.0, -75.0},
};
constexpr int kRegionCount = static_cast<int>(sizeof(kRegions) / sizeof(kRegions[0]));

constexpr char kRegionsPath[] = "A:/guides/regions.tsv";

// Slurp an SD file via the shared ui::fs helper (loops to true EOF; a local
// break-on-short-read would silently truncate an article since the LVGL POSIX
// read is a single read() that can return a partial count mid-file).
bool read_text_file(const char* path, std::string& out)
{
    return ::ui::fs::read_text_file(path, out);
}

// Load A:/guides/regions.tsv into st->region_tags. Each row is
// "section/file.txt \t r1,r2,..." where the ids are comma-separated region ids and
// the token "all" expands to all 8 regions. A missing file is fine: region_tags is
// left empty and the "Near me" screen simply finds no matches. Reuses read_text_file.
void load_region_tags(FieldGuideAppState* st)
{
    st->region_tags.clear();
    std::string text;
    if (!read_text_file(kRegionsPath, text))
    {
        return; // no regions.tsv -> feature yields no matches
    }
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
            const size_t tab = line.find('\t');
            if (tab != std::string::npos)
            {
                const std::string path = line.substr(0, tab);
                const std::string ids = line.substr(tab + 1);
                std::vector<std::string> regions;
                size_t rp = 0;
                while (rp <= ids.size())
                {
                    size_t comma = ids.find(',', rp);
                    const size_t rend = (comma == std::string::npos) ? ids.size() : comma;
                    std::string tok = ids.substr(rp, rend - rp);
                    // Trim incidental whitespace around a token.
                    while (!tok.empty() && (tok.front() == ' ' || tok.front() == '\t'))
                    {
                        tok.erase(tok.begin());
                    }
                    while (!tok.empty() && (tok.back() == ' ' || tok.back() == '\t'))
                    {
                        tok.pop_back();
                    }
                    if (tok == "all")
                    {
                        for (int i = 0; i < kRegionCount; ++i)
                        {
                            regions.push_back(kRegions[i].id);
                        }
                    }
                    else if (!tok.empty())
                    {
                        regions.push_back(tok);
                    }
                    if (comma == std::string::npos)
                    {
                        break;
                    }
                    rp = rend + 1;
                }
                if (!path.empty() && !regions.empty())
                {
                    st->region_tags[path] = std::move(regions);
                }
            }
        }
        pos = eol + 1;
    }
}

// Map a GPS lat/lon to one of the 8 regions, returning an index into kRegions.
// Point-in-bbox first; if the point falls inside several bboxes, pick the region
// whose bbox center is nearest; if it falls inside none, pick the region whose
// center is nearest overall. Distance is a plain squared lat/lon delta (adequate to
// rank the coarse CONUS bboxes; no need for great-circle math here).
int region_for_location(double lat, double lon)
{
    int best_contained = -1;
    double best_contained_d = 1e18;
    int best_any = 0;
    double best_any_d = 1e18;
    for (int i = 0; i < kRegionCount; ++i)
    {
        const RegionInfo& r = kRegions[i];
        const double clat = (r.lat_min + r.lat_max) * 0.5;
        const double clon = (r.lon_min + r.lon_max) * 0.5;
        const double dlat = lat - clat;
        const double dlon = lon - clon;
        const double d = dlat * dlat + dlon * dlon;
        if (d < best_any_d)
        {
            best_any_d = d;
            best_any = i;
        }
        const bool inside = (lat >= r.lat_min && lat <= r.lat_max && lon >= r.lon_min &&
                             lon <= r.lon_max);
        if (inside && d < best_contained_d)
        {
            best_contained_d = d;
            best_contained = i;
        }
    }
    return best_contained >= 0 ? best_contained : best_any;
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
    // Region tags load alongside the index (same "index-load time"); tolerant of a
    // missing regions.tsv.
    load_region_tags(st);
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
void show_photo_viewer_screen(FieldGuideAppState* st);
void show_near_me_screen(FieldGuideAppState* st);

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

    // Synthetic FIRST entry: GPS "Near me" regional filter, above the real category
    // buttons. It does NOT set cat_idx; it flips from_near_me so go_back unwinds the
    // Near-me branch (list -> category) distinctly from the category/entry path.
    {
        lv_obj_t* nm_btn = make_list_button(
            st->body,
            [](lv_event_t*)
            {
                s_state.from_near_me = true;
                s_state.entry_idx = -1;
                s_state.photo_idx = -1;
                show_near_me_screen(&s_state);
            },
            0);
        lv_obj_t* nm_lbl = lv_label_create(nm_btn);
        lv_obj_set_width(nm_lbl, LV_PCT(100));
        lv_label_set_long_mode(nm_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(nm_lbl, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_align(nm_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(nm_lbl, LV_SYMBOL_GPS "  Near Me (what's in your area)");
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

// GPS "Near me": read the current fix, map it to a region, and list every article
// whose regions.tsv tags include that region id. Rows open the EXISTING article
// screen (so photos/viewer/credits all work). No GPS fix -> a friendly prompt; no
// tagged matches -> a short note. This screen creates NO photos, so its clear_body
// path stays a no-op for free_photo_dscs. Nav: from_near_me is already true on entry
// (set by the category button); tapping a row keeps it true so go_back returns here.
void show_near_me_screen(FieldGuideAppState* st)
{
    clear_body(st);
    st->photo_idx = -1; // not in the photo viewer on this screen
    set_title(st, "Near Me");

    const gps::GpsState fix = platform::ui::gps::get_data();
    if (!fix.valid)
    {
        lv_obj_t* msg = lv_label_create(st->body);
        lv_obj_set_width(msg, LV_PCT(100));
        lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_20, 0);
        lv_label_set_text(msg,
                          "No GPS fix yet.\n\n"
                          "Go outside or open Map to get a location, then come back.");
        return;
    }

    const RegionInfo& region = kRegions[region_for_location(fix.lat, fix.lng)];

    lv_obj_t* head = lv_label_create(st->body);
    lv_obj_set_width(head, LV_PCT(100));
    lv_label_set_long_mode(head, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(head, &lv_font_montserrat_24, 0);
    char htext[80];
    std::snprintf(htext, sizeof(htext), "Your area: %s", region.name);
    lv_label_set_text(head, htext);

    int matches = 0;
    for (size_t i = 0; i < st->entries.size(); ++i)
    {
        const auto it = st->region_tags.find(st->entries[i].path);
        if (it == st->region_tags.end())
        {
            continue;
        }
        bool tagged = false;
        for (const auto& rid : it->second)
        {
            if (rid == region.id)
            {
                tagged = true;
                break;
            }
        }
        if (!tagged)
        {
            continue;
        }
        // Opens the same article view a category listing would; entry_idx is the
        // global index into st->entries. from_near_me stays true so go_back returns
        // to this Near-me list rather than the category's entry list.
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
        char rowtext[160];
        std::snprintf(rowtext, sizeof(rowtext), "%s  (%s)", st->entries[i].title.c_str(),
                      st->entries[i].category.c_str());
        lv_label_set_text(lbl, rowtext);
        ++matches;
    }

    if (matches == 0)
    {
        lv_obj_t* none = lv_label_create(st->body);
        lv_obj_set_width(none, LV_PCT(100));
        lv_label_set_long_mode(none, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(none, &lv_font_montserrat_20, 0);
        lv_label_set_text(none, "No tagged articles for this area yet.");
    }
}

// Show the photo attribution required by the CC-BY / CC-BY-SA licenses for the ONE
// photo currently on screen in the viewer. The staging pipeline merges every
// per-photo credit into A:/guides/photos/CREDITS.tsv as rows
// "<section>/<stem>\t<n>.jpg\t<species>\t<artist>\t<license>\t<url>". We render the
// credit for photo <photo_num> ("Photo: Artist (License) / Wikimedia Commons"); if
// that exact per-photo row is missing we fall back to the article-wide list of unique
// "artist (license)" pairs. Without this on-device credit the firmware would ship the
// image in breach of its license terms.
void append_photo_credits(FieldGuideAppState* st, const std::string& stem, int photo_num)
{
    std::string text;
    if (!read_text_file("A:/guides/photos/CREDITS.tsv", text))
    {
        return;
    }
    char want[24];
    std::snprintf(want, sizeof(want), "%d.jpg", photo_num);
    std::string credit;     // exact per-photo "Artist (License)"
    std::string all_credit; // article-wide fallback "Artist (License); Artist2 (...)"
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
        size_t t1 = line.find('\t', t0 + 1); // end of n.jpg
        if (t1 == std::string::npos)
        {
            continue;
        }
        size_t t2 = line.find('\t', t1 + 1); // end of species
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
        const std::string photo_field = line.substr(t0 + 1, t1 - t0 - 1);
        const std::string artist = line.substr(t2 + 1, t3 - t2 - 1);
        const std::string license =
            line.substr(t3 + 1, (t4 == std::string::npos ? line.size() : t4) - t3 - 1);
        std::string pair = artist + " (" + license + ")";
        if (photo_field == want) // exact photo match wins
        {
            credit = pair;
        }
        if (all_credit.find(pair) == std::string::npos) // dedup for fallback
        {
            if (!all_credit.empty())
            {
                all_credit += "; ";
            }
            all_credit += pair;
        }
    }
    std::string full;
    if (!credit.empty())
    {
        full = "Photo: " + credit + " / Wikimedia Commons";
    }
    else if (!all_credit.empty())
    {
        full = "Photos: " + all_credit + " / Wikimedia Commons";
    }
    else
    {
        return;
    }
    lv_obj_t* cred = lv_label_create(st->body);
    lv_obj_set_width(cred, LV_PCT(100));
    lv_label_set_long_mode(cred, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(cred, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(cred, lv_color_hex(0x9A9A9A), 0);
    lv_obj_set_style_pad_top(cred, 4, 0);
    lv_label_set_text(cred, full.c_str());
}

// Count the article's photos: probe A:/guides/photos/<stem>/1.jpg .. N.jpg (baseline
// JPEG, pre-sized to 500px wide) numbered CONTIGUOUSLY from 1; stop at the first gap.
// Same probe the article body used to run inline; now it only yields N (how many
// photos exist) so the article can offer a "View Photos (N)" button without decoding
// anything. Capped at 6 to match the staging convention.
int count_photos(const std::string& stem)
{
    int n = 0;
    for (int i = 1; i <= 6; ++i)
    {
        char photo_path[160];
        std::snprintf(photo_path, sizeof(photo_path), "A:/guides/photos/%s/%d.jpg",
                      stem.c_str(), i);
        if (!::ui::fs::file_exists(photo_path))
        {
            break;
        }
        ++n;
    }
    return n;
}

// Article view = TEXT ONLY. Rendering a stack of large inline photos in a scroll
// stutters badly on this device, so the photos were split onto a dedicated one-at-a-
// time viewer (show_photo_viewer_screen). Here we render heading + body text only
// (zero decoded photos, so the scroll is smooth) and, if the article has photos, a
// "View Photos (N)" button just under the heading that opens the viewer at photo 0.
void show_article_screen(FieldGuideAppState* st)
{
    clear_body(st);
    st->photo_idx = -1; // on the article level, the viewer is closed
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

    // Derive the photo stem ("<section>/<name>") and count how many photos exist. The
    // article decodes NONE of them; it only offers a button into the viewer.
    std::string photo_stem = entry.path; // "<section>/<name>.txt"
    const size_t dot = photo_stem.rfind(".txt");
    if (dot != std::string::npos)
    {
        photo_stem.erase(dot);
    }
    st->photo_stem = photo_stem;
    st->photo_count = count_photos(photo_stem);

    if (st->photo_count > 0)
    {
        // "View Photos (N)" button, app list-button style, placed prominently just
        // under the heading. Opens the one-at-a-time viewer at photo 0. The handler
        // reads photo_stem/photo_count straight from state (set just above).
        lv_obj_t* photos_btn = make_list_button(
            st->body,
            [](lv_event_t*)
            {
                s_state.photo_idx = 0;
                show_photo_viewer_screen(&s_state);
            },
            0);
        lv_obj_t* pblbl = lv_label_create(photos_btn);
        lv_obj_set_style_text_font(pblbl, &lv_font_montserrat_20, 0);
        char btext[48];
        std::snprintf(btext, sizeof(btext), LV_SYMBOL_IMAGE "  View Photos (%d)",
                      st->photo_count);
        lv_label_set_text(pblbl, btext);
        lv_obj_center(pblbl);
    }

    lv_obj_t* body_lbl = lv_label_create(st->body);
    lv_obj_set_width(body_lbl, LV_PCT(100));
    lv_label_set_long_mode(body_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(body_lbl, &lv_font_montserrat_20, 0);
    lv_label_set_text(body_lbl, body_text.c_str());
}

// Photo viewer: ONE photo on screen at a time. Only the current photo is ever decoded
// (into an app-owned VARIABLE descriptor -> PPA-eligible), and there is no scrolling
// stack of images, so it stays smooth where the old inline-in-article render did not.
// Every rebuild goes through clear_body -> free_photo_dscs, so the single decoded
// photo is released on every Prev/Next/Back/exit (no leak, no use-after-free).
void show_photo_viewer_screen(FieldGuideAppState* st)
{
    clear_body(st);
    if (st->entry_idx < 0 || st->photo_count <= 0)
    {
        // Nothing to show (shouldn't happen: the button only exists when N > 0).
        // Fall back to the text-only article, which is cheap to rebuild.
        st->photo_idx = -1;
        show_article_screen(st);
        return;
    }
    const GuideEntry& entry = st->entries[st->entry_idx];
    set_title(st, entry.title.c_str());

    const int n = st->photo_count;
    int idx = st->photo_idx;
    if (idx < 0)
    {
        idx = 0;
    }
    if (idx > n - 1)
    {
        idx = n - 1;
    }
    st->photo_idx = idx;

    // "Photo i / N" position label.
    lv_obj_t* counter = lv_label_create(st->body);
    lv_obj_set_width(counter, LV_PCT(100));
    lv_obj_set_style_text_font(counter, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(counter, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(counter, "Photo %d / %d", idx + 1, n);

    // Decode ONLY the current photo into an app-owned descriptor so the image source
    // is LV_IMAGE_SRC_VARIABLE (in-memory) rather than LV_IMAGE_SRC_FILE. Only the
    // VARIABLE path is eligible for the P4 hardware PPA blit; a file src stays on the
    // software renderer. Same decode-to-owned code the article used inline, mirroring
    // map_tiles.cpp: copy the header (w/h/cf/stride/flags/magic) and pixel bytes into
    // buffers we own, tracked in st->photo_dscs and freed by free_photo_dscs.
    char photo_path[160];
    std::snprintf(photo_path, sizeof(photo_path), "A:/guides/photos/%s/%d.jpg",
                  st->photo_stem.c_str(), idx + 1);
    lv_obj_t* img = lv_image_create(st->body);
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
        // Decode/alloc failed: fall back to the file src (software render, but the
        // photo is never lost). lv_image copies the path via lv_strdup.
        lv_image_set_src(img, photo_path);
    }
    // Full-width box, image centered within it.
    lv_obj_set_width(img, LV_PCT(100));
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CENTER);
    lv_obj_set_style_pad_top(img, 6, 0);

    // Prev / Next controls. Prev is disabled on the first photo, Next on the last.
    lv_obj_t* nav = lv_obj_create(st->body);
    lv_obj_set_width(nav, LV_PCT(100));
    lv_obj_set_height(nav, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(nav, 0, 0);
    lv_obj_set_style_radius(nav, 0, 0);
    lv_obj_set_style_pad_all(nav, 0, 0);
    lv_obj_set_style_pad_top(nav, 8, 0);
    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(nav, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(nav, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* prev_btn = lv_button_create(nav);
    lv_obj_set_style_pad_all(prev_btn, 14, 0);
    lv_obj_t* prev_lbl = lv_label_create(prev_btn);
    lv_obj_set_style_text_font(prev_lbl, &lv_font_montserrat_20, 0);
    lv_label_set_text(prev_lbl, LV_SYMBOL_LEFT " Prev");
    lv_obj_center(prev_lbl);
    lv_obj_add_event_cb(
        prev_btn,
        [](lv_event_t*)
        {
            if (s_state.photo_idx > 0)
            {
                --s_state.photo_idx;
                show_photo_viewer_screen(&s_state); // frees old photo, decodes new one
            }
        },
        LV_EVENT_CLICKED, nullptr);
    if (idx <= 0)
    {
        lv_obj_add_state(prev_btn, LV_STATE_DISABLED);
    }

    lv_obj_t* next_btn = lv_button_create(nav);
    lv_obj_set_style_pad_all(next_btn, 14, 0);
    lv_obj_t* next_lbl = lv_label_create(next_btn);
    lv_obj_set_style_text_font(next_lbl, &lv_font_montserrat_20, 0);
    lv_label_set_text(next_lbl, "Next " LV_SYMBOL_RIGHT);
    lv_obj_center(next_lbl);
    lv_obj_add_event_cb(
        next_btn,
        [](lv_event_t*)
        {
            if (s_state.photo_idx < s_state.photo_count - 1)
            {
                ++s_state.photo_idx;
                show_photo_viewer_screen(&s_state); // frees old photo, decodes new one
            }
        },
        LV_EVENT_CLICKED, nullptr);
    if (idx >= n - 1)
    {
        lv_obj_add_state(next_btn, LV_STATE_DISABLED);
    }

    // CC attribution for the photo currently on screen.
    append_photo_credits(st, st->photo_stem, idx + 1);
}

void go_back(FieldGuideAppState* st)
{
    if (st->photo_idx >= 0)
    {
        // In the photo viewer: return to the SAME article (text-only, cheap to
        // rebuild), not to the article list. entry_idx is unchanged.
        st->photo_idx = -1;
        show_article_screen(st);
        return;
    }
    if (st->entry_idx >= 0)
    {
        // Article -> its parent list. An article opened from Near me returns to the
        // Near-me list; otherwise to the category's entry list (unchanged path).
        st->entry_idx = -1;
        if (st->from_near_me)
        {
            show_near_me_screen(st);
        }
        else
        {
            show_entry_list_screen(st);
        }
        return;
    }
    if (st->from_near_me)
    {
        // Near-me list -> category screen (leave the Near-me branch).
        st->from_near_me = false;
        show_category_screen(st);
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
    st->photo_idx = -1;
    st->photo_count = 0;
    st->photo_stem.clear();
    st->from_near_me = false;

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
    st->region_tags.clear();
    st->cat_idx = -1;
    st->entry_idx = -1;
    st->photo_idx = -1;
    st->photo_count = 0;
    st->photo_stem.clear();
    st->from_near_me = false;
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

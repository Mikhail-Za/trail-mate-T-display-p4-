"""Execute production caption/viewer bodies against observable LVGL/SD substitutes."""

from pathlib import Path
import subprocess
import tempfile
import resource

resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
source = Path("apps/esp32_lvgl/src/esp32_lvgl_field_guide_app.cpp").read_text()
start = source.index("append_photo_caption(FieldGuideAppState*")
start = source.rfind("\n", 0, start) + 1
caption = source[start : source.index("// Count the article", start)]
start = source.index("void show_photo_viewer_screen(FieldGuideAppState* st)\n{")
viewer = source[start : source.index("void go_back(", start)]
stubs = r"""
#include <cstdint>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <iostream>
#include <algorithm>
struct lv_obj_t {std::string text; unsigned color=0;};
struct lv_event_t {};
struct lv_image_dsc_t {int header; unsigned data_size; const unsigned char* data;};
struct lv_draw_buf_t {int header; unsigned data_size; const unsigned char* data;};
struct lv_image_decoder_dsc_t {const lv_draw_buf_t* decoded;};
using lv_result_t=int;
struct GuideEntry {std::string title="Edible article";};
struct FieldGuideAppState {lv_obj_t* body=nullptr; int entry_idx=0,photo_count=2,photo_idx=0; std::string photo_stem="plants/yarrow"; std::vector<GuideEntry> entries{GuideEntry{}};std::vector<lv_image_dsc_t*> photo_dscs;};
FieldGuideAppState s_state;
std::vector<lv_obj_t*> objects;
std::string credits;
bool readable=true;
int images=0,opens=0,attributions=0;
constexpr int lv_font_montserrat_14=14,lv_font_montserrat_20=20;
constexpr int LV_LABEL_LONG_WRAP=0,LV_TEXT_ALIGN_CENTER=0,LV_RESULT_OK=0,LV_IMAGE_ALIGN_CENTER=0,LV_SIZE_CONTENT=0,LV_FLEX_FLOW_ROW=0,LV_FLEX_ALIGN_SPACE_BETWEEN=0,LV_FLEX_ALIGN_CENTER=0,LV_OBJ_FLAG_SCROLLABLE=0,LV_EVENT_CLICKED=0,LV_STATE_DISABLED=0;
#define LV_PCT(x) x
#define LV_SYMBOL_LEFT "<"
#define LV_SYMBOL_RIGHT ">"
bool read_text_file(const char*,std::string& out){out=credits; return readable;}
unsigned lv_color_hex(unsigned c){return c;}
lv_obj_t* lv_obj_create(lv_obj_t*){auto* p=new lv_obj_t;objects.push_back(p);return p;}
lv_obj_t* lv_label_create(lv_obj_t* p){return lv_obj_create(p);}
lv_obj_t* lv_button_create(lv_obj_t* p){return lv_obj_create(p);}
lv_obj_t* lv_image_create(lv_obj_t* p){++images;return lv_obj_create(p);}
void lv_label_set_text(lv_obj_t* p,const char* s){p->text=s;}
template<class... A> void lv_label_set_text_fmt(lv_obj_t* p,const char* f,A... a){char b[256];std::snprintf(b,sizeof b,f,a...);p->text=b;}
void lv_obj_set_style_text_color(lv_obj_t* p,unsigned c,int){p->color=c;}
#define NOOP(name) template<class... A> void name(A...){ }
NOOP(lv_obj_set_width) NOOP(lv_label_set_long_mode) NOOP(lv_obj_set_style_text_font)
NOOP(lv_obj_set_style_text_align) NOOP(lv_obj_set_style_pad_top) NOOP(lv_image_set_src)
NOOP(lv_image_set_inner_align) NOOP(lv_obj_set_height) NOOP(lv_obj_set_style_border_width)
NOOP(lv_obj_set_style_radius) NOOP(lv_obj_set_style_pad_all) NOOP(lv_obj_set_flex_flow)
NOOP(lv_obj_set_flex_align) NOOP(lv_obj_clear_flag) NOOP(lv_obj_center) NOOP(lv_obj_add_event_cb)
NOOP(lv_obj_add_state) NOOP(set_title) NOOP(show_article_screen)
void* lv_malloc(size_t n){return std::malloc(n);} void lv_free(void* p){std::free(p);}
lv_result_t lv_image_decoder_open(lv_image_decoder_dsc_t*,const char*,void*){++opens;return 1;}
NOOP(lv_image_decoder_close)
void clear_body(FieldGuideAppState* st){for(auto p:objects)delete p;objects.clear();for(auto p:st->photo_dscs){std::free((void*)p->data);std::free(p);}st->photo_dscs.clear();images=opens=attributions=0;}
void append_photo_credits(FieldGuideAppState*,const std::string&,int){++attributions;}
"""
tests = r"""
std::string row(std::string species,std::string photo="1.jpg"){return "plants/yarrow\t"+photo+"\t"+species+"\tArtist\tCC-BY\thttps://example.test/photo\n";}
bool label(std::string text,unsigned color=0){for(auto p:objects)if(p->text==text && (!color || p->color==color))return true;return false;}
void check(bool display){show_photo_viewer_screen(&s_state);assert(images==(display?1:0));assert(opens==(display?1:0));assert(label("< Prev"));assert(label("Next >"));if(!display){bool message=false;for(auto p:objects)if(p->text.find("identification")!=std::string::npos)message=true;assert(message);}}
int main(){
 credits=row("Yarrow"); check(true); assert(label("Yarrow",0xFFFFFF));assert(attributions==1);
 credits=row("Poison hemlock - DEADLY lookalike");check(true);assert(label("Poison hemlock - DEADLY lookalike",0xE04030));
 readable=false;check(false);assert(!label("Poison hemlock - DEADLY lookalike"));readable=true;
 credits="";check(false);
 credits=row("Yarrow","2.jpg");check(false);
 credits=row("");check(false);
 credits=row("   ");check(false);
 credits="plants/yarrow\t1.jpg\tPoison hem";check(false);
 credits=row("Yarrow")+"plants/yarrow\t1.jpg\tPoison hem";check(false);
 credits="plants/yarrow\t1.jpg";check(false);
 credits=row("Yarrow")+"plants/yarrow\t1.jpg";check(false);
 credits=row(std::string("Yarrow\0Poison",13));check(false);
 credits=row("Yarrow\v");check(false);
 credits=row("Yarrow");credits.back()='\t';credits+="extra\n";check(false);
 credits=row("Yarrow");credits.pop_back();check(true);assert(label("Yarrow"));
 credits="plants/yarrow\t1.jpg\tYarrow\tArtist\tCC-BY\t";check(false);
 credits=row("Yarrow")+row("Poison hemlock");check(false);
 credits=row("Yarrow");s_state.photo_idx=0;check(true);s_state.photo_idx=1;check(false);assert(!label("Yarrow"));s_state.photo_idx=0;check(true);
 credits=row("Yarrow")+row("Poison hemlock","2.jpg");s_state.photo_idx=1;check(true);assert(label("Poison hemlock",0xE04030));assert(!label("Yarrow"));
 credits="plants/yarrow\t2.jpg\tPoison hemlock\tArtist\tCC-BY\thttps://example.test/photo\r\n";check(true);assert(label("Poison hemlock"));
 clear_body(&s_state);std::cout<<"photo safety scenarios PASS\n";
}
"""
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "photo.cpp"
    exe = Path(tmp) / "photo"
    cpp.write_text(stubs + caption + viewer + tests)
    subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fuse-ld=bfd",
            str(cpp),
            "-o",
            str(exe),
        ],
        check=True,
    )
    subprocess.run([str(exe)], check=True)

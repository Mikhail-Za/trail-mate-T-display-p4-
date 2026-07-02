#!/usr/bin/env python3
"""
Assemble the offline Translate + Field Guide SD content into the staging tree.

Inputs (this directory):
  phrases_master.tsv            category \t id \t english
  translations/<code>.tsv       id \t translation \t roman
  guides/<section>/*.txt        line 1 = title, blank line, ASCII body

Outputs (staging root, default C:\\osm-tiles\\sd-staging):
  translate/manifest.tsv        code \t english \t native \t rtl \t font
  translate/<code>.tsv          category \t id \t english \t translation \t roman
  translate/fonts/<name>.bin    LVGL binfont, glyph-subset to exactly the used text
  guides/index.tsv              category \t title \t relative/path.txt
  guides/<section>/*.txt        copied verbatim (ASCII-validated)

Fonts: downloads Noto/DejaVu TTFs into fonts_cache/ on first run, then subsets with
lv_font_conv (via npx). Arabic uses DejaVu Sans because LVGL's Arabic shaper needs
Presentation Forms-B glyphs reachable through the cmap, which Noto Arabic does not
provide; DejaVu does.
"""
import os
import subprocess
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
STAGING = sys.argv[1] if len(sys.argv) > 1 else r"C:\osm-tiles\sd-staging"
FONT_CACHE = os.path.join(HERE, "fonts_cache")
FONT_SIZE = 30
FONT_BPP = 2

# code, english, native, rtl, font group
LANGS = [
    ("es", "Spanish", "Español", 0, "latin"),
    ("fr", "French", "Français", 0, "latin"),
    ("de", "German", "Deutsch", 0, "latin"),
    ("it", "Italian", "Italiano", 0, "latin"),
    ("pt", "Portuguese", "Português", 0, "latin"),
    ("zh", "Chinese", "中文", 0, "zh"),
    ("ja", "Japanese", "日本語", 0, "ja"),
    ("ko", "Korean", "한국어", 0, "ko"),
    ("ru", "Russian", "Русский", 0, "cyr"),
    ("ar", "Arabic", "العربية", 1, "ar"),
    ("vi", "Vietnamese", "Tiếng Việt", 0, "latin"),
    ("tr", "Turkish", "Türkçe", 0, "latin"),
]

FONT_SOURCES = {
    "latin": ("NotoSans-Regular.ttf",
              "https://raw.githubusercontent.com/googlefonts/noto-fonts/main/hinted/ttf/NotoSans/NotoSans-Regular.ttf"),
    "cyr": ("NotoSans-Regular.ttf",
            "https://raw.githubusercontent.com/googlefonts/noto-fonts/main/hinted/ttf/NotoSans/NotoSans-Regular.ttf"),
    "zh": ("NotoSansCJKsc-Regular.otf",
           "https://raw.githubusercontent.com/googlefonts/noto-cjk/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf"),
    "ja": ("NotoSansCJKjp-Regular.otf",
           "https://raw.githubusercontent.com/googlefonts/noto-cjk/main/Sans/OTF/Japanese/NotoSansCJKjp-Regular.otf"),
    "ko": ("NotoSansCJKkr-Regular.otf",
           "https://raw.githubusercontent.com/googlefonts/noto-cjk/main/Sans/OTF/Korean/NotoSansCJKkr-Regular.otf"),
    # DejaVu ships as a release zip; handled specially in fetch_font().
    "ar": ("DejaVuSans.ttf",
           "https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.zip"),
}

GUIDE_SECTIONS = [
    ("survival", "Survival"),
    ("water_firstaid", "Water & First Aid"),
    ("plants_edible", "Edible Plants"),
    ("plants_hazard", "Plant Hazards"),
    ("nature", "Wildlife & Nature"),
    ("preparedness", "Preparedness"),
]

# Arabic Presentation Forms-B block: LVGL's shaper substitutes into these at render
# time, so the WHOLE block must be in the subset regardless of which letters appear.
ARABIC_EXTRA_RANGE = "0x600-0x6FF,0xFE70-0xFEFF"


def read_tsv(path, min_fields):
    rows = []
    with open(path, encoding="utf-8", newline="") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if not line:
                continue
            fields = line.split("\t")
            if len(fields) < min_fields:
                fields += [""] * (min_fields - len(fields))
            rows.append(fields)
    return rows


def main():
    master = read_tsv(os.path.join(HERE, "phrases_master.tsv"), 3)
    master_ids = [r[1] for r in master]
    print("master: %d phrases, %d categories"
          % (len(master), len({r[0] for r in master})))

    tdir = os.path.join(STAGING, "translate")
    fdir = os.path.join(tdir, "fonts")
    os.makedirs(fdir, exist_ok=True)

    # --- merge translations + collect per-font-group text ---------------------
    group_text = {g: set() for g in FONT_SOURCES}
    manifest_lines = []
    for code, eng, native, rtl, group in LANGS:
        rows = read_tsv(os.path.join(HERE, "translations", code + ".tsv"), 2)
        by_id = {r[0]: (r[1], r[2] if len(r) > 2 else "") for r in rows}
        missing = [i for i in master_ids if i not in by_id or not by_id[i][0]]
        if missing:
            sys.exit("FATAL %s: missing/empty translations for %s" % (code, missing[:5]))
        out_path = os.path.join(tdir, code + ".tsv")
        with open(out_path, "w", encoding="utf-8", newline="\n") as f:
            for cat, pid, english in master:
                tr, roman = by_id[pid]
                f.write("%s\t%s\t%s\t%s\t%s\n" % (cat, pid, english, tr, roman))
                group_text[group].update(tr)
                group_text[group].update(roman)
        group_text[group].update(native)
        font_file = group + str(FONT_SIZE) + ".bin"
        manifest_lines.append("%s\t%s\t%s\t%d\t%s" % (code, eng, native, rtl, font_file))
        print("  %s: %d phrases -> %s" % (code, len(master), out_path))

    with open(os.path.join(tdir, "manifest.tsv"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(manifest_lines) + "\n")

    # --- fonts -----------------------------------------------------------------
    os.makedirs(FONT_CACHE, exist_ok=True)
    for group, (fname, url) in FONT_SOURCES.items():
        cache = os.path.join(FONT_CACHE, fname)
        if not os.path.exists(cache):
            print("downloading %s ..." % fname)
            if url.endswith(".zip"):
                import io
                import zipfile
                with urllib.request.urlopen(url) as resp:
                    data = resp.read()
                with zipfile.ZipFile(io.BytesIO(data)) as zf:
                    member = next(n for n in zf.namelist() if n.endswith("/" + fname))
                    with zf.open(member) as src, open(cache, "wb") as dst:
                        dst.write(src.read())
            else:
                urllib.request.urlretrieve(url, cache)
        chars = group_text[group]
        # Always include printable ASCII so mixed lines (digits, punctuation) render.
        chars.update(chr(c) for c in range(0x20, 0x7F))
        chars.discard("\t")
        chars.discard("\n")
        symbols = "".join(sorted(chars))
        out_bin = os.path.join(fdir, group + str(FONT_SIZE) + ".bin")
        # Invoke node on the lv_font_conv entry directly (no cmd.exe shell: the symbols
        # string contains <, >, & and | which the shell would mangle).
        npm_root = subprocess.run(["npm.cmd", "root", "-g"], capture_output=True,
                                  text=True).stdout.strip()
        conv_js = os.path.join(npm_root, "lv_font_conv", "lv_font_conv.js")
        cmd = ["node", conv_js,
               "--font", cache,
               "--size", str(FONT_SIZE),
               "--bpp", str(FONT_BPP),
               "--format", "bin",
               "--no-kerning",
               "-o", out_bin,
               "--symbols", symbols]
        if group == "ar":
            cmd += ["--range", ARABIC_EXTRA_RANGE]
        print("subsetting %s (%d unique chars)..." % (out_bin, len(chars)))
        res = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8")
        if res.returncode != 0 or not os.path.exists(out_bin):
            sys.exit("FATAL lv_font_conv failed for %s:\n%s\n%s"
                     % (group, res.stdout[-2000:], res.stderr[-2000:]))
        print("  -> %s (%.1f KB)" % (out_bin, os.path.getsize(out_bin) / 1024.0))

    # --- guides ------------------------------------------------------------------
    gdir = os.path.join(STAGING, "guides")
    os.makedirs(gdir, exist_ok=True)
    index_lines = []
    total = 0
    for section, display in GUIDE_SECTIONS:
        src = os.path.join(HERE, "guides", section)
        files = sorted(f for f in os.listdir(src) if f.endswith(".txt"))
        os.makedirs(os.path.join(gdir, section), exist_ok=True)
        for fn in files:
            with open(os.path.join(src, fn), "rb") as f:
                raw = f.read()
            bad = [b for b in raw if b > 127]
            if bad:
                sys.exit("FATAL non-ASCII byte in %s/%s" % (section, fn))
            text = raw.decode("ascii")
            title = text.split("\n", 1)[0].strip()
            if not title:
                sys.exit("FATAL empty title in %s/%s" % (section, fn))
            with open(os.path.join(gdir, section, fn), "w", encoding="ascii",
                      newline="\n") as f:
                f.write(text)
            index_lines.append("%s\t%s\t%s/%s" % (display, title, section, fn))
            total += 1
        print("  guides/%s: %d articles" % (section, len(files)))
    with open(os.path.join(gdir, "index.tsv"), "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(index_lines) + "\n")
    print("guides: %d articles indexed" % total)

    # --- plant photos ------------------------------------------------------------
    # photos_raw/<section>/<article-stem>/{N.jpg|N.png, credits.tsv} -> staged as
    # guides/photos/<section>/<stem>/N.jpg, re-encoded to 500px-wide BASELINE JPEG
    # (TJPGD on-device cannot decode progressive). Credits merge into one file.
    raw_root = os.path.join(HERE, "photos_raw")
    if os.path.isdir(raw_root):
        from PIL import Image, ImageOps
        photo_total = 0
        credit_lines = []
        for section in sorted(os.listdir(raw_root)):
            sdir = os.path.join(raw_root, section)
            if not os.path.isdir(sdir):
                continue
            for stem in sorted(os.listdir(sdir)):
                adir = os.path.join(sdir, stem)
                if not os.path.isdir(adir):
                    continue
                out_dir = os.path.join(gdir, "photos", section, stem)
                n_out = 0
                for fn in sorted(os.listdir(adir)):
                    base, ext = os.path.splitext(fn)
                    if ext.lower() not in (".jpg", ".jpeg", ".png") or not base.isdigit():
                        continue
                    img = Image.open(os.path.join(adir, fn))
                    img = ImageOps.exif_transpose(img).convert("RGB")
                    if img.width > 500:
                        img = img.resize((500, max(1, round(img.height * 500 / img.width))),
                                         Image.LANCZOS)
                    os.makedirs(out_dir, exist_ok=True)
                    out_path = os.path.join(out_dir, base + ".jpg")
                    img.save(out_path, "JPEG", quality=80, progressive=False,
                             optimize=True)
                    n_out += 1
                    photo_total += 1
                cred = os.path.join(adir, "credits.tsv")
                if os.path.exists(cred):
                    with open(cred, encoding="utf-8") as f:
                        for line in f:
                            line = line.strip()
                            if line:
                                credit_lines.append("%s/%s\t%s" % (section, stem, line))
                if n_out:
                    print("  photos %s/%s: %d" % (section, stem, n_out))
        if credit_lines:
            with open(os.path.join(gdir, "photos", "CREDITS.tsv"), "w",
                      encoding="utf-8", newline="\n") as f:
                f.write("\n".join(credit_lines) + "\n")
        print("photos: %d staged (500px baseline JPEG q80)" % photo_total)
    else:
        print("photos: photos_raw/ not present, skipped")

    print("STAGING_DONE %s" % STAGING)


if __name__ == "__main__":
    main()

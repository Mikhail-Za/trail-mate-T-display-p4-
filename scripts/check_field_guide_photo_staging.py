"""Photo-to-caption mapping and preflight failure tests, no network or Pillow needed."""

import importlib.util
import pathlib
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "builder", "tools/offline_content/build_sd_content.py"
)
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class FakeImage:
    width = 100
    height = 100

    def __init__(self, path):
        self.source = pathlib.Path(path).read_text()

    def convert(self, *args):
        return self

    def save(self, path, *args, **kwargs):
        pathlib.Path(path).write_text(self.source)

    def __enter__(self):
        return self

    def __exit__(self, *args):
        pass


pil = types.ModuleType("PIL")
pil.Image = types.SimpleNamespace(open=FakeImage, LANCZOS=1)
pil.ImageOps = types.SimpleNamespace(exif_transpose=lambda im: im)


class PhotoStaging(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.raw = self.root / "raw"
        self.out = self.root / "photos"
        self.deck = self.raw / "plants" / "yarrow"
        self.deck.mkdir(parents=True)
        self.patch = patch.dict(sys.modules, {"PIL": pil})
        self.patch.start()
        self.addCleanup(self.patch.stop)

    def row(self, fn, species):
        return f"{fn}\t{species}\tArtist\tCC-BY\thttps://example.test/{fn}\n"

    def run_stage(self):
        builder.stage_photos(str(self.raw), str(self.out))

    def prepare(self):
        (self.deck / "0.png").write_text("EDIBLE")
        (self.deck / "3.jpg").write_text("POISON")
        (self.deck / "credits.tsv").write_text(
            self.row("3.jpg", "Poison hemlock - deadly lookalike")
            + self.row("0.png", "Yarrow")
        )

    def test_remapping(self):
        self.prepare()
        self.run_stage()
        self.assertEqual((self.out / "plants/yarrow/1.jpg").read_text(), "EDIBLE")
        self.assertEqual((self.out / "plants/yarrow/2.jpg").read_text(), "POISON")
        lines = (self.out / "CREDITS.tsv").read_text().splitlines()
        self.assertEqual(
            lines,
            [
                "plants/yarrow\t"
                + self.row("1.jpg", "Yarrow").strip().replace("/1.jpg", "/0.png"),
                "plants/yarrow\t"
                + self.row("2.jpg", "Poison hemlock - deadly lookalike")
                .strip()
                .replace("/2.jpg", "/3.jpg"),
            ],
        )

    def test_legacy_jpg_credit_for_png(self):
        self.prepare()
        p = self.deck / "credits.tsv"
        p.write_text(p.read_text().replace("0.png\t", "0.jpg\t"))
        self.run_stage()
        self.assertEqual((self.out / "plants/yarrow/1.jpg").read_text(), "EDIBLE")
        self.assertIn(
            "plants/yarrow\t1.jpg\tYarrow\t", (self.out / "CREDITS.tsv").read_text()
        )

    def test_bad_metadata_fails_before_photo_writes(self):
        self.prepare()
        self.out.mkdir()
        sentinel = self.out / "CREDITS.tsv"
        sentinel.write_text("old credits")
        for bad in [
            "",
            self.row("0.png", "Yarrow"),
            self.row("0.png", "Yarrow")
            + self.row("0.png", "Other")
            + self.row("3.jpg", "Poison"),
            self.row("0.png", "   ") + self.row("3.jpg", "Poison"),
            "0.png\tYarrow\tArtist\n",
            self.row("0.png", "Yarrow")
            + self.row("3.jpg", "Poison")
            + self.row("9.jpg", "Other"),
        ]:
            with self.subTest(bad=bad):
                (self.deck / "credits.tsv").write_text(bad)
                with self.assertRaises((ValueError, SystemExit)):
                    self.run_stage()
                self.assertEqual(sentinel.read_text(), "old credits")
                self.assertFalse((self.out / "plants").exists())

    def test_missing_credits(self):
        self.prepare()
        (self.deck / "credits.tsv").unlink()
        with self.assertRaises((ValueError, SystemExit, FileNotFoundError)):
            self.run_stage()
        self.assertFalse(self.out.exists())

    def test_ambiguous_number(self):
        self.prepare()
        (self.deck / "00.jpg").write_text("OTHER")
        with self.assertRaises((ValueError, SystemExit)):
            self.run_stage()
        self.assertFalse(self.out.exists())

    def test_too_many_photos(self):
        for n in range(7):
            (self.deck / f"{n}.jpg").write_text(str(n))
        (self.deck / "credits.tsv").write_text(
            "".join(self.row(f"{n}.jpg", f"Species {n}") for n in range(7))
        )
        with self.assertRaises((ValueError, SystemExit)):
            self.run_stage()
        self.assertFalse(self.out.exists())

    def test_all_decks_preflight(self):
        self.prepare()
        bad = self.raw / "z" / "bad"
        bad.mkdir(parents=True)
        (bad / "1.jpg").write_text("bad")
        with self.assertRaises((ValueError, SystemExit, FileNotFoundError)):
            self.run_stage()
        self.assertFalse(self.out.exists())

    def test_symlinks_rejected(self):
        self.prepare()
        outside = self.root / "outside"
        outside.write_text("outside")
        (self.deck / "unexpected-link").symlink_to(outside)
        with self.assertRaises(ValueError):
            self.run_stage()
        self.assertFalse(self.out.exists())
        self.assertEqual(outside.read_text(), "outside")

    def test_image_directory_rejected_before_writes(self):
        self.prepare()
        (self.deck / "4.jpg").mkdir()
        with self.assertRaises(ValueError):
            self.run_stage()
        self.assertFalse(self.out.exists())

    def test_main_calls_validated_staging(self):
        self.prepare()
        (self.root / "photos_raw").symlink_to(self.raw, target_is_directory=True)
        (self.root / "phrases_master.tsv").write_text("")
        with patch.multiple(
            builder,
            HERE=str(self.root),
            STAGING=str(self.root / "staged"),
            FONT_CACHE=str(self.root / "fonts"),
            LANGS=[],
            FONT_SOURCES={},
            GUIDE_SECTIONS=[],
        ):
            with self.assertRaises(ValueError):
                builder.main()
        self.assertFalse((self.root / "staged/guides/photos").exists())


if __name__ == "__main__":
    unittest.main()

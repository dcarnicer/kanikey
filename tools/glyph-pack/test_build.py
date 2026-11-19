#!/usr/bin/env python3
"""Tests for the glyph-pack builder. Run with:

    uv run --with pillow python3 tools/glyph-pack/test_build.py

The synthetic kind/header cases run without Pillow. The --split CLI test needs
Pillow and a Japanese font; it uses --font or DEFAULT_FONT and is skipped when
either is missing, so verify()/header logic is still exercised everywhere.
"""
from __future__ import annotations

import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import build  # noqa: E402

GLYPH = (0x3042, 20, b"\x0f", 1, 1, 1, 0, 0)
RADICAL = (1, b"\x0f", 1, 1)


def have_pillow() -> bool:
    return importlib.util.find_spec("PIL") is not None


class PackVerifyTest(unittest.TestCase):
    def test_v2_font_and_radical_kinds_verify(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            font = Path(tmp) / "glyphs.kgp"
            info = build.write_pack(font, [GLYPH], [], build.KIND_FONT)
            self.assertEqual((info.version, info.kind, info.glyphs, info.radicals),
                             (2, build.KIND_FONT, 1, 0))
            self.assertEqual(build.verify(font, build.KIND_FONT).kind, build.KIND_FONT)

            radicals = Path(tmp) / "radicals.kgp"
            info = build.write_pack(radicals, [], [RADICAL], build.KIND_RADICAL)
            self.assertEqual((info.version, info.kind, info.glyphs, info.radicals),
                             (2, build.KIND_RADICAL, 0, 1))
            self.assertEqual(build.verify(radicals, build.KIND_RADICAL).kind,
                             build.KIND_RADICAL)

    def test_v1_combined_still_verifies(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "glyphs-v1.kgp"
            info = build.write_pack(path, [GLYPH], [RADICAL], build.KIND_COMBINED,
                                    version=1)
            self.assertEqual((info.version, info.kind, info.glyphs, info.radicals),
                             (1, build.KIND_COMBINED, 1, 1))
            self.assertEqual(build.read_pack(path).version, 1)

    def test_kind_table_mismatch_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.kgp"
            path.write_bytes(build.pack_bytes([GLYPH], [RADICAL], build.KIND_FONT))
            with self.assertRaises(AssertionError):
                build.verify(path)
            path.write_bytes(build.pack_bytes([GLYPH], [RADICAL], build.KIND_RADICAL))
            with self.assertRaises(AssertionError):
                build.verify(path)

    def test_public_chars_are_account_independent(self) -> None:
        chars = build.public_chars()
        for ch in ("あ", "ア", "一", "々", "ー", "語"):
            self.assertIn(ch, chars)
        self.assertNotIn("A", chars)
        self.assertGreaterEqual(len(chars), 6000)
        # JIS X 0208 is two bytes in EUC-JP; JIS X 0212 is three and excluded.
        for ch in list(chars)[::200]:
            self.assertEqual(len(ch.encode("euc_jp")), 2, ch)


@unittest.skipUnless(have_pillow(), "Pillow is not installed")
class SplitCliTest(unittest.TestCase):
    def font(self) -> Path:
        import os

        candidate = Path(os.environ.get("KANIKEY_TEST_FONT", build.DEFAULT_FONT))
        if candidate.exists():
            return candidate
        self.skipTest(f"Japanese font not found: {candidate}")

    def run_build(self, *args: str) -> None:
        subprocess.run([sys.executable, str(HERE / "build.py"), *args], check=True,
                       cwd=HERE)

    def test_split_without_token_writes_verified_font_pack(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "packs"
            self.run_build("--split", str(out), "--chars", "あア一", "--no-kana",
                           "--font", str(self.font()))
            font = out / "glyphs.kgp"
            self.assertTrue(font.exists())
            self.assertFalse((out / "radicals.kgp").exists())
            info = build.verify(font, build.KIND_FONT)
            self.assertEqual(info.version, 2)
            self.assertEqual(info.glyphs, 9)  # three characters at three sizes
            self.assertEqual(info.radicals, 0)

    def test_single_out_stays_combined(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "glyphs-test.kgp"
            self.run_build("--chars", "あア一", "--no-kana", "--font", str(self.font()),
                           "--out", str(out))
            info = build.verify(out, build.KIND_COMBINED)
            self.assertEqual(info.version, 2)
            self.assertGreater(info.glyphs, 0)
            self.assertEqual(info.radicals, 0)


if __name__ == "__main__":
    unittest.main()

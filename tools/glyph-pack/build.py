#!/usr/bin/env python3
"""Build a KaniKey glyph pack for the microSD card.

The pack contains 4bpp glyphs for kanji/kana at several sizes, plus optional
radical bitmaps. The firmware reads it from /kanikey/glyphs.kgp on the card.

Test pack from a character list (no API):

    uv run --with pillow python3 tools/glyph-pack/build.py \
        --chars "食休日本語" --out dist/glyphs-test.kgp

Full kanji/kana pack from Tangocho's local WaniKani kanji cache (no token):

    uv run --with pillow python3 tools/glyph-pack/build.py \
        --tangocho --out dist/glyphs-full.kgp

Full pack from a WaniKani account (token stays in memory; includes radicals):

    WANIKANI_TOKEN=... uv run --with pillow --with cairosvg \
        python3 tools/glyph-pack/build.py --out dist/glyphs.kgp

The generator uses Noto Sans JP by default (OFL); pass --font for another font.
Radicals are rasterised from the SVG URLs the API returns for the user's own
account; the generated pack is personal and must not be redistributed.
"""
from __future__ import annotations

import argparse
import io
import json
import os
import re
import struct
import sys
import time
import urllib.request
import zlib
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

API = "https://api.wanikani.com/v2/"
REVISION = "20170710"
DEFAULT_FONT = Path.home() / "code/tangocho/tools/pack-builder/cache/noto/NotoSansJP-Regular.otf"
DEFAULT_KANJI_CACHE = Path.home() / "code/tangocho/tools/pack-builder/cache/kanji.json"
SIZES = (64, 32, 20)
KANA = (
    "".join(chr(c) for c in range(0x3041, 0x3097))
    + "ー"
    + "".join(chr(c) for c in range(0x30A1, 0x30FB))
    + "々〜・〇"  # common in WaniKani vocabulary
)


def rle4(nibbles: bytes) -> bytes:
    out = bytearray()
    i = 0
    n = len(nibbles)
    while i < n:
        v = nibbles[i]
        run = 1
        while i + run < n and nibbles[i + run] == v and run < 16:
            run += 1
        out.append(((run - 1) << 4) | v)
        i += run
    return bytes(out)


def glyph_bitmap(font: ImageFont.FreeTypeFont, ch: str) -> tuple[bytes, int, int, int, int, int] | None:
    try:
        bbox = font.getbbox(ch, anchor="ls")
    except Exception:
        return None
    x0, y0, x1, y1 = bbox
    w, h = x1 - x0, y1 - y0
    if w <= 0 or h <= 0:
        return None
    im = Image.new("L", (w, h), 0)
    ImageDraw.Draw(im).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
    nibbles = bytearray()
    for px in im.getdata():
        nibbles.append(px >> 4)
    advance = int(round(font.getlength(ch))) or w
    return rle4(bytes(nibbles)), w, h, advance, x0, y0


def radical_bitmap(svg: bytes, size: int = 96) -> tuple[bytes, int, int] | None:
    try:
        import cairosvg
    except ImportError:
        print("warning: cairosvg missing; radicals skipped", file=sys.stderr)
        return None
    # WaniKani's inline SVGs use CSS custom properties such as
    # `stroke:var(--color-text, #000)`. cairosvg does not resolve var(), so
    # inline the fallback value (or black when there is no fallback).
    svg = re.sub(rb"var\([^,)]+,\s*([^)]+)\)", rb"\1", svg)
    svg = re.sub(rb"var\([^)]*\)", b"#000", svg)
    png = cairosvg.svg2png(bytestring=svg, output_width=size, output_height=size)
    im = Image.open(io.BytesIO(png)).convert("RGBA")
    alpha = im.getchannel("A")
    nibbles = bytearray()
    for px in alpha.getdata():
        nibbles.append(px >> 4)
    return rle4(bytes(nibbles)), size, size


def fetch_json(url: str, token: str) -> dict:
    req = urllib.request.Request(
        url,
        headers={
            "Authorization": f"Bearer {token}",
            "Wanikani-Revision": REVISION,
            "User-Agent": "KaniKey-glyph-pack",
        },
    )
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.loads(r.read())


def all_pages(url: str, token: str):
    while url:
        page = fetch_json(url, token)
        yield from page.get("data") or []
        url = (page.get("pages") or {}).get("next_url") or ""


def collect_from_api(token: str) -> tuple[set[str], list[tuple[int, str]]]:
    user = fetch_json(API + "user", token)
    max_level = int((user.get("data") or {}).get("subscription", {}).get("max_level_granted", 3))
    print(f"account max level: {max_level}")
    chars: set[str] = set(KANA)
    radicals: list[tuple[int, str]] = []
    for s in all_pages(API + "subjects", token):
        if int((s.get("data") or {}).get("level") or 0) > max_level:
            continue
        d = s.get("data") or {}
        text = d.get("characters")
        if text:
            chars.update(text)
        if s.get("object") == "radical":
            for img in d.get("character_images") or []:
                if img.get("content_type") == "image/svg+xml" and img.get("url"):
                    radicals.append((int(s["id"]), img["url"]))
                    break
    return chars, radicals


def build(chars: set[str], radicals: list[tuple[int, str]], font_path: Path, out: Path, token: str | None):
    if not font_path.exists():
        raise SystemExit(f"font not found: {font_path}")
    glyphs: list[tuple[int, int, bytes, int, int, int, int, int]] = []
    for size in SIZES:
        font = ImageFont.truetype(str(font_path), size)
        for ch in sorted(chars):
            if len(ch) != 1:
                continue
            cp = ord(ch)
            if cp < 0x20:
                continue
            g = glyph_bitmap(font, ch)
            if g is None:
                continue
            data, w, h, adv, xoff, yoff = g
            glyphs.append((cp, size, data, w, h, adv, xoff, yoff))
        print(f"size {size}: {sum(1 for g in glyphs if g[1] == size)} glyphs")

    radical_data: list[tuple[int, bytes, int, int]] = []
    if radicals and token:
        for i, (sid, url) in enumerate(radicals, 1):
            try:
                req = urllib.request.Request(url, headers={"User-Agent": "KaniKey-glyph-pack"})
                with urllib.request.urlopen(req, timeout=30) as r:
                    svg = r.read()
                b = radical_bitmap(svg)
                if b:
                    radical_data.append((sid, b[0], b[1], b[2]))
            except Exception as e:
                print(f"warning: radical {sid}: {e}", file=sys.stderr)
            if i % 50 == 0:
                print(f"radicals {i}/{len(radicals)}")
                time.sleep(0.2)

    glyphs.sort(key=lambda g: (g[0], g[1]))
    radical_data.sort(key=lambda r: r[0])
    table = bytearray()
    data = bytearray()
    data_base = 32 + 20 * len(glyphs) + 16 * len(radical_data)
    for cp, size, payload, w, h, adv, xoff, yoff in glyphs:
        table += struct.pack("<IIIBBBBbbBB", cp, data_base + len(data), len(payload),
                             w, h, size, adv, xoff, yoff, 1, 0)
        data += payload
    for sid, payload, w, h in radical_data:
        table += struct.pack("<IIIBBBB", sid, data_base + len(data), len(payload),
                             w, h, 1, 0)
        data += payload

    header = struct.pack("<4sHHHHIIIII", b"KKGP", 1, 32, len(glyphs), len(radical_data),
                         32, data_base, len(data), zlib.crc32(table + data) & 0xFFFFFFFF, 0)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(header + table + data)
    print(f"wrote {out} ({out.stat().st_size} bytes): {len(glyphs)} glyphs, {len(radical_data)} radicals")
    verify(out)


def verify(path: Path) -> None:
    raw = path.read_bytes()
    magic, version, header_size, glyphs, radicals, table_off, data_off, data_len, crc, _ = (
        struct.unpack_from("<4sHHHHIIIII", raw, 0)
    )
    assert magic == b"KKGP" and version == 1 and header_size == 32, "bad header"
    assert table_off == 32, "bad table offset"
    assert data_off == 32 + 20 * glyphs + 16 * radicals, "bad data offset"
    assert data_off + data_len <= len(raw), "truncated payload"
    assert (zlib.crc32(raw[table_off : data_off + data_len]) & 0xFFFFFFFF) == crc, "bad crc"
    last = (-1, -1)
    for i in range(glyphs):
        cp, off, length, w, h, size, adv, xo, yo, enc, _ = struct.unpack_from(
            "<IIIBBBBbbBB", raw, table_off + i * 20
        )
        assert (cp, size) >= last, f"glyph {i} not sorted"
        last = (cp, size)
        assert size in SIZES and w and h and enc == 1, f"bad glyph {i}"
        assert data_off <= off and off + length <= data_off + data_len, f"glyph {i} outside data"
    for i in range(radicals):
        sid, off, length, w, h, enc, _ = struct.unpack_from(
            "<IIIBBBB", raw, table_off + glyphs * 20 + i * 16
        )
        assert w and h and enc == 1, f"bad radical {i}"
        assert data_off <= off and off + length <= data_off + data_len, f"radical {i} outside data"
    print(f"verified {path.name}: {glyphs} glyphs, {radicals} radicals")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--chars", help="explicit characters (test mode; no API)")
    ap.add_argument("--font", type=Path, default=DEFAULT_FONT)
    ap.add_argument("--out", type=Path, default=Path("dist/glyphs.kgp"))
    ap.add_argument("--token", default=os.environ.get("WANIKANI_TOKEN"))
    ap.add_argument("--no-kana", action="store_true")
    ap.add_argument("--tangocho", action="store_true",
                    help="use Tangocho's WaniKani kanji cache instead of the API")
    ap.add_argument("--kanji-cache", type=Path, default=DEFAULT_KANJI_CACHE)
    args = ap.parse_args()
    if args.tangocho:
        if not args.kanji_cache.exists():
            print(f"error: kanji cache not found: {args.kanji_cache}", file=sys.stderr)
            return 2
        data = json.loads(args.kanji_cache.read_text(encoding="utf-8"))
        chars: set[str] = set(KANA)
        kanji = 0
        for ch, entry in data.items():
            if len(ch) == 1 and entry.get("wk_level"):
                chars.add(ch)
                kanji += 1
        print(f"WaniKani kanji in cache: {kanji}, kana included: {not args.no_kana}")
        if args.no_kana:
            chars.difference_update(KANA)
        build(chars, [], args.font, args.out, None)
        return 0
    if args.chars is not None:
        chars = set(args.chars)
        if not args.no_kana:
            chars.update(KANA)
        build(chars, [], args.font, args.out, None)
        return 0
    if not args.token:
        print("error: set WANIKANI_TOKEN or pass --chars for a test pack", file=sys.stderr)
        return 2
    chars, radicals = collect_from_api(args.token)
    build(chars, radicals, args.font, args.out, args.token)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Build KaniKey glyph packs for the microSD card.

Since ONB-01 the device reads two files:

    /kanikey/glyphs.kgp    font pack (kind 1): kana/kanji, no WaniKani content
    /kanikey/radicals.kgp  personal radical pack (kind 2): token-generated

The font pack is built from a fixed, account-independent character set (kana
plus the JIS X 0208 characters) and needs no token. Radicals come from the
user's own WaniKani account and must not be shared.

Split build (the current flow):

    uv run --with pillow python3 tools/glyph-pack/build.py --split dist
    WANIKANI_TOKEN=... uv run --with pillow --with cairosvg \\
        python3 tools/glyph-pack/build.py --split dist

The token run writes both files; without a token only glyphs.kgp is written.

The legacy single-file combined pack (kind 0) is still built with --out:

    uv run --with pillow python3 tools/glyph-pack/build.py \\
        --chars "食休日本語" --out dist/glyphs-test.kgp

    uv run --with pillow python3 tools/glyph-pack/build.py \\
        --tangocho --out dist/glyphs-full.kgp

    WANIKANI_TOKEN=... uv run --with pillow --with cairosvg \\
        python3 tools/glyph-pack/build.py --out dist/glyphs.kgp

A v1 combined pack from an older build is still readable by the firmware and
by verify().
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
from dataclasses import dataclass
from pathlib import Path

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

KIND_COMBINED = 0
KIND_FONT = 1
KIND_RADICAL = 2
KIND_NAMES = {KIND_COMBINED: "combined", KIND_FONT: "font", KIND_RADICAL: "radicals"}
HEADER_BYTES = 32
GLYPH_BYTES = 20
RADICAL_BYTES = 16


@dataclass(frozen=True)
class PackInfo:
    version: int
    kind: int
    glyphs: int
    radicals: int
    crc: int


def public_chars() -> set[str]:
    """The fixed font-pack set: kana plus JIS X 0208 (CJIS X 0208) characters.

    The set is derived from Python's euc_jp codec, not from a WaniKani account
    or subject list. JIS X 0208 encodes to two bytes in EUC-JP; the three-byte
    forms are JIS X 0212 and are deliberately left out.
    """
    chars = set(KANA)
    for cp in range(0x3000, 0xA000):
        ch = chr(cp)
        if ch.isspace():
            continue
        try:
            encoded = ch.encode("euc_jp")
        except UnicodeEncodeError:
            continue
        if len(encoded) == 2:
            chars.add(ch)
    return chars


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


def glyph_bitmap(font, ch: str) -> tuple[bytes, int, int, int, int, int] | None:
    from PIL import Image, ImageDraw

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
        from PIL import Image
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


def render_glyphs(chars: set[str], font_path: Path):
    from PIL import ImageFont

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
    glyphs.sort(key=lambda g: (g[0], g[1]))
    return glyphs


def fetch_radical_data(radicals: list[tuple[int, str]], token: str):
    radical_data: list[tuple[int, bytes, int, int]] = []
    if not radicals or not token:
        return radical_data
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
    radical_data.sort(key=lambda r: r[0])
    return radical_data


def pack_bytes(glyphs, radicals, kind: int, version: int = 2) -> bytes:
    table = bytearray()
    data = bytearray()
    data_base = HEADER_BYTES + GLYPH_BYTES * len(glyphs) + RADICAL_BYTES * len(radicals)
    for cp, size, payload, w, h, adv, xoff, yoff in glyphs:
        table += struct.pack("<IIIBBBBbbBB", cp, data_base + len(data), len(payload),
                             w, h, size, adv, xoff, yoff, 1, 0)
        data += payload
    for sid, payload, w, h in radicals:
        table += struct.pack("<IIIBBBB", sid, data_base + len(data), len(payload),
                             w, h, 1, 0)
        data += payload

    reserved = kind if version >= 2 else 0
    header = struct.pack("<4sHHHHIIIII", b"KKGP", version, HEADER_BYTES, len(glyphs),
                         len(radicals), HEADER_BYTES, data_base, len(data),
                         zlib.crc32(table + data) & 0xFFFFFFFF, reserved)
    return header + table + data


def write_pack(out: Path, glyphs, radicals, kind: int, version: int = 2) -> PackInfo:
    if version >= 2:
        if kind == KIND_FONT and (not glyphs or radicals):
            raise SystemExit("font pack needs glyphs and no radicals")
        if kind == KIND_RADICAL and (not radicals or glyphs):
            raise SystemExit("radical pack needs radicals and no glyphs")
        if kind == KIND_COMBINED and not glyphs:
            raise SystemExit("combined pack needs glyphs")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(pack_bytes(glyphs, radicals, kind, version))
    print(f"wrote {out} ({out.stat().st_size} bytes): {len(glyphs)} glyphs, "
          f"{len(radicals)} radicals, v{version} kind {KIND_NAMES.get(kind, kind)}")
    return verify(out)


def read_pack(path: Path) -> PackInfo:
    raw = path.read_bytes()
    (magic, version, header_size, glyphs, radicals, table_off, data_off, data_len,
     crc, reserved) = struct.unpack_from("<4sHHHHIIIII", raw, 0)
    kind = reserved if version >= 2 else KIND_COMBINED
    return PackInfo(version=version, kind=kind, glyphs=glyphs, radicals=radicals, crc=crc)


def verify(path: Path, expected_kind: int | None = None) -> PackInfo:
    raw = path.read_bytes()
    assert len(raw) >= HEADER_BYTES, "short header"
    (magic, version, header_size, glyphs, radicals, table_off, data_off, data_len,
     crc, reserved) = struct.unpack_from("<4sHHHHIIIII", raw, 0)
    assert magic == b"KKGP", "bad magic"
    assert version in (1, 2), f"bad version {version}"
    assert header_size == HEADER_BYTES, "bad header size"
    assert table_off == HEADER_BYTES, "bad table offset"
    kind = KIND_COMBINED if version == 1 else reserved
    if version >= 2:
        assert kind in KIND_NAMES, f"bad kind {kind}"
    if expected_kind is not None:
        assert kind == expected_kind, f"kind {kind}, expected {expected_kind}"
    if kind == KIND_FONT:
        assert glyphs > 0 and radicals == 0, "font kind needs glyphs, no radicals"
    elif kind == KIND_RADICAL:
        assert radicals > 0 and glyphs == 0, "radical kind needs radicals, no glyphs"
    else:
        assert glyphs > 0, "combined kind needs glyphs"
    assert data_off == HEADER_BYTES + GLYPH_BYTES * glyphs + RADICAL_BYTES * radicals, \
        "bad data offset"
    assert data_off + data_len <= len(raw), "truncated payload"
    assert (zlib.crc32(raw[table_off:data_off + data_len]) & 0xFFFFFFFF) == crc, "bad crc"
    last = (-1, -1)
    for i in range(glyphs):
        cp, off, length, w, h, size, adv, xo, yo, enc, _ = struct.unpack_from(
            "<IIIBBBBbbBB", raw, table_off + i * GLYPH_BYTES
        )
        assert (cp, size) >= last, f"glyph {i} not sorted"
        last = (cp, size)
        assert size in SIZES and w and h and enc == 1, f"bad glyph {i}"
        assert data_off <= off and off + length <= data_off + data_len, f"glyph {i} outside data"
    last_sid = -1
    for i in range(radicals):
        sid, off, length, w, h, enc, _ = struct.unpack_from(
            "<IIIBBBB", raw, table_off + glyphs * GLYPH_BYTES + i * RADICAL_BYTES
        )
        assert sid > last_sid, f"radical {i} not sorted"
        last_sid = sid
        assert w and h and enc == 1, f"bad radical {i}"
        assert data_off <= off and off + length <= data_off + data_len, f"radical {i} outside data"
    print(f"verified {path.name}: v{version} kind {KIND_NAMES[kind]}, {glyphs} glyphs, "
          f"{radicals} radicals")
    return PackInfo(version=version, kind=kind, glyphs=glyphs, radicals=radicals, crc=crc)


def build(chars: set[str], radicals: list[tuple[int, str]], font_path: Path, out: Path,
          token: str | None) -> None:
    glyphs = render_glyphs(chars, font_path)
    radical_data = fetch_radical_data(radicals, token) if radicals and token else []
    write_pack(out, glyphs, radical_data, KIND_COMBINED)


def build_split(split_dir: Path, chars: set[str], token: str | None, font_path: Path) -> int:
    glyphs = render_glyphs(chars, font_path)
    if not glyphs:
        print("error: no glyphs rendered", file=sys.stderr)
        return 2
    write_pack(split_dir / "glyphs.kgp", glyphs, [], KIND_FONT)

    if not token:
        print("no token: radicals.kgp skipped (font pack only)")
        return 0
    _, radicals = collect_from_api(token)
    radical_data = fetch_radical_data(radicals, token)
    if not radical_data:
        print("error: no radical bitmaps fetched; radicals.kgp not written", file=sys.stderr)
        return 2
    write_pack(split_dir / "radicals.kgp", [], radical_data, KIND_RADICAL)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--chars", help="explicit characters (test mode; no API)")
    ap.add_argument("--font", type=Path, default=DEFAULT_FONT)
    output = ap.add_mutually_exclusive_group()
    output.add_argument("--out", type=Path, default=Path("dist/glyphs.kgp"),
                        help="single combined pack (v2 kind 0)")
    output.add_argument("--split", type=Path, metavar="DIR",
                        help="write DIR/glyphs.kgp (kind 1) and DIR/radicals.kgp (kind 2)")
    ap.add_argument("--token", default=os.environ.get("WANIKANI_TOKEN"))
    ap.add_argument("--no-kana", action="store_true")
    ap.add_argument("--tangocho", action="store_true",
                    help="use Tangocho's WaniKani kanji cache instead of the API")
    ap.add_argument("--kanji-cache", type=Path, default=DEFAULT_KANJI_CACHE)
    args = ap.parse_args()

    if args.split:
        if args.tangocho:
            print("error: --tangocho builds a personal combined pack; "
                  "use --split with --token", file=sys.stderr)
            return 2
        if args.chars is not None:
            chars = set(args.chars)
            if not args.no_kana:
                chars.update(KANA)
        else:
            chars = public_chars()
            if args.no_kana:
                chars.difference_update(KANA)
            print(f"public font set: {len(chars)} characters (kana + JIS X 0208)")
        return build_split(args.split, chars, args.token, args.font)

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
        print("error: set WANIKANI_TOKEN, pass --chars for a test pack, or use --split "
              "for a font-only pack", file=sys.stderr)
        return 2
    chars, radicals = collect_from_api(args.token)
    build(chars, radicals, args.font, args.out, args.token)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

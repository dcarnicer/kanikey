#!/usr/bin/env python3
"""Render the KaniKey screens from the firmware's own layouts.

The images in ``docs/screens`` are renders, not device captures. UI text is
drawn with the M5GFX bitmap fonts the firmware uses: Font0/Font1 (the 6x8 GLCD
font), Font2 (16 px) and Font4 (26 px). Every colour comes from the RGB565
constants in ``firmware/src/ui/Theme.h``, quantized to the 8-bit canvas's RGB332
palette and expanded back to RGB. Japanese prompts and readings use Noto Sans JP
only as a stand-in for the microSD glyph pack; no Latin UI text uses it.

    python3 tools/mockups/render.py
    python3 tools/mockups/render.py --check

Output: ``docs/screens/*.png`` (individual screens at 4x, contact sheet at 2x).
``--check`` reads the committed PNGs and fails on a size or palette mismatch.
"""
from __future__ import annotations

import argparse
import base64
import os
import zlib
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:  # pragma: no cover - the README gives the uv command
    raise SystemExit(
        "Pillow is not installed. Run "
        "`uv run --with pillow python3 tools/mockups/render.py`."
    )

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs" / "screens"

W, H = 240, 135
SCALE = 4

# --- M5GFX bitmap fonts -------------------------------------------------------
# The tables are copied from the M5GFX headers that the firmware links:
#   Font0/Font1  lgfx/Fonts/glcdfont.h  (6x8 GLCD, 5 data columns)
#   Font2        lgfx/Fonts/Font16.h    (16 px, per-character width)
#   Font4        lgfx/Fonts/Font32rle.h (26 px, 8-bit RLE)
# Stored as one zlib/base64 blob: Font0 95*5 bytes, then Font2 (width, bytes
# per row, 16 rows), then Font4 (width, RLE length, RLE bytes).
_FONT_B64 = """
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
"""

FONT_HEIGHT = {0: 8, 1: 8, 2: 16, 4: 26}
FONTS: dict[int, dict[str, tuple[int, list[list[bool]]]]] = {}


def _decode_rle(width: int, data: bytes) -> list[list[bool]]:
    rows = [[False] * width for _ in range(26)]
    x = y = 0
    for run in data:
        length = (run & 0x7F) + 1
        on = bool(run & 0x80)
        for _ in range(length):
            if y < 26 and x < width and on:
                rows[y][x] = True
            x += 1
            if x == width:
                x = 0
                y += 1
    return rows


def _load_fonts() -> None:
    raw = zlib.decompress(base64.b64decode("".join(_FONT_B64.split())))
    pos = 0
    f0: dict[str, tuple[int, list[list[bool]]]] = {}
    for code in range(32, 127):
        cols = raw[pos:pos + 5]
        pos += 5
        rows = [[bool(cols[col] & (1 << row)) for col in range(5)] for row in range(8)]
        f0[chr(code)] = (6, rows)
    f2: dict[str, tuple[int, list[list[bool]]]] = {}
    for code in range(32, 127):
        advance = raw[pos]
        stride = raw[pos + 1]
        pos += 2
        glyph = raw[pos:pos + 16 * stride]
        pos += 16 * stride
        bits = []
        for row in range(16):
            line = []
            for col in range(advance - 1):
                line.append(bool(glyph[row * stride + (col >> 3)] & (0x80 >> (col & 7))))
            bits.append(line)
        f2[chr(code)] = (advance, bits)
    f4: dict[str, tuple[int, list[list[bool]]]] = {}
    for code in range(32, 127):
        width = raw[pos]
        length = raw[pos + 1]
        pos += 2
        f4[chr(code)] = (width, _decode_rle(width, raw[pos:pos + length]))
        pos += length
    if pos != len(raw):
        raise SystemExit("font blob is malformed")
    FONTS[0] = f0
    FONTS[1] = f0  # M5GFX maps setTextFont(1) to Font0.
    FONTS[2] = f2
    FONTS[4] = f4


_load_fonts()

# --- palette -----------------------------------------------------------------
# RGB565 constants from firmware/src/ui/Theme.h. The canvas converts each to
# RGB332 with color_convert<rgb332_t, rgb565_t> and the panel expands it back.
RGB565 = {
    "bg": 0x0000,
    "surface": 0x0108,
    "surface_hi": 0x2128,
    "line": 0x2208,
    "text": 0xFFFF,
    "dim": 0xA577,
    "faint": 0x636F,
    "brand": 0xE308,
    "ok": 0x5EF1,
    "ok_bg": 0x0183,
    "accent": 0x2B1B,
    "accent_hi": 0x7D5F,
    "reading": 0x2D14,
    "err": 0xFB60,
    "err_bg": 0x3882,
    "warn": 0xFE26,
}


def _to332(v: int) -> int:
    return (((((v >> 13) & 7) << 3) + ((v >> 8) & 7)) << 2) + ((v >> 3) & 3)


def _expand332(v: int) -> tuple[int, int, int]:
    r = (v >> 5) & 7
    g = (v >> 2) & 7
    b = v & 3
    return (36 * r + r // 2, 36 * g + g // 2, 85 * b)


PALETTE = {name: _expand332(_to332(v)) for name, v in RGB565.items()}
ALLOWED = set(PALETTE.values())
# GOLD is M5GFX's TFT_GOLD (0xFEA0) in HomeScreen/StatsScreen, not a Theme.h
# token. It quantizes to the same #FFDB00 as kWarn, so kWarn is used here.
BG = PALETTE["bg"]
SURFACE = PALETTE["surface"]
SURFACE_HI = PALETTE["surface_hi"]
LINE = PALETTE["line"]
TEXT = PALETTE["text"]
DIM = PALETTE["dim"]
FAINT = PALETTE["faint"]
BRAND = PALETTE["brand"]
OK = PALETTE["ok"]
OK_BG = PALETTE["ok_bg"]
ACCENT = PALETTE["accent"]
ACCENT_HI = PALETTE["accent_hi"]
READING = PALETTE["reading"]
ERR = PALETTE["err"]
ERR_BG = PALETTE["err_bg"]
WARN = PALETTE["warn"]


def nearest_palette(rgb: tuple[int, int, int]) -> tuple[int, int, int]:
    return min(ALLOWED, key=lambda p: (p[0] - rgb[0]) ** 2 + (p[1] - rgb[1]) ** 2 +
                                      (p[2] - rgb[2]) ** 2)


_QUANT_CACHE: dict[tuple[int, int, int], tuple[int, int, int]] = {}


def _pixels(im: Image.Image) -> list[tuple[int, int, int]]:
    getter = getattr(im, "get_flattened_data", None)
    return list(getter() if getter is not None else im.getdata())


def quantize(im: Image.Image) -> Image.Image:
    """Map antialiased pack stand-in pixels to the nearest RGB332 colour."""
    data = _pixels(im)
    for i, px in enumerate(data):
        if px not in _QUANT_CACHE:
            _QUANT_CACHE[px] = nearest_palette(px)
        data[i] = _QUANT_CACHE[px]
    im.putdata(data)
    return im


# --- drawing helpers ----------------------------------------------------------
def text_width(s: str, font: int = 0, size: int = 1) -> int:
    table = FONTS[font]
    space = table[" "][0]
    return sum(table.get(ch, (space, []))[0] for ch in s) * size


def text(im: Image.Image, x: int, y: int, s: str, font: int = 0, size: int = 1,
         color: tuple[int, int, int] = TEXT, anchor: str = "lt") -> None:
    d = ImageDraw.Draw(im)
    width = text_width(s, font, size)
    height = FONT_HEIGHT[font] * size
    if anchor == "rt":
        x -= width
    elif anchor == "mt":
        x -= width // 2
    elif anchor == "mm":
        x -= width // 2
        y -= height // 2
    elif anchor == "rm":
        x -= width
        y -= height // 2
    table = FONTS[font]
    space = table[" "]
    for ch in s:
        advance, rows = table.get(ch, space)
        for row, line in enumerate(rows):
            for col, on in enumerate(line):
                if on:
                    rect(im, x + col * size, y + row * size, size, size, color)
        x += advance * size


def rect(im: Image.Image, x: int, y: int, w: int, h: int,
         fill: tuple[int, int, int] | None = None,
         outline: tuple[int, int, int] | None = None) -> None:
    if w <= 0 or h <= 0:
        return
    ImageDraw.Draw(im).rectangle((x, y, x + w - 1, y + h - 1), fill=fill, outline=outline)


def line(im: Image.Image, x0: int, y0: int, x1: int, y1: int,
         color: tuple[int, int, int]) -> None:
    ImageDraw.Draw(im).line((x0, y0, x1, y1), fill=color)


def pill(im: Image.Image, x: int, y: int, label: str,
         fg: tuple[int, int, int], bg: tuple[int, int, int],
         badge: str | None = None) -> None:
    """A pill; `badge` is an optional icon after the label (Widgets.cpp)."""
    label_w = text_width(label, 0)
    w = label_w + 8 + (ICON_W + 4 if badge else 0)
    rect(im, x, y, w, 13, bg)
    text(im, x + 4, y + 2, label, 0, 1, fg)
    if badge:
        icon(im, badge, x + 4 + label_w + 4, y + 1, fg)


def progress(im: Image.Image, x: int, y: int, w: int, h: int, done: int, total: int,
             fill: tuple[int, int, int] = ACCENT_HI,
             track: tuple[int, int, int] = SURFACE_HI) -> None:
    rect(im, x, y, w, h, track)
    if total > 0:
        rect(im, x, y, max(1, w * done // total), h, fill)


def clipped(s: str, max_width: int, font: int = 0, size: int = 1) -> str:
    """Trim from the end until the string fits, like the screen's while loop."""
    while len(s) > 1 and text_width(s, font, size) > max_width:
        s = s[:-1]
    return s


def list_window(count: int, selected: int, visible: int) -> tuple[int, int]:
    """The firmware's listWindow(): centred selection, clamped first row."""
    if count <= 0:
        return 0, 0
    selected = 0 if selected < 0 else count - 1 if selected >= count else selected
    first = selected - visible // 2
    max_first = max(0, count - visible)
    first = min(max(first, 0), max_first)
    return first, min(visible, count - first)


def scrollbar(im: Image.Image, x: int, y: int, height: int, count: int, visible: int,
              first: int) -> None:
    if count <= visible or visible <= 0:
        return
    rect(im, x, y, 2, height, LINE)
    thumb = max(10, height * visible // count)
    max_first = count - visible
    thumb_y = y + ((height - thumb) * first // max_first if max_first > 0 else 0)
    rect(im, x, thumb_y, 2, thumb, ACCENT_HI)


# --- UI icons -----------------------------------------------------------------
# The 11x11 1bpp tables from firmware/src/ui/Icons.h, MSB first with a 2-byte
# stride. Screens draw these after a label; the text stays in place.
ICON_W = ICON_H = 11
ICONS: dict[str, list[int]] = {
    "check": [
        0x00, 0x00, 0x00, 0x60, 0x00, 0xC0, 0x01, 0x80, 0x03, 0x00, 0xC6,
        0x00, 0x6C, 0x00, 0x38, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
    ],
    "cross": [
        0x00, 0x00, 0x60, 0xC0, 0x31, 0x80, 0x1B, 0x00, 0x0E, 0x00, 0x04,
        0x00, 0x0E, 0x00, 0x1B, 0x00, 0x31, 0x80, 0x60, 0xC0, 0x00, 0x00,
    ],
    "wifi": [
        0x0E, 0x00, 0x31, 0x80, 0x60, 0xC0, 0xC0, 0x60, 0x00, 0x00, 0x1F,
        0x00, 0x31, 0x80, 0x60, 0xC0, 0x00, 0x00, 0x0E, 0x00, 0x04, 0x00,
    ],
    "sync": [
        0x0F, 0x80, 0x30, 0xE0, 0x60, 0x60, 0xC0, 0x00, 0x80, 0x00, 0x80,
        0x00, 0xC0, 0xC0, 0x60, 0xE0, 0x31, 0x80, 0x1F, 0x00, 0x00, 0x00,
    ],
    "sd": [
        0x3F, 0x80, 0x20, 0xC0, 0x20, 0x60, 0x20, 0x60, 0x20, 0x60, 0x20,
        0x60, 0x20, 0x60, 0x20, 0x60, 0x20, 0x60, 0x3F, 0x80, 0x00, 0x00,
    ],
    "key": [
        0x1E, 0x00, 0x21, 0x00, 0x21, 0x00, 0x1E, 0x00, 0x04, 0x00, 0x04,
        0x00, 0x07, 0x00, 0x04, 0x00, 0x07, 0x00, 0x04, 0x00, 0x00, 0x00,
    ],
    "outbox": [
        0x04, 0x00, 0x0E, 0x00, 0x15, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00,
        0x00, 0x40, 0x40, 0xC0, 0x60, 0x80, 0x20, 0xFF, 0xE0, 0x00, 0x00,
    ],
}


def icon_pixel(name: str, x: int, y: int) -> bool:
    bits = ICONS[name]
    if x < 0 or y < 0 or x >= ICON_W or y >= ICON_H:
        return False
    return bool(bits[y * 2 + (x >> 3)] & (0x80 >> (x & 7)))


def icon(im: Image.Image, name: str, x: int, y: int,
         color: tuple[int, int, int]) -> None:
    """Draw the 1bpp table like ui::drawIcon(): set pixels only, no box."""
    for row in range(ICON_H):
        for col in range(ICON_W):
            if icon_pixel(name, col, row):
                rect(im, x + col, y + row, 1, 1, color)


# --- Japanese pack stand-in ---------------------------------------------------
def find_font() -> Path | None:
    candidates = [
        os.environ.get("KANIKEY_FONT"),
        str(ROOT / "tools/pack-builder/cache/noto/NotoSansJP-Regular.otf"),
        str(Path.home() / "code/tangocho/tools/pack-builder/cache/noto/NotoSansJP-Regular.otf"),
    ]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return Path(candidate)
    return None


JP_FONT: Path | None = find_font()
_JP_CACHE: dict[int, ImageFont.FreeTypeFont] = {}


def jp_font(size: int) -> ImageFont.FreeTypeFont:
    if JP_FONT is None:
        raise SystemExit("Noto Sans JP not found. Set KANIKEY_FONT to the .otf path.")
    if size not in _JP_CACHE:
        _JP_CACHE[size] = ImageFont.truetype(str(JP_FONT), size)
    return _JP_CACHE[size]


def jp_width(s: str, size: int) -> int:
    return int(round(jp_font(size).getlength(s)))


def jp(im: Image.Image, x: int, y: int, s: str, size: int,
       color: tuple[int, int, int] = TEXT, anchor: str = "ls") -> None:
    ImageDraw.Draw(im).text((x, y), s, font=jp_font(size), fill=color, anchor=anchor)


def jp_fit(im: Image.Image, x: int, y: int, s: str, prefer: int, max_width: int,
           color: tuple[int, int, int], anchor: str = "ls") -> None:
    for size in (prefer, 32, 20):
        if size <= prefer and jp_width(s, size) <= max_width:
            jp(im, x, y, s, size, color, anchor)
            return
    jp(im, x, y, s, 20, WARN, anchor)


# --- screens ------------------------------------------------------------------
def screen_home(outbox: bool = False) -> Image.Image:
    im = Image.new("RGB", (W, H), BG)
    text(im, 6, 2, "KaniKey", 2, 1, BRAND)
    sync = "Synced 14:47"
    icon(im, "sync", 198 - text_width(sync) - 4 - ICON_W, 3, DIM)
    text(im, 198, 5, sync, 1, 1, DIM, "rt")
    text(im, 233, 5, "84%", 1, 1, DIM, "rt")

    text(im, 7, 21, "LEVEL 12", 1, 1, WARN)
    text(im, 80, 21, "you", 1, 1, DIM)
    text(im, 8, 36, "28", 2, 2, TEXT)
    text(im, 164, 36, "5", 2, 2, TEXT)
    text(im, 8, 73, "REVIEWS DUE", 0, 1, DIM)
    text(im, 164, 73, "LESSONS", 0, 1, DIM)

    text(im, 8, 86, "Next review", 1, 1, DIM)
    text(im, 232, 86, "18:00 (+12)", 1, 1, TEXT, "rt")

    counts = [42, 18, 9, 4, 2]
    colors = [ERR, WARN, ACCENT_HI, READING, FAINT]
    total = sum(counts)
    x = 8
    for i, (n, color) in enumerate(zip(counts, colors)):
        w = 232 - x if i == 4 else max(3, (224 * n + total // 2) // total)
        w = min(w, 232 - x)
        if w > 0:
            rect(im, x, 100, w - 1, 3, color)
        x += w
    labels = ["A 42", "G 18", "M 9", "E 4", "B 2"]
    for i, (label, color) in enumerate(zip(labels, colors)):
        x = 8 + i * 43
        rect(im, x, 107, 4, 4, color)
        text(im, x + 5, 106, label, 0, 1, DIM)

    # The outbox line replaces the level line and changes the hint to the
    # attention variant (HomeScreen.cpp:117-138).
    if outbox:
        text(im, 8, 117, "Outbox: 2 need attention", 0, 1, ERR)
        hint = "O: outbox  Enter  S  Esc: menu"
    else:
        text(im, 8, 117, "L12 42% since 03 Feb", 0, 1, FAINT)
        hint = "Enter: reviews  L  S  Esc: menu"
    text(im, 8, 125, hint, 0, 1, FAINT)
    return im


def review_header(im: Image.Image, position: int = 12, total: int = 48) -> None:
    text(im, 6, 3, "REVIEWS", 1, 1, DIM)
    text(im, 62, 5, "SRS 3  86%", 0, 1, FAINT)
    text(im, 233, 5, f"{position}/{total}", 0, 1, FAINT, "rt")
    progress(im, 0, 14, 240, 2, position - 1, total, ACCENT_HI, SURFACE_HI)


def review_subject(im: Image.Image) -> None:
    rect(im, 4, 20, 88, 88, SURFACE, LINE)
    jp(im, 48, 56, "食", 64, TEXT, "mm")
    label = "KANJI L5"
    pill(im, 48 - (text_width(label, 0) + 8) // 2, 92, label, DIM, SURFACE_HI)


def screen_outbox() -> Image.Image:
    """Outbox header with the icon; two attention rows (TNG-03)."""
    im = Image.new("RGB", (W, H), BG)
    text(im, 6, 2, "OUTBOX", 2, 1, BRAND)
    count = "2 need attention"
    icon(im, "outbox", 232 - text_width(count) - 4 - ICON_W, 4, ERR)
    text(im, 232, 5, count, 0, 1, ERR, "rt")
    line(im, 4, 22, 235, 22, LINE)

    rows = [
        ("day L1", "HTTP 422", ERR),
        ("eat L2", "gave up", WARN),
    ]
    for i, (label, reason, reason_color) in enumerate(rows):
        y = 28 + i * 12
        if i == 0:
            rect(im, 4, y - 2, 228, 12, SURFACE_HI)
            rect(im, 4, y - 2, 2, 12, ACCENT)
        text(im, 10, y, label, 0, 1, TEXT if i == 0 else DIM)
        text(im, 228, y, reason, 0, 1, reason_color, "rt")

    text(im, 8, 124, "Enter: review  D: discard  Esc: back", 0, 1, FAINT)
    return im


def screen_review_meaning() -> Image.Image:
    im = Image.new("RGB", (W, H), BG)
    review_header(im)
    review_subject(im)
    pill(im, 100, 20, "MEANING", ACCENT_HI, SURFACE_HI)
    text(im, 100, 38, "Type the meaning", 0, 1, DIM)
    rect(im, 100, 52, 136, 32, SURFACE_HI, LINE)
    text(im, 106, 60, "eat", 1, 1, TEXT)
    rect(im, 106 + text_width("eat", 1), 58, 2, 16, ACCENT)
    text(im, 6, 125, "Enter: check", 0, 1, FAINT)
    text(im, 233, 125, "Esc: quit", 0, 1, FAINT, "rt")
    return im


def screen_review_reading() -> Image.Image:
    im = Image.new("RGB", (W, H), BG)
    review_header(im)
    review_subject(im)
    pill(im, 100, 20, "READING", READING, SURFACE_HI)
    text(im, 100, 38, "Type the reading", 0, 1, DIM)
    rect(im, 100, 52, 136, 32, SURFACE_HI, LINE)
    jp(im, 230, 80, "たべ", 32, TEXT, "rs")
    rect(im, 230, 58, 2, 16, ACCENT)
    text(im, 100, 88, "romaji: tabe", 0, 1, FAINT)
    text(im, 6, 125, "Enter: check", 0, 1, FAINT)
    text(im, 233, 125, "Esc: quit", 0, 1, FAINT, "rt")
    return im


def screen_review_feedback(correct: bool) -> Image.Image:
    im = Image.new("RGB", (W, H), BG)
    review_header(im)
    review_subject(im)
    if correct:
        pill(im, 100, 20, "CORRECT", OK, OK_BG, "check")
        text(im, 100, 38, "You typed", 0, 1, FAINT)
        text(im, 100, 47, "eat", 1, 1, DIM)
        text(im, 100, 65, "Answer", 0, 1, FAINT)
        text(im, 100, 74, "eat / food", 0, 1, OK)
    else:
        pill(im, 100, 20, "NOT QUITE", ERR, ERR_BG, "cross")
        text(im, 100, 38, "You typed", 0, 1, FAINT)
        text(im, 100, 47, "eta", 1, 1, DIM)
        text(im, 100, 65, "Answer", 0, 1, FAINT)
        text(im, 100, 74, "eat / food", 0, 1, ERR)
        # REV-03: the first hint sentence, never a generic "Incorrect meaning".
        text(im, 100, 92, "Picture eating a meal.", 0, 1, FAINT)
    text(im, 6, 125, "Enter: next  Tab: details", 0, 1, FAINT)
    text(im, 233, 125, "Esc: quit", 0, 1, FAINT, "rt")
    return im


def screen_review_check() -> Image.Image:
    """R6 Strict typo warning: pill and message only, no typed/answer rows."""
    im = Image.new("RGB", (W, H), BG)
    review_header(im)
    review_subject(im)
    pill(im, 100, 20, "CHECK", WARN, SURFACE_HI)
    text(im, 100, 40, "Close. Check the", 0, 1, FAINT)
    text(im, 100, 48, "spelling", 0, 1, FAINT)
    text(im, 6, 125, "Enter: edit", 0, 1, FAINT)
    text(im, 233, 125, "Esc: pause", 0, 1, FAINT, "rt")
    return im


def screen_lesson() -> Image.Image:
    im = Image.new("RGB", (W, H), BG)
    text(im, 6, 3, "LESSON", 1, 1, DIM)
    # One header string: card position then mnemonic page (LessonSession.cpp).
    text(im, 233, 5, "2/5 p1/3", 0, 1, DIM, "rt")
    progress(im, 0, 14, 240, 2, 1, 5, ACCENT_HI, SURFACE_HI)

    rect(im, 4, 20, 74, 64, SURFACE, LINE)
    jp(im, 41, 52, "休", 60, TEXT, "mm")
    pill(im, 86, 20, "KANJI L5", DIM, SURFACE_HI)
    text(im, 86, 38, "rest", 1, 1, TEXT)
    jp(im, 86, 80, "きゅう", 32, READING, "ls")

    # LES-06: the topic title above three 6x8 body lines.
    text(im, 5, 87, "Meaning", 0, 1, ACCENT_HI)
    lines = [
        "Meanings: rest. A person leans against",
        "a tree in the shade and takes a break.",
        "Picture yourself resting there.",
    ]
    for i, line_text in enumerate(lines):
        text(im, 5, 96 + i * 9, line_text, 0, 1, DIM)

    text(im, 5, 125, "Q quiz", 0, 1, FAINT)
    text(im, 233, 125, "Enter/Space: next  Esc: list", 0, 1, FAINT, "rt")
    return im


def screen_lesson_quiz() -> Image.Image:
    """Lesson quiz wrong feedback: same pill geometry at the narrower x=86."""
    im = Image.new("RGB", (W, H), BG)
    text(im, 6, 3, "LESSON QUIZ", 1, 1, DIM)
    text(im, 233, 5, "1/2", 0, 1, FAINT, "rt")
    progress(im, 0, 14, 240, 2, 0, 1, ACCENT_HI, SURFACE_HI)

    rect(im, 4, 20, 74, 64, SURFACE, LINE)
    jp(im, 41, 52, "休", 60, TEXT, "mm")
    pill(im, 86, 20, "NOT QUITE", ERR, ERR_BG, "cross")
    text(im, 86, 38, "You typed", 0, 1, FAINT)
    text(im, 86, 47, "eta", 1, 1, DIM)
    text(im, 86, 65, "Answer", 0, 1, FAINT)
    text(im, 86, 74, "rest", 0, 1, ERR)
    text(im, 86, 92, "A person rests by a tree.", 0, 1, FAINT)
    text(im, 5, 125, "Enter: continue  Esc: card", 0, 1, FAINT)
    return im


def screen_summary() -> Image.Image:
    im = Image.new("RGB", (W, H), BG)
    text(im, 6, 3, "SESSION", 1, 1, DIM)
    text(im, 8, 30, "24", 2, 2, TEXT)
    text(im, 84, 30, "92%", 2, 2, TEXT)
    text(im, 164, 30, "12m", 2, 2, TEXT)
    text(im, 8, 66, "REVIEWS", 0, 1, DIM)
    text(im, 84, 66, "ACCURACY", 0, 1, DIM)
    text(im, 164, 66, "TIME", 0, 1, DIM)
    line(im, 8, 77, 231, 77, LINE)

    text(im, 8, 84, "8 items moved up", 0, 1, OK)
    text(im, 8, 98, "3 mistakes", 0, 1, DIM)
    text(im, 232, 98, "All reviews sent", 0, 1, FAINT, "rt")
    text(im, 8, 112, "SRS 3 -> 4  86%", 0, 1, OK)
    text(im, 8, 125, "Enter: home", 0, 1, FAINT)
    return im


def screen_sync() -> Image.Image:
    im = Image.new("RGB", (W, H), BG)
    text(im, 6, 2, "KaniKey", 2, 1, BRAND)
    text(im, 233, 5, "84%", 1, 1, DIM, "rt")
    title = "Syncing..."
    text(im, 8, 24, title, 2, 1, TEXT)
    icon(im, "sync", 8 + text_width(title, 2) + 6, 27, TEXT)
    text(im, 233, 32, "5/7", 0, 1, FAINT, "rt")

    rows = [
        ("Wi-Fi", "192.168.1.42", "ok", OK, TEXT),
        ("Outbox", "sent", "ok", OK, TEXT),
        ("Account", "you L12", "ok", OK, TEXT),
        ("Summary", "28 due, 5 lessons", "ok", OK, TEXT),
        ("Level", "L12 since 03 Feb", "ok", OK, TEXT),
        ("SRS", "fetching", "..", ACCENT_HI, TEXT),
        ("Critical", "", "--", FAINT, FAINT),
    ]
    for i, (label, value, state, status_color, value_color) in enumerate(rows):
        y = 42 + i * 10
        text(im, 8, y, label, 0, 1, DIM)
        if value:
            text(im, 64, y, value, 0, 1, value_color)
        text(im, 233, y, state, 0, 1, status_color, "rt")

    progress(im, 8, 113, 224, 3, 5, 7, ACCENT_HI, SURFACE_HI)
    text(im, 8, 125, "Esc: cancel", 0, 1, FAINT)
    return im


def _settings_list(im: Image.Image, rows: list[tuple[str, str]], title: str,
                   selected: int, visible: int = 7) -> None:
    text(im, 6, 2, "KaniKey", 2, 1, BRAND)
    text(im, 7, 22, title, 1, 1, DIM)
    text(im, 232, 5, "Esc: back", 0, 1, FAINT, "rt")
    line(im, 4, 36, 235, 36, LINE)

    first, drawn = list_window(len(rows), selected, visible)
    for r in range(drawn):
        i = first + r
        label, value = rows[i]
        y = 42 + r * 11
        focus = i == selected
        if focus:
            rect(im, 4, y - 2, 228, 11, SURFACE_HI)
            rect(im, 4, y - 2, 2, 11, ACCENT)
        text(im, 10, y, label, 0, 1, TEXT if focus else DIM)
        text(im, 228, y, value, 0, 1, ACCENT_HI if focus else FAINT, "rt")

    scrollbar(im, 235, 42, 66, len(rows), visible, first)


def screen_settings() -> Image.Image:
    im = Image.new("RGB", (W, H), BG)
    rows = [
        ("Reviews", "12 settings"),
        ("Lessons", "6 settings"),
        ("Study", "5 settings"),
        ("Sync & data", "6 settings"),
        ("Display & power", "6 settings"),
        ("Reminders & sound", "7 settings"),
        ("Account", "token set"),
        ("System", "3 settings"),
    ]
    _settings_list(im, rows, "settings", 0)
    text(im, 8, 123, "Enter: open  Esc: back", 0, 1, FAINT)
    return im


def screen_settings_reviews() -> Image.Image:
    """Settings > Reviews scrolled to the live review options (R6-R12)."""
    im = Image.new("RGB", (W, H), BG)
    rows = [
        ("Batch size", "20"),
        ("Order", "Account preference"),
        ("Type order", "Off"),
        ("First question", "Meaning"),
        ("Back-to-back", "On"),
        ("Typos", "WaniKani"),
        ("Wrong answer actions", "Off"),
        ("Auto-advance on correct", "Off"),
        ("Details on wrong", "Off"),
        ("Header info", "SRS+accuracy"),
        ("Prioritize critical", "Off"),
        ("Submit reviews", "At session end"),
    ]
    _settings_list(im, rows, "Reviews", 9)
    text(im, 8, 123, "SRS and accuracy in the header", 0, 1, FAINT)
    return im


def screen_wifi_scan() -> Image.Image:
    im = Image.new("RGB", (W, H), BG)
    text(im, 6, 2, "KaniKey", 2, 1, BRAND)
    subtitle = "Wi-Fi · 2.4 GHz networks"
    text(im, 7, 22, subtitle, 1, 1, DIM)
    icon(im, "wifi", 7 + text_width(subtitle) + 4, 20, DIM)
    text(im, 232, 5, "Esc: back", 0, 1, FAINT, "rt")
    line(im, 4, 36, 235, 36, LINE)

    rows = [
        ("dev-net", "||||", "WPA2"),
        ("dev-net-guest", "|||", "WPA2"),
        ("Studio 2.4", "|||", "WPA2/3"),
        ("cafe-wifi", "||", "WPA2"),
        ("Lobby", "|", "WPA2"),
        ("OldRouter", "|", "WEP"),
        ("+ Enter network name", "", "hidden"),
    ]
    visible = 6
    selected = 0
    first, drawn = list_window(len(rows), selected, visible)
    for r in range(drawn):
        i = first + r
        ssid, signal, security = rows[i]
        y = 42 + r * 11
        focus = i == selected
        if focus:
            rect(im, 4, y - 2, 228, 11, SURFACE_HI)
            rect(im, 4, y - 2, 2, 11, ACCENT)
        manual = i == len(rows) - 1
        label_color = (ACCENT_HI if focus else DIM) if manual else (TEXT if focus else DIM)
        text(im, 9, y, clipped(ssid, 142), 0, 1, label_color)
        if manual:
            text(im, 228, y, "hidden", 0, 1, FAINT, "rt")
        else:
            text(im, 228, y, security, 0, 1, ACCENT_HI if focus else FAINT, "rt")
            if signal:
                text(im, 228 - text_width(security) - 5, y, signal, 0, 1,
                     TEXT if focus else DIM, "rt")
    scrollbar(im, 235, 42, 66, len(rows), visible, first)
    text(im, 8, 124, "Enter: choose  R: rescan", 0, 1, FAINT)
    return im


def screen_wifi_password() -> Image.Image:
    im = Image.new("RGB", (W, H), BG)
    text(im, 6, 2, "KaniKey", 2, 1, BRAND)
    subtitle = "Wi-Fi · password"
    text(im, 7, 22, subtitle, 1, 1, DIM)
    icon(im, "wifi", 7 + text_width(subtitle) + 4, 20, DIM)
    text(im, 232, 5, "Esc: back", 0, 1, FAINT, "rt")
    line(im, 4, 36, 235, 36, LINE)

    text(im, 8, 44, "Network", 0, 1, DIM)
    text(im, 8, 55, "dev-net", 1, 1, TEXT)
    text(im, 8, 78, "Password", 0, 1, DIM)
    text(im, 232, 78, "Tab: show", 0, 1, ACCENT_HI, "rt")

    rect(im, 6, 88, 228, 18, SURFACE, LINE)
    text(im, 10, 93, "**********", 1, 1, TEXT)
    rect(im, 10 + text_width("**********") + 1, 93, 2, 10, ACCENT_HI)
    text(im, 8, 112, "8-63 characters", 0, 1, FAINT)
    text(im, 8, 124, "Enter: connect  Esc: back", 0, 1, FAINT)
    return im


def screen_setup() -> Image.Image:
    im = Image.new("RGB", (W, H), BG)
    text(im, 6, 2, "KaniKey", 2, 1, BRAND)
    text(im, 7, 22, "setup", 1, 1, DIM)
    text(im, 233, 5, "84%", 1, 1, DIM, "rt")

    text(im, 8, 38, "Join this Wi-Fi", 1, 1, DIM)
    icon(im, "wifi", 8 + text_width("Join this Wi-Fi") + 5, 36, DIM)
    text(im, 8, 52, "KaniKey-4F2C", 2, 1, TEXT)
    text(im, 8, 74, "Password", 1, 1, DIM)
    text(im, 8, 86, "mango-tide", 1, 1, TEXT)
    text(im, 8, 100, "Open", 1, 1, DIM)
    text(im, 8, 110, "http://192.168.4.1/", 1, 1, ACCENT_HI)
    text(im, 8, 125, "Esc: cancel", 0, 1, FAINT)
    return im


def screen_setup_station() -> Image.Image:
    im = Image.new("RGB", (W, H), BG)
    text(im, 6, 2, "KaniKey", 2, 1, BRAND)
    text(im, 7, 22, "setup · Wi-Fi", 1, 1, DIM)
    text(im, 233, 5, "84%", 1, 1, DIM, "rt")

    text(im, 8, 36, "Connected", 1, 1, OK)
    icon(im, "wifi", 8 + text_width("Connected") + 5, 34, OK)
    text(im, 8, 48, "dev-net", 2, 1, TEXT)
    text(im, 8, 66, "Open on this network", 1, 1, DIM)
    text(im, 8, 76, "http://192.168.1.42/", 1, 1, ACCENT_HI)
    text(im, 8, 88, "Page code 123456", 2, 1, TEXT)
    text(im, 8, 106, "Token OK: dev L12", 1, 1, OK)
    # ONB-01 status pair. This is the message-empty branch; a portal notice
    # replaces the Font line on a live station screen.
    text(im, 8, 113, "Font: 7269 glyphs, 64/32/20 px", 0, 1, FAINT)
    text(im, 8, 121, "Rad: 498 radicals", 0, 1, FAINT)
    text(im, 8, 128, "Enter code on page  Esc: cancel", 0, 1, FAINT)
    return im


def screen_about() -> Image.Image:
    im = Image.new("RGB", (W, H), BG)
    text(im, 6, 2, "KaniKey", 2, 1, BRAND)
    text(im, 7, 22, "about · unofficial", 1, 1, DIM)
    text(im, 232, 5, "Esc: back", 0, 1, FAINT, "rt")
    text(im, 7, 34, "KaniKey 0.1.0 (ota_0)", 1, 1, TEXT)

    lines = [
        ("Build Nov 19 2026 14:32", FAINT, 50),
        ("Font: 7269 glyphs, 64/32/20 px", OK, 60),
        ("Rad: 498 radicals", OK, 70),
        ("Cache: 20 batch, 28 due", DIM, 80),
        ("Last sync: 14:47", DIM, 90),
        ("Wi-Fi: dev-net 192.168.1.42", DIM, 100),
        ("Token: configured", DIM, 110),
        ("Zone: Europe/Berlin", FAINT, 118),
        ("Heap: 145000 free / 110000 largest", FAINT, 126),
    ]
    for label, color, y in lines:
        text(im, 7, y, label, 0, 1, color)
        badge = ("sd" if label.startswith("Font: ") or label.startswith("Rad: ")
                 else "key" if label.startswith("Token: ") else None)
        if badge and 7 + text_width(label) + 4 + ICON_W <= 232:
            icon(im, badge, 7 + text_width(label) + 4, y - 2, color)
    return im


SCREENS = [
    ("home", screen_home),
    ("home-outbox", lambda: screen_home(True)),
    ("outbox", screen_outbox),
    ("review-meaning", screen_review_meaning),
    ("review-reading", screen_review_reading),
    ("review-correct", lambda: screen_review_feedback(True)),
    ("review-wrong", lambda: screen_review_feedback(False)),
    ("review-check", screen_review_check),
    ("lesson", screen_lesson),
    ("lesson-quiz", screen_lesson_quiz),
    ("summary", screen_summary),
    ("sync", screen_sync),
    ("settings", screen_settings),
    ("settings-reviews", screen_settings_reviews),
    ("setup", screen_setup),
    ("setup-station", screen_setup_station),
    ("wifi-scan", screen_wifi_scan),
    ("wifi-password", screen_wifi_password),
    ("about", screen_about),
]


def build() -> list[tuple[str, Image.Image]]:
    if JP_FONT is None:
        raise SystemExit("Noto Sans JP not found. Set KANIKEY_FONT to the .otf path.")
    return [(name, quantize(fn())) for name, fn in SCREENS]


def save(name: str, im: Image.Image) -> Path:
    path = OUT / f"{name}.png"
    im.resize((W * SCALE, H * SCALE), Image.Resampling.NEAREST).save(path)
    return path


def overview(paths: list[tuple[str, Path]]) -> Path:
    cols = 3
    rows = (len(paths) + cols - 1) // cols
    cell_w, cell_h = W * 2, H * 2
    gap = 16
    top = 44
    sheet = Image.new("RGB", (cols * cell_w + (cols + 1) * gap,
                             top + rows * cell_h + (rows + 1) * gap), BG)
    text(sheet, gap, 8, "KaniKey - screen renders (240x135, 2x here)", 2, 2, TEXT)
    for i, (label, path) in enumerate(paths):
        im = Image.open(path).resize((cell_w, cell_h), Image.Resampling.NEAREST)
        cx = gap + (i % cols) * (cell_w + gap)
        cy = top + gap + (i // cols) * (cell_h + gap)
        sheet.paste(im, (cx, cy))
        text(sheet, cx, cy + cell_h + 3, label, 0, 1, DIM)
    out = OUT / "overview.png"
    sheet.save(out)
    return out


def check_palette(path: Path) -> None:
    im = Image.open(path).convert("RGB")
    bad = set(_pixels(im)) - ALLOWED
    if bad:
        raise SystemExit(f"{path}: {len(bad)} colours outside the RGB332 Theme set: "
                         f"{sorted(bad)[:4]}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true",
                        help="verify the committed PNGs use only Theme colours")
    args = parser.parse_args()

    if args.check:
        expected = {f"{name}.png" for name, _ in SCREENS} | {"overview.png"}
        missing = expected - {p.name for p in OUT.glob("*.png")}
        if missing:
            raise SystemExit(f"missing images: {', '.join(sorted(missing))}")
        if JP_FONT is not None:
            for name, im in build():
                bad = set(_pixels(im)) - ALLOWED
                if bad:
                    raise SystemExit(f"{name}: generated colours outside the Theme set")
        for path in sorted(OUT.glob("*.png")):
            check_palette(path)
            if path.name != "overview.png":
                with Image.open(path) as im:
                    if im.size != (W * SCALE, H * SCALE):
                        raise SystemExit(f"{path}: expected {W * SCALE}x{H * SCALE}")
        print(f"ok: {len(expected)} renders use the Theme RGB332 palette "
              f"(surface #{PALETTE['surface'][0]:02X}{PALETTE['surface'][1]:02X}"
              f"{PALETTE['surface'][2]:02X}, surface-hi "
              f"#{PALETTE['surface_hi'][0]:02X}{PALETTE['surface_hi'][1]:02X}"
              f"{PALETTE['surface_hi'][2]:02X})")
        return

    OUT.mkdir(parents=True, exist_ok=True)
    paths: list[tuple[str, Path]] = []
    for name, im in build():
        path = save(name, im)
        paths.append((name.replace("-", " "), path))
        print(path)
    print(overview(paths))


if __name__ == "__main__":
    main()

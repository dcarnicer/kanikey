#!/usr/bin/env python3
"""Render the KaniKey design mockups at the Cardputer ADV's 240x135 panel.

The geometry and type sizes are the 1x design values; the individual PNGs are
nearest-neighbour 4x upscales and the contact sheet is 2x, so the pixel grid
stays honest. The UI font is Noto Sans JP, the same family Tangocho pre-renders
for its firmware; the kanji/radical shapes here stand in for the device's glyph
pack.

    python3 tools/mockups/render.py

Output: docs/screens/*.png (and overview.png).
"""
from __future__ import annotations

import os
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs" / "screens"
# Reuse Tangocho's downloaded Noto cache by default; override with KANIKEY_FONT.
DEFAULT_FONT = Path.home() / "code/tangocho/tools/pack-builder/cache/noto/NotoSansJP-Regular.otf"
FONT = Path(os.environ.get("KANIKEY_FONT", DEFAULT_FONT))
FONT_MEDIUM = Path(os.environ.get(
    "KANIKEY_FONT_MEDIUM",
    str(FONT).replace("-Regular", "-Medium"),
))

W, H = 240, 135
SCALE = 4

# --- palette -----------------------------------------------------------------
BG = "#0B0D10"
SURFACE = "#151A22"
SURFACE_HI = "#202733"
LINE = "#33404F"
TEXT = "#F4F7FB"
DIM = "#9BA8BA"
FAINT = "#647184"
ACCENT = "#6C8CFF"
ACCENT_HI = "#9DB2FF"
BRAND = "#FF7A59"
READING = "#5EEAD4"
OK = "#6EE7A8"
OK_BG = "#123326"
ERR = "#FF6B6B"
ERR_BG = "#351A1C"
WARN = "#FBBF24"
GOLD = "#F5C451"

FONTS: dict[tuple[int, bool], ImageFont.FreeTypeFont] = {}


def font(size: int, medium: bool = False) -> ImageFont.FreeTypeFont:
    key = (size, medium)
    if key not in FONTS:
        FONTS[key] = ImageFont.truetype(str(FONT_MEDIUM if medium else FONT), size)
    return FONTS[key]


def text(d: ImageDraw.ImageDraw, xy: tuple[int, int], s: str, size: int, color: str,
         anchor: str = "la", medium: bool = False) -> None:
    d.text(xy, s, font=font(size, medium), fill=color, anchor=anchor)


def tw(d: ImageDraw.ImageDraw, s: str, size: int, medium: bool = False) -> int:
    return int(round(d.textlength(s, font=font(size, medium))))


def fit_size(d: ImageDraw.ImageDraw, s: str, max_w: int, sizes: list[int],
             medium: bool = False) -> int:
    for size in sizes:
        if tw(d, s, size, medium) <= max_w:
            return size
    return sizes[-1]


def rect(d: ImageDraw.ImageDraw, x: int, y: int, w: int, h: int, fill: str | None = None,
         outline: str | None = None) -> None:
    d.rectangle((x, y, x + w - 1, y + h - 1), fill=fill, outline=outline)


def pill(d: ImageDraw.ImageDraw, x: int, y: int, label: str, fg: str, bg: str,
         size: int = 9, pad: int = 5) -> int:
    w = tw(d, label, size) + 2 * pad
    rect(d, x, y, w, 13, bg)
    text(d, (x + pad, y + 2), label, size, fg)
    return w


def keycap(d: ImageDraw.ImageDraw, x: int, y: int, label: str, size: int = 8) -> int:
    w = max(12, tw(d, label, size) + 6)
    rect(d, x, y, w, 11, SURFACE_HI, LINE)
    text(d, (x + w // 2, y + 1), label, size, TEXT, anchor="ma")
    return w


def hints(d: ImageDraw.ImageDraw, left: list[tuple[str, str]], right: list[tuple[str, str]]) -> None:
    y = 124
    x = 5
    for key, word in left:
        x += keycap(d, x, y, key) + 3
        text(d, (x, y + 1), word, 9, DIM)
        x += tw(d, word, 9) + 9
    x = W - 5
    for key, word in reversed(right):
        w = tw(d, word, 9)
        text(d, (x - w, y + 1), word, 9, DIM)
        x -= w + 3
        kw = max(12, tw(d, key, 8) + 6)
        keycap(d, x - kw, y, key)
        x -= kw + 9


def battery(d: ImageDraw.ImageDraw, x: int, y: int, pct: int) -> None:
    rect(d, x, y, 13, 7, None, DIM)
    rect(d, x + 13, y + 2, 2, 3, DIM)
    fill_w = max(1, round(10 * pct / 100))
    color = ERR if pct < 10 else WARN if pct < 20 else OK
    rect(d, x + 2, y + 2, fill_w, 3, color)


def wifi(d: ImageDraw.ImageDraw, x: int, y: int, color: str = DIM) -> None:
    d.arc((x, y, x + 11, y + 11), 225, 315, fill=color, width=1)
    d.arc((x + 2, y + 3, x + 9, y + 10), 225, 315, fill=color, width=1)
    d.ellipse((x + 4, y + 7, x + 6, y + 9), fill=color)


def check(d: ImageDraw.ImageDraw, x: int, y: int, color: str = OK) -> None:
    d.line((x, y + 4, x + 3, y + 7), fill=color, width=2)
    d.line((x + 3, y + 7, x + 9, y + 1), fill=color, width=2)


def cross(d: ImageDraw.ImageDraw, x: int, y: int, color: str = ERR) -> None:
    d.line((x, y, x + 8, y + 8), fill=color, width=2)
    d.line((x + 8, y, x, y + 8), fill=color, width=2)


def chevron(d: ImageDraw.ImageDraw, x: int, y: int, color: str = FAINT) -> None:
    d.line((x, y, x + 3, y + 3), fill=color, width=1)
    d.line((x + 3, y + 3, x, y + 6), fill=color, width=1)


def progress(d: ImageDraw.ImageDraw, x: int, y: int, w: int, h: int, done: int, total: int,
             fill: str = ACCENT, track: str = SURFACE_HI) -> None:
    rect(d, x, y, w, h, track)
    if total > 0:
        rect(d, x, y, max(1, round(w * done / total)), h, fill)


def header(d: ImageDraw.ImageDraw, title: str, right: str | None = None,
           pct: int = 84, done: int = 0, total: int = 0, brand: bool = False) -> None:
    if brand:
        text(d, (6, 2), "KaniKey", 15, BRAND, medium=True)
    else:
        text(d, (6, 3), title, 11, DIM, medium=True)
    if right:
        text(d, (W - 50, 4), right, 9, DIM, anchor="ra")
    wifi(d, W - 43, 5)
    battery(d, W - 20, 5, pct)
    if total > 0:
        progress(d, 0, 14, W, 2, done, total)
    else:
        rect(d, 0, 15, W, 1, LINE)


def wrap(d: ImageDraw.ImageDraw, s: str, size: int, max_w: int) -> list[str]:
    words = s.split(" ")
    lines: list[str] = []
    cur = ""
    for word in words:
        cand = (cur + " " + word).strip()
        if tw(d, cand, size) <= max_w or not cur:
            cur = cand
        else:
            lines.append(cur)
            cur = word
    if cur:
        lines.append(cur)
    return lines


def panel(d: ImageDraw.ImageDraw, x: int, y: int, w: int, h: int) -> None:
    rect(d, x, y, w, h, SURFACE, LINE)


# --- screens -----------------------------------------------------------------
def screen_home() -> Image.Image:
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    header(d, "", pct=84, brand=True)

    text(d, (8, 22), "LEVEL 12", 9, GOLD, medium=True)
    text(d, (66, 21), "you", 10, DIM)
    text(d, (8, 35), "28", 40, TEXT, medium=True)
    text(d, (54, 55), "REVIEWS DUE", 8, DIM)
    text(d, (150, 35), "5", 32, TEXT, medium=True)
    text(d, (168, 55), "LESSONS", 8, DIM)

    text(d, (8, 80), "Next review", 9, FAINT)
    text(d, (70, 79), "18:00", 12, TEXT, medium=True)
    text(d, (105, 80), "· in 42 min", 9, DIM)

    # SRS stage distribution: apprentice, guru, master, enlightened, burned
    counts = [42, 18, 9, 4, 2]
    colors = [ERR, WARN, ACCENT, READING, FAINT]
    x = 8
    total = sum(counts)
    for n, c in zip(counts, colors):
        w = max(3, round(224 * n / total))
        rect(d, x, 101, w - 1, 3, c)
        x += w
    labels = ["A 42", "G 18", "M 9", "E 4", "B 2"]
    x = 8
    for label, c in zip(labels, colors):
        rect(d, x, 110, 3, 3, c)
        text(d, (x + 5, 108), label, 8, DIM)
        x += 43

    hints(d, [("Enter", "reviews"), ("L", "lessons"), ("S", "sync")], [])
    return im


def prompt_panel(d: ImageDraw.ImageDraw, char_text: str, kind: str, level: int,
                 size: int = 64) -> None:
    panel(d, 4, 20, 88, 88)
    # Centre the prompt in the upper part of the box, leaving room for its pill.
    cy = 57
    text(d, (48, cy), char_text, size, TEXT, anchor="mm")
    label = f"{kind} · L{level}"
    w = tw(d, label, 8) + 10
    pill(d, 48 - w // 2, 92, label, DIM, SURFACE_HI, size=8)


def screen_review(kind: str = "MEANING", answer: str = "eat", romaji: str | None = None) -> Image.Image:
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    header(d, "REVIEWS", "12/48", done=12, total=48)
    prompt_panel(d, "食", "KANJI", 5)

    pill(d, 100, 20, kind, ACCENT if kind == "MEANING" else READING, SURFACE_HI, size=9)

    text(d, (100, 39), "Type the meaning" if kind == "MEANING" else "Type the reading",
         9, DIM)
    rect(d, 100, 52, 136, 32, SURFACE_HI, LINE)
    text(d, (106, 58), answer, 20, TEXT)
    caret_x = 106 + tw(d, answer, 20) + 2
    rect(d, caret_x, 57, 2, 21, ACCENT)
    if romaji:
        text(d, (100, 88), f"romaji: {romaji}", 9, FAINT)
    else:
        text(d, (100, 88), "Enter to check", 9, FAINT)
    text(d, (100, 104), "Tab: details", 8, FAINT)

    hints(d, [("Enter", "check")], [("Esc", "pause")])
    return im


def screen_feedback(correct: bool) -> Image.Image:
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    header(d, "REVIEWS", "12/48", done=12, total=48)
    prompt_panel(d, "食", "KANJI", 5)

    color, bg = (OK, OK_BG) if correct else (ERR, ERR_BG)
    label = "CORRECT" if correct else "NOT QUITE"
    pill(d, 100, 20, label, color, bg, size=9)
    (check if correct else cross)(d, 100 + tw(d, label, 9) + 11, 23, color)

    text(d, (100, 38), "You typed", 8, FAINT)
    text(d, (100, 48), "eat" if correct else "eta", 14, DIM)

    text(d, (100, 68), "Meaning", 8, FAINT)
    text(d, (100, 78), "eat", 18, color, medium=True)
    text(d, (100, 100), "Reading  たべる", 9, READING)

    hints(d, [("Enter", "next"), ("Tab", "details")], [])
    return im


def screen_lesson() -> Image.Image:
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    header(d, "LESSON", "2/5", done=2, total=5)

    panel(d, 4, 20, 74, 60)
    text(d, (41, 50), "休", 48, TEXT, anchor="mm")
    pill(d, 84, 20, "KANJI · L5", DIM, SURFACE_HI, size=8)
    text(d, (84, 36), "rest", 20, TEXT, medium=True)
    text(d, (84, 58), "きゅう · やす.む", 12, READING)
    text(d, (84, 72), "person + tree", 8, FAINT)

    rect(d, 4, 86, 232, 34, SURFACE, LINE)
    mnemonic = ("A person leans against a tree in the shade. That is rest. "
                "Picture yourself stopping there for a moment.")
    lines = wrap(d, mnemonic, 9, 224)[:3]
    for i, line in enumerate(lines):
        text(d, (10, 90 + i * 10), line, 9, DIM)
    text(d, (232, 116), "p1/3", 8, FAINT, anchor="ra")

    hints(d, [("Space", "next page"), ("Enter", "start")], [("Esc", "back")])
    return im


def screen_summary() -> Image.Image:
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    header(d, "SESSION", "done", pct=84)

    cols = [("24", "REVIEWS"), ("92%", "ACCURACY"), ("12m", "TIME")]
    xs = [8, 84, 160]
    for (value, label), x in zip(cols, xs):
        text(d, (x, 28), value, 28, TEXT, medium=True)
        text(d, (x, 60), label, 8, DIM)
    rect(d, 8, 76, 224, 1, LINE)

    text(d, (8, 84), "8 items moved up", 11, OK)
    text(d, (8, 100), "Next review  18:00", 11, DIM)
    text(d, (8, 114), "All caught up for now.", 9, FAINT)

    hints(d, [("Enter", "done"), ("R", "review more")], [])
    return im


def screen_sync() -> Image.Image:
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    header(d, "SYNC", pct=84)

    rows = [
        ("Account", "you · L12", "ok"),
        ("Summary", "28 due · 5 lessons", "ok"),
        ("Reviews", "42 assignments", "ok"),
        ("Subjects", "64 / 128", "busy"),
        ("Outbox", "3 to send", "clock"),
    ]
    y = 22
    for label, value, state in rows:
        text(d, (10, y), label, 10, TEXT)
        text(d, (78, y), value, 9, DIM)
        if state == "ok":
            check(d, 220, y + 1, OK)
        elif state == "busy":
            d.arc((220, y, 230, y + 10), 30, 300, fill=ACCENT, width=2)
        else:
            d.ellipse((221, y + 2, 227, y + 8), outline=WARN, width=1)
            d.line((224, y + 3, 224, y + 6), fill=WARN)
        y += 16

    progress(d, 10, 114, 220, 3, 64, 128)
    text(d, (10, 104), "Fetching subjects…", 9, DIM)
    hints(d, [], [("Esc", "cancel")])
    return im


def screen_settings() -> Image.Image:
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    header(d, "SETTINGS", pct=84)

    rows = [
        ("Account", "you · L12"),
        ("Study", "Meaning first"),
        ("Sync", "2 min ago"),
        ("Device", "Wi-Fi · 84%"),
        ("About", "v0.1 design"),
    ]
    y = 22
    for i, (label, value) in enumerate(rows):
        rect(d, 4, y, 232, 18, SURFACE if i else ACCENT, None if i else None)
        text(d, (10, y + 4), label, 10, TEXT if i else "#0B0D10", medium=True)
        text(d, (224, y + 5), value, 9, DIM if i else "#0B0D10", anchor="ra")
        chevron(d, 230, y + 6, FAINT if i else "#0B0D10")
        y += 20

    hints(d, [("↑↓", "move"), ("Enter", "open")], [("Esc", "back")])
    return im


def screen_setup() -> Image.Image:
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    header(d, "WELCOME", pct=84)

    text(d, (8, 26), "KaniKey", 26, BRAND, medium=True)
    text(d, (8, 54), "an unofficial WaniKani companion", 9, DIM)
    text(d, (8, 68), "for the M5Stack Cardputer ADV", 9, FAINT)

    steps = ["Wi-Fi", "API token", "Clock", "First sync"]
    x = 8
    for i, step in enumerate(steps):
        rect(d, x, 90, 7, 7, None, ACCENT if i == 0 else LINE)
        if i == 0:
            rect(d, x + 2, 92, 3, 3, ACCENT)
        text(d, (x + 11, 88), step, 8, TEXT if i == 0 else DIM)
        x += 57

    text(d, (8, 108), "Step 1: choose your Wi-Fi network", 9, DIM)
    hints(d, [("Enter", "start")], [("Esc", "later")])
    return im


def save(name: str, im: Image.Image) -> Path:
    path = OUT / f"{name}.png"
    im.resize((W * SCALE, H * SCALE), Image.Resampling.NEAREST).save(path)
    return path


def overview(paths: list[tuple[str, Path]]) -> Path:
    cols, rows = 2, 4
    cell_w, cell_h = 480, 300
    gap = 16
    top = 38
    sheet = Image.new("RGB", (cols * cell_w + (cols + 1) * gap,
                             rows * cell_h + (rows + 1) * gap + top), "#07080A")
    d = ImageDraw.Draw(sheet)
    d.text((gap, 10), "KaniKey · screen directions (240×135, 2× here)", font=font(18),
           fill="#F4F7FB")
    for i, (label, path) in enumerate(paths):
        im = Image.open(path).resize((cell_w, cell_h), Image.Resampling.LANCZOS)
        cx = gap + (i % cols) * (cell_w + gap)
        cy = top + gap + (i // cols) * (cell_h + gap)
        sheet.paste(im, (cx, cy))
        d.text((cx, cy + cell_h + 2), label, font=font(13), fill="#9BA8BA")
    out = OUT / "overview.png"
    sheet.save(out)
    return out


def main() -> None:
    if not FONT.exists():
        raise SystemExit(
            f"Noto Sans JP not found at {FONT}. Set KANIKEY_FONT to the .otf path."
        )
    OUT.mkdir(parents=True, exist_ok=True)
    screens = [
        ("home", screen_home),
        ("review-meaning", lambda: screen_review("MEANING", "eat")),
        ("review-reading", lambda: screen_review("READING", "たべ", "tabe")),
        ("review-correct", lambda: screen_feedback(True)),
        ("review-wrong", lambda: screen_feedback(False)),
        ("lesson", screen_lesson),
        ("summary", screen_summary),
        ("sync", screen_sync),
        ("settings", screen_settings),
        ("setup", screen_setup),
    ]
    paths: list[tuple[str, Path]] = []
    for name, fn in screens:
        path = save(name, fn())
        paths.append((name.replace("-", " "), path))
        print(path)
    print(overview(paths))


if __name__ == "__main__":
    main()

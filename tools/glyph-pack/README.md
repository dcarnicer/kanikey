# Visual pack generator

`build.py` makes the `glyphs.kgp` file that the firmware reads from
`/kanikey/glyphs.kgp` on the microSD card. The pack is generated locally and is
never committed: it is derived from WaniKani subjects and Noto Sans JP.

## Quick start

From the repository root:

```sh
# Small pack for UI work: the given characters plus kana.
uv run --with pillow python3 tools/glyph-pack/build.py \
    --chars "食休日本語" --out dist/glyphs-test.kgp

# Full kanji/kana pack without a token, from Tangocho's local WaniKani cache.
uv run --with pillow python3 tools/glyph-pack/build.py \
    --tangocho --out dist/glyphs-full.kgp

# Full pack from a WaniKani account, including radical SVG images.
WANIKANI_TOKEN=... uv run --with pillow --with cairosvg \
    python3 tools/glyph-pack/build.py --out dist/glyphs.kgp
```

Upload it with the Wi-Fi path (recommended for multi-megabyte files):

```sh
uv run --with pyserial python3 tools/device.py push \
    dist/glyphs-full.kgp /kanikey/glyphs.kgp
```

The token is read from `WANIKANI_TOKEN`, kept in memory and never written to
disk. `--tangocho` does not contact the API at all.

## Pack format (`KKGP` v1)

All integers are little-endian.

### Header (32 bytes)

| Offset | Type | Field |
|---:|---|---|
| 0 | `char[4]` | Magic `KKGP` |
| 4 | `uint16` | Version, `1` |
| 6 | `uint16` | Header size, `32` |
| 8 | `uint16` | Glyph count |
| 10 | `uint16` | Radical count |
| 12 | `uint32` | Glyph/radical table offset (`32`) |
| 16 | `uint32` | Payload data offset |
| 20 | `uint32` | Payload data length |
| 24 | `uint32` | CRC-32 of the table + payload |
| 28 | `uint32` | Reserved, `0` |

### Glyph entry (20 bytes, sorted by `(codepoint, size)`)

| Offset | Type | Field |
|---:|---|---|
| 0 | `uint32` | Unicode code point |
| 4 | `uint32` | Payload offset |
| 8 | `uint32` | Payload length |
| 12 | `uint8` | Bitmap width |
| 13 | `uint8` | Bitmap height |
| 14 | `uint8` | Font pixel size (64, 32 or 20) |
| 15 | `uint8` | Advance width |
| 16 | `int8` | Bitmap left from the pen position |
| 17 | `int8` | Bitmap top from the baseline (negative above it) |
| 18 | `uint8` | Encoding, `1` = 4bpp RLE |
| 19 | `uint8` | Reserved |

### Radical entry (16 bytes, sorted by subject id)

| Offset | Type | Field |
|---:|---|---|
| 0 | `uint32` | WaniKani subject id |
| 4 | `uint32` | Payload offset |
| 8 | `uint32` | Payload length |
| 12 | `uint8` | Bitmap width |
| 13 | `uint8` | Bitmap height |
| 14 | `uint8` | Encoding, `1` = 4bpp RLE |
| 15 | `uint8` | Reserved |

### RLE payload

One byte per run:

- high nibble: run length minus one (`0..15`, so 1–16 pixels);
- low nibble: alpha (`0..15`).

Pixels are row-major, width × height. The bitmap is the tight glyph bounding
box; `xoff`/`yoff` place it relative to the pen and baseline.

The firmware (`firmware/src/platform/GlyphPack.cpp`) binary-searches the entry
table, seeks to one payload, decodes it in 128-byte reads and blends each
pixel over the destination color. It does not load the pack into RAM.

## Sizes

The tokenless full pack (2,026 WaniKani kanji + kana + `々〜・〇`) has 2,207
characters, 6,621 glyphs and is about 2.9 MB. The test pack with 203 characters
is 189,805 B. The entry table is 20 bytes per glyph; the rest is RLE payloads.

## Notes

- Noto Sans JP is OFL; the generator uses
  `tools/pack-builder/cache/noto/NotoSansJP-Regular.otf` from Tangocho by
  default. Pass `--font` for another font.
- Radical images come from the URLs in the user's own WaniKani subject
  response. They are personal, not redistributable.
- The current firmware has radical entries in the format but no radical
  renderer yet; the tokenless pack has no radical images at all.
- The generated `dist/` directory is in `.gitignore`.

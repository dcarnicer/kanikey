# Visual pack generator

`build.py` makes the `glyphs.kgp` file that the firmware reads from
`/kanikey/glyphs.kgp` on the microSD card. There are two kinds of pack:

- the public **font pack** (kind 1): kana plus the fixed JIS X 0208 set,
  rendered from Noto Sans JP. It contains no WaniKani content and is published
  with every release.
- the personal **radical pack** (kind 2): rasterized from the owner's WaniKani
  subject images. It is never published.

Generated packs are not committed.

## Quick start

From the repository root:

```sh
# Public font-only pack: no token, no radical images.
uv run --with pillow python3 tools/glyph-pack/build.py --split dist

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

## Release font pack

`.github/workflows/release.yml` builds the public pack on every `v*` tag and
attaches it as `glyphs.kgp`. The step does not set `WANIKANI_TOKEN` and passes
`--token ""`, so it writes `dist/glyphs.kgp` and no `radicals.kgp`. The workflow
verifies that the file is a v2 kind-1 pack, then writes the OTA manifest with
`--pack dist/glyphs.kgp` and includes the pack in `SHA256SUMS`.

The font and Pillow are pinned for reproducibility:

| What | Pin |
|---|---|
| Font release | Noto CJK `Sans2.004`, asset `16_NotoSansJP.zip`, SHA-256 `2bbdd2c20f30670b39ca735c96d75f1fdabdb348103e43b820cf17701fd22b18` |
| Font file | `NotoSansJP-Regular.otf` inside the zip, SHA-256 `dff723ba59d57d136764a04b9b2d03205544f7cd785a711442d6d2d085ac5073` |
| Pillow | `13.0.0` on Python 3.11 |

To reproduce the release pack locally:

```sh
mkdir -p dist/font-cache
curl -fsSL --retry 3 -o dist/font-cache/16_NotoSansJP.zip \
  https://github.com/notofonts/noto-cjk/releases/download/Sans2.004/16_NotoSansJP.zip
echo "2bbdd2c20f30670b39ca735c96d75f1fdabdb348103e43b820cf17701fd22b18  dist/font-cache/16_NotoSansJP.zip" \
  | sha256sum -c -
python3 -m zipfile -e dist/font-cache/16_NotoSansJP.zip dist/font-cache
echo "dff723ba59d57d136764a04b9b2d03205544f7cd785a711442d6d2d085ac5073  dist/font-cache/NotoSansJP-Regular.otf" \
  | sha256sum -c -
uv run --python 3.11 --with pillow==13.0.0 \
  python3 tools/glyph-pack/build.py --split dist \
  --font dist/font-cache/NotoSansJP-Regular.otf --token ""
```

On macOS use `shasum -a 256 -c -` instead of `sha256sum -c -`. If the font or
Pillow pin changes, update the workflow and this README together.

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

The public font pack built from the pinned Noto Sans JP has 6,561 characters
and 19,683 glyphs, 9,358,845 B (~8.9 MiB). The legacy tokenless `--tangocho`
pack (2,026 WaniKani kanji + kana + `々〜・〇`) has 2,207 characters, 6,621
glyphs and is about 2.9 MB. The token pack generated and uploaded on
2026-10-27 has 7,269 glyphs and 498 radical bitmaps, 3,698,476 B (~3.7 MB). The
test pack with 203 characters is 189,805 B. The entry table is 20 bytes per
glyph; the rest is RLE payloads.

## Notes

- Noto Sans JP is OFL. A local build uses
  `tools/pack-builder/cache/noto/NotoSansJP-Regular.otf` from Tangocho by
  default; the release workflow downloads the pinned `Sans2.004` asset above.
  Pass `--font` for another font.
- Radical images come from the URLs in the user's own WaniKani subject
  response. They are personal, not redistributable. Six radicals have no SVG
  in the API; they fall back to their Unicode glyph.
- WaniKani SVGs use CSS variables such as `var(--color-text, #000)`.
  `cairosvg` does not resolve them, so the builder inlines the fallback
  before rasterizing.
- `cairosvg` needs the native Cairo library. On macOS with Homebrew, if it
  cannot find it, run with `DYLD_LIBRARY_PATH=/opt/homebrew/lib` (or install
  `cairo` with `brew install cairo`).
- The firmware renders radical entries at their native 96 px and scales them
  down for review/lesson prompts. The tokenless pack has no radical images at
  all, so regenerate with `WANIKANI_TOKEN` and `cairosvg` to see radicals.
- The generated `dist/` directory is in `.gitignore`.

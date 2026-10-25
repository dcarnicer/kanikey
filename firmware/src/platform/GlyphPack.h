#pragma once
// Reader for the KKGP visual glyph pack on the microSD card.
//
// The pack stores 4bpp anti-aliased glyphs at several pixel sizes, plus
// optional radical bitmaps. Entries are sorted, so a glyph lookup is a binary
// search over the entry table and never loads the whole pack into RAM.
//
// Format v1 (all little-endian):
//   header  32 B: "KKGP", u16 version, u16 header size, u16 glyphs,
//                 u16 radicals, u32 table offset, u32 data offset,
//                 u32 data length, u32 crc32, u32 reserved
//   glyph   20 B: u32 codepoint, u32 data offset, u32 payload length,
//                 u8 width, u8 height, u8 size, u8 advance,
//                 i8 xoffset, i8 yoffset, u8 encoding, u8 reserved
//   radical 16 B: u32 subject id, u32 data offset, u32 payload length,
//                 u8 width, u8 height, u8 encoding, u8 reserved
//   payload: 4bpp RLE, high nibble = run - 1, low nibble = alpha 0..15,
//            pixels row-major; radicals are stored once at their native size.
#include <FS.h>
#include <M5GFX.h>

#include <cstdint>
#include <string_view>

namespace kk {

struct ClipRect {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    bool contains(int x, int y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
};

struct GlyphInfo {
    uint32_t offset = 0;  // payload offset in the pack
    uint32_t length = 0;  // payload bytes
    uint8_t width = 0;
    uint8_t height = 0;
    uint8_t size = 0;
    uint8_t advance = 0;
    int8_t xoff = 0;  // bitmap left from the pen position
    int8_t yoff = 0;  // bitmap top from the baseline (negative above it)
    uint8_t encoding = 0;
};

class GlyphPack {
public:
    GlyphPack() = default;
    GlyphPack(const GlyphPack&) = delete;
    GlyphPack& operator=(const GlyphPack&) = delete;

    // Opens the pack read-only. On failure `statusLine()` explains why and the
    // object stays not-ready; callers fall back to the built-in font.
    bool open(fs::FS& fs, const char* path);
    void close();

    bool ready() const { return ready_; }
    const char* statusLine() const { return status_; }
    uint16_t glyphCount() const { return glyphCount_; }
    uint16_t radicalCount() const { return radicalCount_; }

    // True when every non-space code point can be drawn at `size`.
    bool covers(std::string_view text, uint8_t size) const;
    // Width in pixels, or -1 when a glyph is missing.
    int textWidth(std::string_view text, uint8_t size) const;

    // Draws with the baseline at `y`, clipped to `clip`.
    bool drawText(lgfx::LovyanGFX& gfx, std::string_view text, int x, int y,
                  uint8_t size, uint16_t fg, uint16_t bg, const ClipRect& clip) const;
    // Right-aligned to `right`; useful for an input box that scrolls left.
    bool drawTextRight(lgfx::LovyanGFX& gfx, std::string_view text, int right, int y,
                       uint8_t size, uint16_t fg, uint16_t bg, const ClipRect& clip) const;
    // Largest size <= prefer that fits maxWidth; returns the size used or 0.
    uint8_t drawTextFit(lgfx::LovyanGFX& gfx, std::string_view text, int x, int y,
                        uint8_t prefer, int maxWidth, uint16_t fg, uint16_t bg,
                        const ClipRect& clip) const;
    // Centres the ink of the whole string in (cx, cy), trying sizes <= prefer.
    bool drawTextCentered(lgfx::LovyanGFX& gfx, std::string_view text, uint8_t prefer,
                          int cx, int cy, int maxWidth, int maxHeight, uint16_t fg,
                          uint16_t bg, const ClipRect& clip) const;

private:
    struct CacheEntry {
        uint32_t cp = 0;
        uint8_t size = 0;
        bool valid = false;
        GlyphInfo info;
    };

    bool readAt(uint32_t offset, uint8_t* out, size_t length) const;
    bool readGlyph(uint16_t index, GlyphInfo* out) const;
    uint16_t lowerBound(uint32_t cp) const;
    bool findExact(uint32_t cp, uint8_t size, GlyphInfo* out) const;
    bool findRadical(uint32_t subjectId, GlyphInfo* out) const;
    bool drawGlyph(lgfx::LovyanGFX& gfx, const GlyphInfo& glyph, int x, int y,
                   uint16_t fg, uint16_t bg, const ClipRect& clip) const;

    mutable File file_;
    bool ready_ = false;
    uint16_t glyphCount_ = 0;
    uint16_t radicalCount_ = 0;
    uint32_t tableOffset_ = 0;
    uint32_t dataOffset_ = 0;
    uint32_t dataLength_ = 0;
    uint8_t sizes_[8] = {};
    uint8_t sizeCount_ = 0;
    char status_[48] = "no pack";
    mutable CacheEntry cache_[16];
    mutable uint8_t cacheNext_ = 0;
};

}  // namespace kk

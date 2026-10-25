#include "platform/GlyphPack.h"

#include <Arduino.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "render/Blend565.h"
#include "wk/Utf8.h"

namespace kk {

namespace {
uint16_t le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

constexpr uint32_t kHeaderBytes = 32;
constexpr uint32_t kGlyphBytes = 20;
constexpr uint32_t kRadicalBytes = 16;
constexpr uint8_t kEncodingRle4 = 1;
}  // namespace

void GlyphPack::close() {
    if (file_) file_.close();
    ready_ = false;
    glyphCount_ = 0;
    radicalCount_ = 0;
    tableOffset_ = dataOffset_ = dataLength_ = 0;
    sizeCount_ = 0;
    for (CacheEntry& c : cache_) c.valid = false;
    snprintf(status_, sizeof status_, "no pack");
}

bool GlyphPack::open(fs::FS& fs, const char* path) {
    close();
    file_ = fs.open(path, FILE_READ);
    if (!file_) {
        snprintf(status_, sizeof status_, "pack missing");
        return false;
    }

    auto fail = [this](const char* why) {
        snprintf(status_, sizeof status_, "%s", why);
        if (file_) file_.close();
        ready_ = false;
        return false;
    };

    uint8_t h[kHeaderBytes];
    if (!readAt(0, h, sizeof h)) return fail("pack: no header");
    if (std::memcmp(h, "KKGP", 4) != 0) return fail("pack: not KKGP");
    const uint16_t version = le16(h + 4);
    const uint16_t headerSize = le16(h + 6);
    glyphCount_ = le16(h + 8);
    radicalCount_ = le16(h + 10);
    tableOffset_ = le32(h + 12);
    dataOffset_ = le32(h + 16);
    dataLength_ = le32(h + 20);
    if (version != 1 || headerSize < kHeaderBytes) return fail("pack: bad version");

    const uint32_t fileSize = file_.size();
    if (dataOffset_ > fileSize || dataLength_ > fileSize - dataOffset_)
        return fail("pack: truncated");
    if (dataLength_ == 0 || glyphCount_ == 0) return fail("pack: empty");
    const uint64_t tableEnd = static_cast<uint64_t>(tableOffset_) +
                              static_cast<uint64_t>(glyphCount_) * kGlyphBytes +
                              static_cast<uint64_t>(radicalCount_) * kRadicalBytes;
    if (tableEnd > dataOffset_) return fail("pack: bad table");

    // The first entries repeat the same code point at every stored size; a
    // short scan is enough to discover all sizes without reading the table.
    const uint16_t scan = std::min<uint16_t>(glyphCount_, 64);
    for (uint16_t i = 0; i < scan; ++i) {
        GlyphInfo g;
        if (!readGlyph(i, &g)) return fail("pack: bad entry");
        bool seen = false;
        for (uint8_t s = 0; s < sizeCount_; ++s)
            if (sizes_[s] == g.size) seen = true;
        if (!seen && sizeCount_ < sizeof sizes_) sizes_[sizeCount_++] = g.size;
    }
    if (sizeCount_ == 0) return fail("pack: no sizes");
    std::sort(sizes_, sizes_ + sizeCount_, [](uint8_t a, uint8_t b) { return a > b; });

    ready_ = true;
    char sizeText[24] = {};
    int n = 0;
    for (uint8_t i = 0; i < sizeCount_; ++i)
        n += snprintf(sizeText + n, sizeof sizeText - n, "%s%u", i ? "/" : "", sizes_[i]);
    if (radicalCount_)
        snprintf(status_, sizeof status_, "%u glyphs, %u radicals, %s px", glyphCount_,
                 radicalCount_, sizeText);
    else
        snprintf(status_, sizeof status_, "%u glyphs, %s px", glyphCount_, sizeText);
    return true;
}

bool GlyphPack::readAt(uint32_t offset, uint8_t* out, size_t length) const {
    if (!file_) return false;
    if (!file_.seek(offset)) return false;
    return file_.read(out, length) == length;
}

bool GlyphPack::readGlyph(uint16_t index, GlyphInfo* out) const {
    if (index >= glyphCount_) return false;
    uint8_t raw[kGlyphBytes];
    if (!readAt(tableOffset_ + static_cast<uint32_t>(index) * kGlyphBytes, raw, sizeof raw))
        return false;
    out->offset = le32(raw + 4);
    out->length = le32(raw + 8);
    out->width = raw[12];
    out->height = raw[13];
    out->size = raw[14];
    out->advance = raw[15];
    out->xoff = static_cast<int8_t>(raw[16]);
    out->yoff = static_cast<int8_t>(raw[17]);
    out->encoding = raw[18];
    return true;
}

uint16_t GlyphPack::lowerBound(uint32_t cp) const {
    uint16_t lo = 0;
    uint16_t hi = glyphCount_;
    while (lo < hi) {
        const uint16_t mid = static_cast<uint16_t>(lo + (hi - lo) / 2);
        uint8_t raw[4];
        if (!readAt(tableOffset_ + static_cast<uint32_t>(mid) * kGlyphBytes, raw, sizeof raw))
            return glyphCount_;
        const uint32_t value = le32(raw);
        if (value < cp)
            lo = static_cast<uint16_t>(mid + 1);
        else
            hi = mid;
    }
    return lo;
}

bool GlyphPack::findExact(uint32_t cp, uint8_t size, GlyphInfo* out) const {
    if (!ready_) return false;
    for (const CacheEntry& c : cache_) {
        if (c.valid && c.cp == cp && c.size == size) {
            *out = c.info;
            return true;
        }
    }
    uint16_t idx = lowerBound(cp);
    for (; idx < glyphCount_; ++idx) {
        uint8_t raw[kGlyphBytes];
        if (!readAt(tableOffset_ + static_cast<uint32_t>(idx) * kGlyphBytes, raw, sizeof raw))
            return false;
        const uint32_t value = le32(raw);
        if (value != cp) break;
        GlyphInfo g;
        if (!readGlyph(idx, &g)) return false;
        if (g.size != size) continue;
        CacheEntry& c = cache_[cacheNext_];
        cacheNext_ = static_cast<uint8_t>((cacheNext_ + 1) % (sizeof cache_ / sizeof cache_[0]));
        c.cp = cp;
        c.size = size;
        c.valid = true;
        c.info = g;
        *out = g;
        return true;
    }
    return false;
}

bool GlyphPack::findRadical(uint32_t subjectId, GlyphInfo* out) const {
    if (!ready_ || !radicalCount_) return false;
    const uint32_t radicalBase = tableOffset_ + static_cast<uint32_t>(glyphCount_) * kGlyphBytes;
    uint16_t lo = 0;
    uint16_t hi = radicalCount_;
    while (lo < hi) {
        const uint16_t mid = static_cast<uint16_t>(lo + (hi - lo) / 2);
        uint8_t raw[4];
        if (!readAt(radicalBase + static_cast<uint32_t>(mid) * kRadicalBytes, raw, sizeof raw))
            return false;
        if (le32(raw) < subjectId)
            lo = static_cast<uint16_t>(mid + 1);
        else
            hi = mid;
    }
    if (lo >= radicalCount_) return false;
    uint8_t raw[kRadicalBytes];
    if (!readAt(radicalBase + static_cast<uint32_t>(lo) * kRadicalBytes, raw, sizeof raw))
        return false;
    if (le32(raw) != subjectId) return false;
    out->offset = le32(raw + 4);
    out->length = le32(raw + 8);
    out->width = raw[12];
    out->height = raw[13];
    out->size = raw[13];
    out->advance = raw[12];
    out->xoff = 0;
    out->yoff = 0;
    out->encoding = raw[14];
    return true;
}

bool GlyphPack::covers(std::string_view text, uint8_t size) const {
    return textWidth(text, size) >= 0;
}

int GlyphPack::textWidth(std::string_view text, uint8_t size) const {
    if (!ready_) return -1;
    const char* p = text.data();
    const char* end = p + text.size();
    int width = 0;
    while (p < end) {
        const uint32_t cp = utf8Next(&p);
        if (cp == 0) break;
        if (cp == ' ') {
            width += std::max<int>(1, size / 4);
            continue;
        }
        GlyphInfo g;
        if (!findExact(cp, size, &g)) return -1;
        width += g.advance;
    }
    return width;
}

bool GlyphPack::drawGlyph(lgfx::LovyanGFX& gfx, const GlyphInfo& glyph, int x, int y,
                          uint16_t fg, uint16_t bg, const ClipRect& clip) const {
    if (!file_ || glyph.encoding != kEncodingRle4 || glyph.width == 0 || glyph.height == 0)
        return false;
    if (!file_.seek(glyph.offset)) return false;

    const int total = static_cast<int>(glyph.width) * glyph.height;
    uint8_t buf[128];
    size_t bufLen = 0;
    size_t bufPos = 0;
    uint32_t consumed = 0;
    int col = 0;
    int row = 0;
    int done = 0;
    while (done < total) {
        if (bufPos >= bufLen) {
            if (consumed >= glyph.length) return false;
            const size_t want =
                std::min(sizeof buf, static_cast<size_t>(glyph.length - consumed));
            const size_t got = file_.read(buf, want);
            if (got == 0) return false;
            consumed += got;
            bufLen = got;
            bufPos = 0;
        }
        const uint8_t b = buf[bufPos++];
        const int run = (b >> 4) + 1;
        const uint8_t alpha = b & 0x0F;
        for (int i = 0; i < run && done < total; ++i, ++done) {
            if (alpha) {
                const int px = x + col;
                const int py = y + row;
                if (clip.contains(px, py))
                    gfx.drawPixel(px, py, alpha >= 15 ? fg : blend565(fg, bg, alpha));
            }
            if (++col >= glyph.width) {
                col = 0;
                ++row;
            }
        }
    }
    return true;
}

bool GlyphPack::drawText(lgfx::LovyanGFX& gfx, std::string_view text, int x, int y,
                         uint8_t size, uint16_t fg, uint16_t bg,
                         const ClipRect& clip) const {
    if (!covers(text, size)) return false;
    gfx.startWrite();
    int pen = x;
    const char* p = text.data();
    const char* end = p + text.size();
    while (p < end) {
        const uint32_t cp = utf8Next(&p);
        if (cp == 0) break;
        if (cp == ' ') {
            pen += std::max<int>(1, size / 4);
            continue;
        }
        GlyphInfo g;
        if (!findExact(cp, size, &g)) {
            gfx.endWrite();
            return false;
        }
        drawGlyph(gfx, g, pen + g.xoff, y + g.yoff, fg, bg, clip);
        pen += g.advance;
    }
    gfx.endWrite();
    return true;
}

bool GlyphPack::drawTextRight(lgfx::LovyanGFX& gfx, std::string_view text, int right, int y,
                              uint8_t size, uint16_t fg, uint16_t bg,
                              const ClipRect& clip) const {
    const int width = textWidth(text, size);
    if (width < 0) return false;
    return drawText(gfx, text, right - width, y, size, fg, bg, clip);
}

uint8_t GlyphPack::drawTextFit(lgfx::LovyanGFX& gfx, std::string_view text, int x, int y,
                               uint8_t prefer, int maxWidth, uint16_t fg, uint16_t bg,
                               const ClipRect& clip) const {
    for (uint8_t i = 0; i < sizeCount_; ++i) {
        const uint8_t size = sizes_[i];
        if (size > prefer) continue;
        const int width = textWidth(text, size);
        if (width < 0 || width > maxWidth) continue;
        if (drawText(gfx, text, x, y, size, fg, bg, clip)) return size;
    }
    return 0;
}

bool GlyphPack::drawTextCentered(lgfx::LovyanGFX& gfx, std::string_view text, uint8_t prefer,
                                 int cx, int cy, int maxWidth, int maxHeight, uint16_t fg,
                                 uint16_t bg, const ClipRect& clip) const {
    if (!ready_) return false;
    for (uint8_t i = 0; i < sizeCount_; ++i) {
        const uint8_t size = sizes_[i];
        if (size > prefer) continue;
        if (!covers(text, size)) continue;

        bool any = false;
        int minX = 0;
        int maxX = 0;
        int minY = 0;
        int maxY = 0;
        int pen = 0;
        const char* p = text.data();
        const char* end = p + text.size();
        while (p < end) {
            const uint32_t cp = utf8Next(&p);
            if (cp == 0) break;
            if (cp == ' ') {
                pen += std::max<int>(1, size / 4);
                continue;
            }
            GlyphInfo g;
            if (!findExact(cp, size, &g)) break;
            const int gx0 = pen + g.xoff;
            const int gx1 = gx0 + g.width;
            const int gy0 = g.yoff;
            const int gy1 = gy0 + g.height;
            if (!any) {
                minX = gx0;
                maxX = gx1;
                minY = gy0;
                maxY = gy1;
                any = true;
            } else {
                minX = std::min(minX, gx0);
                maxX = std::max(maxX, gx1);
                minY = std::min(minY, gy0);
                maxY = std::max(maxY, gy1);
            }
            pen += g.advance;
        }
        if (!any) return false;
        if (maxX - minX > maxWidth || maxY - minY > maxHeight) continue;
        const int startX = cx - (minX + maxX) / 2;
        const int baseline = cy - (minY + maxY) / 2;
        return drawText(gfx, text, startX, baseline, size, fg, bg, clip);
    }
    return false;
}

}  // namespace kk

#include "platform/GlyphPack.h"

#include <Arduino.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "render/Blend565.h"
#include "wk/Crc32.h"
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

bool GlyphPack::open(fs::FS& fs, const char* path, bool checkCrc) {
    return openPath(fs, path, checkCrc);
}

bool GlyphPack::reopen() {
    if (!fs_ || !path_[0]) return false;
    char path[sizeof path_];
    snprintf(path, sizeof path, "%s", path_);
    return openPath(*fs_, path, false);
}

bool GlyphPack::openPath(fs::FS& fs, const char* path, bool crcCheck) {
    close();
    fs_ = &fs;
    snprintf(path_, sizeof path_, "%s", path);
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
    if (tableOffset_ < kHeaderBytes) return fail("pack: bad table");
    const uint64_t tableEnd = static_cast<uint64_t>(tableOffset_) +
                              static_cast<uint64_t>(glyphCount_) * kGlyphBytes +
                              static_cast<uint64_t>(radicalCount_) * kRadicalBytes;
    if (tableEnd > dataOffset_) return fail("pack: bad table");

    // The header CRC covers the entry table and the payload, from the table
    // offset to the end of the data (tools/glyph-pack/build.py packs the table
    // right after the 32-byte header and stores the CRC at offset 24). The
    // full pack is streamed in 1 KB chunks, so this costs no extra RAM, but it
    // took 3,587 ms for the 3.7 MB pack on hardware: only an explicit check or
    // a file that was just written pays for it, never the boot path.
    if (crcCheck) {
        const uint32_t t0 = millis();
        const bool ok = checkStoredCrc(le32(h + 24));
        Serial.printf("[pack] crc %s, %u ms\n", ok ? "ok" : "bad",
                      static_cast<unsigned>(millis() - t0));
        if (!ok) return fail("pack: bad crc");
    }

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

bool GlyphPack::checkStoredCrc(uint32_t want) {
    // One seek, then 1 KB chunks: the card streams the table and the payload
    // without touching the glyph cache or allocating a file-sized buffer.
    if (!file_ || !file_.seek(tableOffset_)) return false;
    if (file_.position() == static_cast<size_t>(UINT32_MAX)) return false;
    uint64_t remaining = static_cast<uint64_t>(dataOffset_) + dataLength_ - tableOffset_;
    uint32_t crc = 0xFFFFFFFFu;
    uint8_t buf[1024];
    while (remaining > 0) {
        const size_t n = static_cast<size_t>(std::min<uint64_t>(sizeof buf, remaining));
        const size_t got = file_.read(buf, n);
        if (got == 0) return false;
        crc = crc32Update(crc, buf, got);
        remaining -= got;
    }
    return (crc ^ 0xFFFFFFFFu) == want;
}

bool GlyphPack::reopenAfterLoss() const {
    // A File whose descriptor died (seen after a re-open inside an HTTP
    // request: seek() failed and position() read UINT32_MAX although the
    // header and table had loaded) returns false here until the file is opened
    // again. Only one re-open per failed access; the caller's retry decides.
    if (!ready_) return false;
    Serial.println("[pack] file handle lost, re-opening");
    GlyphPack* self = const_cast<GlyphPack*>(this);
    return self->reopen();
}

bool GlyphPack::readAt(uint32_t offset, uint8_t* out, size_t length) const {
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (file_ && file_.seek(offset) &&
            file_.position() != static_cast<size_t>(UINT32_MAX) &&
            file_.read(out, length) == length)
            return true;
        if (attempt > 0 || !reopenAfterLoss()) return false;
    }
    return false;
}

bool GlyphPack::seekToData(uint32_t offset) const {
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (file_ && file_.seek(offset) &&
            file_.position() != static_cast<size_t>(UINT32_MAX))
            return true;
        if (attempt > 0 || !reopenAfterLoss()) return false;
    }
    return false;
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

bool GlyphPack::hasRadical(uint32_t subjectId) const {
    GlyphInfo g;
    return findRadical(subjectId, &g);
}

bool GlyphPack::drawRadical(lgfx::LovyanGFX& gfx, uint32_t subjectId, int cx, int cy,
                            int maxWidth, int maxHeight, uint16_t fg, uint16_t bg,
                            const ClipRect& clip) const {
    GlyphInfo g;
    if (!findRadical(subjectId, &g)) return false;
    if (!file_ || g.encoding != kEncodingRle4 || g.width == 0 || g.height == 0)
        return false;
    if (g.width > 128 || maxWidth <= 0 || maxHeight <= 0) return false;

    // Keep the aspect ratio and only ever downscale: 96 px is the native size
    // used by the pack builder.
    int dstW = std::min<int>(maxWidth, g.width);
    int dstH = dstW * static_cast<int>(g.height) / static_cast<int>(g.width);
    if (dstH > maxHeight) {
        dstH = maxHeight;
        dstW = dstH * static_cast<int>(g.width) / static_cast<int>(g.height);
    }
    if (dstW <= 0 || dstH <= 0 || dstW > 240 || dstH > 240) return false;

    if (!seekToData(g.offset)) return false;

    const int left = cx - dstW / 2;
    const int top = cy - dstH / 2;
    uint16_t sum[240] = {};
    uint16_t count[240] = {};
    int curDy = -1;
    auto flush = [&](int dy) {
        if (dy < 0) return;
        const int py = top + dy;
        for (int dx = 0; dx < dstW; ++dx) {
            if (count[dx] == 0) continue;
            const uint8_t alpha =
                static_cast<uint8_t>((sum[dx] + count[dx] / 2) / count[dx]);
            if (alpha == 0) continue;
            const int px = left + dx;
            if (clip.contains(px, py))
                gfx.drawPixel(px, py, alpha >= 15 ? fg : blend565(fg, bg, alpha));
        }
    };

    gfx.startWrite();
    uint8_t buf[128];
    size_t bufLen = 0;
    size_t bufPos = 0;
    uint32_t consumed = 0;
    int sx = 0;
    int sy = 0;
    bool ok = true;
    while (sy < g.height) {
        if (bufPos >= bufLen) {
            if (consumed >= g.length) {
                ok = false;
                break;
            }
            const size_t want =
                std::min(sizeof buf, static_cast<size_t>(g.length - consumed));
            const size_t got = file_.read(buf, want);
            if (got == 0) {
                ok = false;
                break;
            }
            consumed += got;
            bufLen = got;
            bufPos = 0;
        }
        const uint8_t b = buf[bufPos++];
        const int run = (b >> 4) + 1;
        const uint8_t alpha = b & 0x0F;
        for (int i = 0; i < run && sy < g.height; ++i) {
            const int dy = sy * dstH / g.height;
            if (dy != curDy) {
                flush(curDy);
                std::memset(sum, 0, sizeof sum);
                std::memset(count, 0, sizeof count);
                curDy = dy;
            }
            if (alpha != 0) {
                const int dx = sx * dstW / g.width;
                sum[dx] += alpha;
                ++count[dx];
            }
            if (++sx >= g.width) {
                sx = 0;
                ++sy;
            }
        }
    }
    if (ok) flush(curDy);
    gfx.endWrite();
    return ok;
}

bool GlyphPack::drawGlyph(lgfx::LovyanGFX& gfx, const GlyphInfo& glyph, int x, int y,
                          uint16_t fg, uint16_t bg, const ClipRect& clip) const {
    if (!file_ || glyph.encoding != kEncodingRle4 || glyph.width == 0 || glyph.height == 0)
        return false;
    if (!seekToData(glyph.offset)) return false;

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

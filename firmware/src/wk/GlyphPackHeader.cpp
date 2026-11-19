#include "wk/GlyphPackHeader.h"

#include <cstring>

namespace kk {

namespace {

constexpr uint32_t kGlyphBytes = 20;
constexpr uint32_t kRadicalBytes = 16;

uint16_t le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

PackHeaderStatus parsePackHeader(const uint8_t* raw, size_t available, uint32_t fileSize,
                                 PackRole role, PackHeader* out) {
    if (!raw || !out || available < kGlyphPackHeaderBytes ||
        fileSize < kGlyphPackHeaderBytes)
        return PackHeaderStatus::Short;
    if (std::memcmp(raw, "KKGP", 4) != 0) return PackHeaderStatus::BadMagic;

    PackHeader h;
    h.version = le16(raw + 4);
    h.headerSize = le16(raw + 6);
    h.glyphCount = le16(raw + 8);
    h.radicalCount = le16(raw + 10);
    h.tableOffset = le32(raw + 12);
    h.dataOffset = le32(raw + 16);
    h.dataLength = le32(raw + 20);
    h.crc = le32(raw + 24);

    if (h.version != kGlyphPackVersion1 && h.version != kGlyphPackVersion2)
        return PackHeaderStatus::BadVersion;
    if (h.headerSize < kGlyphPackHeaderBytes) return PackHeaderStatus::BadHeaderSize;

    // v1 has a reserved u32; v2 stores the kind there.
    const uint32_t rawKind = le32(raw + 28);
    if (h.version == kGlyphPackVersion1) {
        h.kind = PackKind::Combined;
    } else {
        if (rawKind > static_cast<uint32_t>(PackKind::Radical))
            return PackHeaderStatus::BadKind;
        h.kind = static_cast<PackKind>(rawKind);
    }

    if (h.tableOffset < h.headerSize) return PackHeaderStatus::BadOffsets;
    if (h.dataLength == 0 || h.dataOffset < h.tableOffset)
        return PackHeaderStatus::BadOffsets;
    if (h.dataOffset > fileSize || h.dataLength > fileSize - h.dataOffset)
        return PackHeaderStatus::Truncated;

    const uint64_t tableEnd = static_cast<uint64_t>(h.tableOffset) +
                              static_cast<uint64_t>(h.glyphCount) * kGlyphBytes +
                              static_cast<uint64_t>(h.radicalCount) * kRadicalBytes;
    if (tableEnd > h.dataOffset) return PackHeaderStatus::BadOffsets;

    // A kind promises which table exists; the other must be empty in v2.
    switch (h.kind) {
        case PackKind::Font:
            if (h.radicalCount != 0) return PackHeaderStatus::BadKind;
            if (h.glyphCount == 0) return PackHeaderStatus::EmptyFont;
            break;
        case PackKind::Radical:
            if (h.glyphCount != 0) return PackHeaderStatus::BadKind;
            if (h.radicalCount == 0) return PackHeaderStatus::EmptyRadical;
            break;
        case PackKind::Combined:
            if (h.glyphCount == 0) return PackHeaderStatus::EmptyFont;
            break;
    }

    // A v2 font/radical file cannot serve the other role. A combined file
    // (v1 or v2 kind 0) can, as long as the requested table is present.
    if (role == PackRole::Font) {
        if (h.kind == PackKind::Radical) return PackHeaderStatus::WrongKind;
        if (h.glyphCount == 0) return PackHeaderStatus::EmptyFont;
    } else {
        if (h.kind == PackKind::Font) return PackHeaderStatus::WrongKind;
        if (h.radicalCount == 0) return PackHeaderStatus::EmptyRadical;
    }

    *out = h;
    return PackHeaderStatus::Ok;
}

const char* packHeaderStatusText(PackHeaderStatus status) {
    switch (status) {
        case PackHeaderStatus::Ok: return "";
        case PackHeaderStatus::Short: return "pack: no header";
        case PackHeaderStatus::BadMagic: return "pack: not KKGP";
        case PackHeaderStatus::BadVersion: return "pack: bad version";
        case PackHeaderStatus::BadHeaderSize: return "pack: bad header";
        case PackHeaderStatus::BadOffsets: return "pack: bad table";
        case PackHeaderStatus::Truncated: return "pack: truncated";
        case PackHeaderStatus::BadKind: return "pack: bad kind";
        case PackHeaderStatus::EmptyFont: return "pack: no glyphs";
        case PackHeaderStatus::EmptyRadical: return "pack: no radicals";
        case PackHeaderStatus::WrongKind: return "pack: wrong kind";
    }
    return "pack: bad header";
}

const char* packKindName(PackKind kind) {
    switch (kind) {
        case PackKind::Combined: return "combined";
        case PackKind::Font: return "font";
        case PackKind::Radical: return "radicals";
    }
    return "?";
}

}  // namespace kk

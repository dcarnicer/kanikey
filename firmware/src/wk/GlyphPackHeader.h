#pragma once
// Pure parser for the KKGP glyph-pack header. Kept out of platform/GlyphPack.h
// so the header rules can run in the native test build without Arduino or SD.
//
// Format v2 (all little-endian):
//   header  32 B: "KKGP", u16 version, u16 header size, u16 glyphs,
//                 u16 radicals, u32 table offset, u32 data offset,
//                 u32 data length, u32 crc32, u32 kind
//   kind: 0 = combined/legacy layout, 1 = font (glyphs only),
//         2 = radicals (radical table only)
//
// Format v1 has the same header with version 1 and a reserved u32 at offset 28;
// it is a combined pack and can serve both roles.
#include <cstddef>
#include <cstdint>

namespace kk {

constexpr uint16_t kGlyphPackHeaderBytes = 32;
constexpr uint16_t kGlyphPackVersion1 = 1;
constexpr uint16_t kGlyphPackVersion2 = 2;

enum class PackKind : uint8_t { Combined = 0, Font = 1, Radical = 2 };

// The role the caller wants from the file. A combined pack serves both.
enum class PackRole : uint8_t { Font, Radical };

enum class PackHeaderStatus : uint8_t {
    Ok,
    Short,
    BadMagic,
    BadVersion,
    BadHeaderSize,
    BadOffsets,
    Truncated,
    BadKind,
    EmptyFont,
    EmptyRadical,
    WrongKind,
};

struct PackHeader {
    uint16_t version = 0;
    uint16_t headerSize = 0;
    uint16_t glyphCount = 0;
    uint16_t radicalCount = 0;
    uint32_t tableOffset = 0;
    uint32_t dataOffset = 0;
    uint32_t dataLength = 0;
    uint32_t crc = 0;
    PackKind kind = PackKind::Combined;
};

// Validates the first 32 bytes against `fileSize` and the requested role.
// `raw` may be a larger buffer; only the header is read. Returns Ok and fills
// `out` on success. A v1 file is always treated as a combined pack, whatever
// its reserved u32 says.
PackHeaderStatus parsePackHeader(const uint8_t* raw, size_t available, uint32_t fileSize,
                                 PackRole role, PackHeader* out);

// User-facing reason, suitable for GlyphPack::statusLine().
const char* packHeaderStatusText(PackHeaderStatus status);

const char* packKindName(PackKind kind);

}  // namespace kk

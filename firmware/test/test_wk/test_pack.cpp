// ONB-01: the KKGP header parser accepts v2 font/radical packs and keeps v1
// combined files readable. The parser is pure (wk/GlyphPackHeader), so these
// cases do not need an SD card.
#include <unity.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "wk/GlyphPackHeader.h"

using namespace kk;

namespace {

constexpr uint32_t kFileSize = 4096;

void put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

void put32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

std::vector<uint8_t> header(uint16_t version, uint16_t glyphs, uint16_t radicals,
                            uint32_t kind, uint32_t dataLength = 64) {
    const uint32_t tableOffset = kGlyphPackHeaderBytes;
    const uint32_t dataOffset = tableOffset + 20u * glyphs + 16u * radicals;
    std::vector<uint8_t> h(kGlyphPackHeaderBytes, 0);
    std::memcpy(h.data(), "KKGP", 4);
    put16(h.data() + 4, version);
    put16(h.data() + 6, kGlyphPackHeaderBytes);
    put16(h.data() + 8, glyphs);
    put16(h.data() + 10, radicals);
    put32(h.data() + 12, tableOffset);
    put32(h.data() + 16, dataOffset);
    put32(h.data() + 20, dataLength);
    put32(h.data() + 24, 0x12345678u);
    put32(h.data() + 28, kind);
    return h;
}

PackHeader parse(const std::vector<uint8_t>& h, PackRole role, uint32_t fileSize = kFileSize) {
    PackHeader out;
    const PackHeaderStatus status =
        parsePackHeader(h.data(), h.size(), fileSize, role, &out);
    TEST_ASSERT_EQUAL(static_cast<int>(PackHeaderStatus::Ok), static_cast<int>(status));
    return out;
}

}  // namespace

static void test_pack_v2_font_header() {
    const std::vector<uint8_t> h =
        header(kGlyphPackVersion2, 3, 0, static_cast<uint32_t>(PackKind::Font));

    const PackHeader font = parse(h, PackRole::Font);
    TEST_ASSERT_EQUAL_UINT16(2, font.version);
    TEST_ASSERT_EQUAL(static_cast<int>(PackKind::Font), static_cast<int>(font.kind));
    TEST_ASSERT_EQUAL_UINT16(3, font.glyphCount);
    TEST_ASSERT_EQUAL_UINT16(0, font.radicalCount);
    TEST_ASSERT_EQUAL_UINT32(kGlyphPackHeaderBytes, font.tableOffset);
    TEST_ASSERT_EQUAL_UINT32(32 + 3 * 20, font.dataOffset);

    // A font kind cannot answer the radical role.
    PackHeader out;
    TEST_ASSERT_EQUAL(static_cast<int>(PackHeaderStatus::WrongKind),
                      static_cast<int>(
                          parsePackHeader(h.data(), h.size(), kFileSize, PackRole::Radical,
                                          &out)));
}

static void test_pack_v2_radical_header() {
    const std::vector<uint8_t> h =
        header(kGlyphPackVersion2, 0, 4, static_cast<uint32_t>(PackKind::Radical));

    const PackHeader radical = parse(h, PackRole::Radical);
    TEST_ASSERT_EQUAL_UINT16(2, radical.version);
    TEST_ASSERT_EQUAL(static_cast<int>(PackKind::Radical), static_cast<int>(radical.kind));
    TEST_ASSERT_EQUAL_UINT16(0, radical.glyphCount);
    TEST_ASSERT_EQUAL_UINT16(4, radical.radicalCount);
    TEST_ASSERT_EQUAL_UINT32(32, radical.tableOffset);
    TEST_ASSERT_EQUAL_UINT32(32 + 4 * 16, radical.dataOffset);

    PackHeader out;
    TEST_ASSERT_EQUAL(static_cast<int>(PackHeaderStatus::WrongKind),
                      static_cast<int>(parsePackHeader(h.data(), h.size(), kFileSize,
                                                       PackRole::Font, &out)));
}

static void test_pack_v1_still_readable() {
    // Version 1 with a non-zero reserved word: the old field is ignored and the
    // file is a combined pack for both roles.
    const std::vector<uint8_t> h = header(kGlyphPackVersion1, 2, 1, 0xFFFFFFFFu);

    const PackHeader font = parse(h, PackRole::Font);
    TEST_ASSERT_EQUAL_UINT16(1, font.version);
    TEST_ASSERT_EQUAL(static_cast<int>(PackKind::Combined), static_cast<int>(font.kind));

    const PackHeader radical = parse(h, PackRole::Radical);
    TEST_ASSERT_EQUAL(static_cast<int>(PackKind::Combined), static_cast<int>(radical.kind));
    TEST_ASSERT_EQUAL_UINT16(1, radical.radicalCount);

    // A v1 pack built without radicals still serves text, but not the radical
    // role: that is the explicit "no radicals" state, not a wrong kind.
    const std::vector<uint8_t> fontOnly = header(kGlyphPackVersion1, 2, 0, 0);
    parse(fontOnly, PackRole::Font);
    PackHeader out;
    TEST_ASSERT_EQUAL(static_cast<int>(PackHeaderStatus::EmptyRadical),
                      static_cast<int>(parsePackHeader(fontOnly.data(), fontOnly.size(),
                                                       kFileSize, PackRole::Radical, &out)));
}

static void test_pack_wrong_kind_missing() {
    PackHeader out;

    // v2 kind/table disagreements are rejected.
    const std::vector<uint8_t> fontWithRadicals =
        header(kGlyphPackVersion2, 2, 1, static_cast<uint32_t>(PackKind::Font));
    TEST_ASSERT_EQUAL(static_cast<int>(PackHeaderStatus::BadKind),
                      static_cast<int>(parsePackHeader(fontWithRadicals.data(),
                                                       fontWithRadicals.size(), kFileSize,
                                                       PackRole::Font, &out)));

    const std::vector<uint8_t> radicalsWithGlyphs =
        header(kGlyphPackVersion2, 2, 1, static_cast<uint32_t>(PackKind::Radical));
    TEST_ASSERT_EQUAL(static_cast<int>(PackHeaderStatus::BadKind),
                      static_cast<int>(parsePackHeader(radicalsWithGlyphs.data(),
                                                       radicalsWithGlyphs.size(), kFileSize,
                                                       PackRole::Radical, &out)));

    // Unknown kinds are not silently treated as combined.
    const std::vector<uint8_t> unknown =
        header(kGlyphPackVersion2, 2, 0, 3);
    TEST_ASSERT_EQUAL(static_cast<int>(PackHeaderStatus::BadKind),
                      static_cast<int>(parsePackHeader(unknown.data(), unknown.size(),
                                                       kFileSize, PackRole::Font, &out)));

    // Empty tables and short/truncated files fail with a status the reader can
    // put on the status line.
    const std::vector<uint8_t> emptyFont =
        header(kGlyphPackVersion2, 0, 0, static_cast<uint32_t>(PackKind::Font), 64);
    TEST_ASSERT_EQUAL(static_cast<int>(PackHeaderStatus::EmptyFont),
                      static_cast<int>(parsePackHeader(emptyFont.data(), emptyFont.size(),
                                                       kFileSize, PackRole::Font, &out)));

    const std::vector<uint8_t> truncated =
        header(kGlyphPackVersion2, 1, 0, static_cast<uint32_t>(PackKind::Font), 64);
    TEST_ASSERT_EQUAL(static_cast<int>(PackHeaderStatus::Truncated),
                      static_cast<int>(parsePackHeader(truncated.data(), truncated.size(),
                                                       64, PackRole::Font, &out)));
    TEST_ASSERT_EQUAL(static_cast<int>(PackHeaderStatus::Short),
                      static_cast<int>(parsePackHeader(truncated.data(), 12, kFileSize,
                                                       PackRole::Font, &out)));
}

void run_pack_tests() {
    RUN_TEST(test_pack_v2_font_header);
    RUN_TEST(test_pack_v2_radical_header);
    RUN_TEST(test_pack_v1_still_readable);
    RUN_TEST(test_pack_wrong_kind_missing);
}

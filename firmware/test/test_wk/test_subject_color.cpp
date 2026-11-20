// WaniKani-style subject type colours: the type-to-colour mapping and the
// 8-bit canvas values the panel shows. Pure host tests (no Arduino headers).
#include <unity.h>

#include <cstdint>

#include "ui/Theme.h"
#include "wk/Types.h"

using namespace kk::ui;

namespace {

// The canvas conversion documented in docs/design.md §8.1: RGB565 to RGB332,
// then the panel's expansion of each channel back to 8 bits.
uint8_t to332(uint16_t c) {
    return static_cast<uint8_t>(((((c >> 13) & 7) << 3) + ((c >> 8) & 7)) << 2) +
           static_cast<uint8_t>((c >> 3) & 3);
}

struct Rgb8 {
    int r, g, b;
    bool operator==(const Rgb8& o) const { return r == o.r && g == o.g && b == o.b; }
};

Rgb8 expand332(uint16_t c) {
    const int v = to332(c);
    const int r = (v >> 5) & 7;
    const int g = (v >> 2) & 7;
    const int b = v & 3;
    return Rgb8{36 * r + r / 2, 36 * g + g / 2, 85 * b};
}

}  // namespace

// Radical is light blue, kanji pink, and both vocabulary kinds purple.
static void test_subject_type_color_mapping() {
    TEST_ASSERT_TRUE(subjectTypeColor(kk::SubjectType::Radical) == kRadical);
    TEST_ASSERT_TRUE(subjectTypeColor(kk::SubjectType::Kanji) == kKanji);
    TEST_ASSERT_TRUE(subjectTypeColor(kk::SubjectType::Vocabulary) == kVocab);
    TEST_ASSERT_TRUE(subjectTypeColor(kk::SubjectType::KanaVocabulary) == kVocab);
}

// The three colours are told apart by the prompt panel and the list chips.
static void test_subject_type_colors_are_distinct() {
    TEST_ASSERT_TRUE(kRadical != kKanji);
    TEST_ASSERT_TRUE(kKanji != kVocab);
    TEST_ASSERT_TRUE(kVocab != kRadical);
    TEST_ASSERT_TRUE(kRadical != kSurface);
    TEST_ASSERT_TRUE(kKanji != kSurface);
    TEST_ASSERT_TRUE(kVocab != kSurface);
}

// The RGB332 results the owner expects: #00B6FF, #FF00AA and #B600FF.
static void test_subject_type_colors_quantize_to_canvas() {
    TEST_ASSERT_TRUE(expand332(kRadical) == (Rgb8{0x00, 0xB6, 0xFF}));
    TEST_ASSERT_TRUE(expand332(kKanji) == (Rgb8{0xFF, 0x00, 0xAA}));
    TEST_ASSERT_TRUE(expand332(kVocab) == (Rgb8{0xB6, 0x00, 0xFF}));
}

// The same canvas value is what white glyphs are blended against, so every
// type colour must stay a real hue on the 8-bit canvas: no channel collapses
// to zero in the wrong place.
static void test_subject_type_colors_keep_their_hue() {
    const Rgb8 radical = expand332(kRadical);
    const Rgb8 kanji = expand332(kKanji);
    const Rgb8 vocab = expand332(kVocab);
    TEST_ASSERT_TRUE(radical.b > radical.r && radical.b > 0 && radical.g > 0);
    TEST_ASSERT_TRUE(kanji.r > kanji.g && kanji.b > kanji.g && kanji.r > 0);
    TEST_ASSERT_TRUE(vocab.b > vocab.g && vocab.r > vocab.g && vocab.b > 0);
}

void run_subject_color_tests() {
    RUN_TEST(test_subject_type_color_mapping);
    RUN_TEST(test_subject_type_colors_are_distinct);
    RUN_TEST(test_subject_type_colors_quantize_to_canvas);
    RUN_TEST(test_subject_type_colors_keep_their_hue);
}

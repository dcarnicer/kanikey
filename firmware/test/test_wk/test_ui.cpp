// TNG-03: pure list-window math, hint-bar fallback, text layout, UI font
// metrics and icons.
#include <unity.h>

#include <cstring>
#include <string>
#include <string_view>

#include "ui/HintBar.h"
#include "ui/Icons.h"
#include "ui/ListView.h"
#include "ui/TextLayout.h"
#include "ui/UiFont.h"

using namespace kk::ui;

namespace {

int codePointCount(std::string_view text) {
    int count = 0;
    for (size_t i = 0; i < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        size_t step = 1;
        if ((lead & 0xE0) == 0xC0) step = 2;
        else if ((lead & 0xF0) == 0xE0) step = 3;
        else if ((lead & 0xF8) == 0xF0) step = 4;
        if (i + step > text.size()) step = 1;
        i += step;
        ++count;
    }
    return count;
}

// Monospaced stub: every code point is `per` pixels wide.
int measureFixed(void* ctx, std::string_view text) {
    return codePointCount(text) * *static_cast<int*>(ctx);
}

void assertLine(const TextLine& line, size_t begin, size_t end) {
    TEST_ASSERT_EQUAL_UINT(begin, line.begin);
    TEST_ASSERT_EQUAL_UINT(end, line.end);
}

// ------------------------------------------------------------- ListWindow

static void test_list_window_empty() {
    const ListWindow w = listWindow(0, 3, 7);
    TEST_ASSERT_EQUAL(0, w.count);
    TEST_ASSERT_EQUAL(7, w.visible);
    TEST_ASSERT_EQUAL(0, w.selected);
    TEST_ASSERT_EQUAL(0, w.first);
    TEST_ASSERT_EQUAL(0, w.rows);
}

static void test_list_window_short_list() {
    const ListWindow w = listWindow(3, 1, 7);
    TEST_ASSERT_EQUAL(3, w.count);
    TEST_ASSERT_EQUAL(7, w.visible);
    TEST_ASSERT_EQUAL(1, w.selected);
    TEST_ASSERT_EQUAL(0, w.first);
    TEST_ASSERT_EQUAL(3, w.rows);
}

static void test_list_window_seven_rows_fit() {
    const ListWindow w = listWindow(7, 3, 7);
    TEST_ASSERT_EQUAL(3, w.selected);
    TEST_ASSERT_EQUAL(0, w.first);
    TEST_ASSERT_EQUAL(7, w.rows);
}

static void test_list_window_centres_selection() {
    const ListWindow middle = listWindow(10, 4, 7);
    TEST_ASSERT_EQUAL(1, middle.first);
    TEST_ASSERT_EQUAL(7, middle.rows);

    const ListWindow firstRow = listWindow(10, 0, 7);
    TEST_ASSERT_EQUAL(0, firstRow.first);
    TEST_ASSERT_EQUAL(7, firstRow.rows);

    const ListWindow lastRow = listWindow(10, 9, 7);
    TEST_ASSERT_EQUAL(3, lastRow.first);
    TEST_ASSERT_EQUAL(7, lastRow.rows);
}

static void test_list_window_clamps_selection() {
    const ListWindow low = listWindow(10, -2, 7);
    TEST_ASSERT_EQUAL(0, low.selected);
    TEST_ASSERT_EQUAL(0, low.first);

    const ListWindow high = listWindow(10, 99, 7);
    TEST_ASSERT_EQUAL(9, high.selected);
    TEST_ASSERT_EQUAL(3, high.first);
}

static void test_list_window_wraps_selection() {
    const ListWindow high = listWindow(10, 13, 7, true);
    TEST_ASSERT_EQUAL(3, high.selected);
    TEST_ASSERT_EQUAL(0, high.first);

    const ListWindow low = listWindow(10, -2, 7, true);
    TEST_ASSERT_EQUAL(8, low.selected);
    TEST_ASSERT_EQUAL(3, low.first);
    TEST_ASSERT_EQUAL(7, low.rows);
}

static void test_list_window_last_page_start() {
    const ListWindow w = listWindow(9, 8, 7);
    TEST_ASSERT_EQUAL(2, w.first);
    TEST_ASSERT_EQUAL(7, w.rows);

    const ListWindow tiny = listWindow(5, 4, 3);
    TEST_ASSERT_EQUAL(2, tiny.first);
    TEST_ASSERT_EQUAL(3, tiny.rows);
}

static void test_list_window_zero_visible() {
    const ListWindow w = listWindow(5, 3, 0);
    TEST_ASSERT_EQUAL(5, w.count);
    TEST_ASSERT_EQUAL(0, w.visible);
    TEST_ASSERT_EQUAL(3, w.selected);
    TEST_ASSERT_EQUAL(0, w.first);
    TEST_ASSERT_EQUAL(0, w.rows);
}

// ------------------------------------------------------- List selection

static void test_list_move_selection_non_wrap() {
    TEST_ASSERT_EQUAL(1, listMoveSelection(0, 5, 1));
    TEST_ASSERT_EQUAL(3, listMoveSelection(2, 5, 1));
    TEST_ASSERT_EQUAL(4, listMoveSelection(4, 5, 1));
    TEST_ASSERT_EQUAL(0, listMoveSelection(0, 5, -1));
    TEST_ASSERT_EQUAL(4, listMoveSelection(2, 5, 10));
    TEST_ASSERT_EQUAL(0, listMoveSelection(2, 5, -10));
}

static void test_list_move_selection_wrap() {
    TEST_ASSERT_EQUAL(0, listMoveSelection(4, 5, 1, true));
    TEST_ASSERT_EQUAL(4, listMoveSelection(0, 5, -1, true));
    TEST_ASSERT_EQUAL(2, listMoveSelection(0, 5, 7, true));
    TEST_ASSERT_EQUAL(0, listMoveSelection(9, 5, 1, true));
}

static void test_list_move_selection_edges() {
    TEST_ASSERT_EQUAL(0, listMoveSelection(0, 0, 1));
    TEST_ASSERT_EQUAL(0, listMoveSelection(3, 0, -1, true));
    TEST_ASSERT_EQUAL(4, listMoveSelection(9, 5, 0));
    TEST_ASSERT_EQUAL(0, listMoveSelection(-3, 5, 0, true));
}

// --------------------------------------------------------- hint fallback

static void test_hint_fit_index_first_match() {
    const int widths[] = {120, 100, 80};
    TEST_ASSERT_EQUAL(0, fitHintIndex(widths, 3, 120));
    TEST_ASSERT_EQUAL(1, fitHintIndex(widths, 3, 110));
    TEST_ASSERT_EQUAL(1, fitHintIndex(widths, 3, 100));
    TEST_ASSERT_EQUAL(2, fitHintIndex(widths, 3, 80));
}

static void test_hint_fit_index_falls_back_to_last() {
    const int widths[] = {120, 100, 80};
    TEST_ASSERT_EQUAL(2, fitHintIndex(widths, 3, 40));
    TEST_ASSERT_EQUAL(2, fitHintIndex(widths, 3, 0));
    TEST_ASSERT_EQUAL(0, fitHintIndex(widths, 1, 0));
    const int zero[] = {5, 0};
    TEST_ASSERT_EQUAL(1, fitHintIndex(zero, 2, 0));
}

static void test_hint_fit_index_empty() {
    TEST_ASSERT_EQUAL(-1, fitHintIndex(nullptr, 3, 224));
    TEST_ASSERT_EQUAL(-1, fitHintIndex(nullptr, 0, 224));
    const int widths[] = {10};
    TEST_ASSERT_EQUAL(-1, fitHintIndex(widths, 0, 224));
}

static void test_hint_bar_geometry() {
    TEST_ASSERT_EQUAL(125, kHintY);
    TEST_ASSERT_EQUAL(8, kHintLeftX);
    TEST_ASSERT_EQUAL(232, kHintRightX);
    TEST_ASSERT_EQUAL(224, kHintWidth);
}

// ----------------------------------------------------------- text layout

static void test_layout_wrap_words_breaks_at_spaces() {
    int per = 6;
    const TextMeasure measure{&measureFixed, &per};
    TextLine lines[4];
    const WrapResult r = wrapLines("hello world", 36, measure, lines, 4);
    TEST_ASSERT_EQUAL(2, r.lineCount);
    TEST_ASSERT_FALSE(r.truncated);
    assertLine(lines[0], 0, 5);
    assertLine(lines[1], 6, 11);
    TEST_ASSERT_EQUAL(2, wrapLineCount("hello world", 36, measure));
}

static void test_layout_wrap_words_splits_long_word() {
    int per = 6;
    const TextMeasure measure{&measureFixed, &per};
    TextLine lines[8];
    const WrapResult r = wrapLines("abcdefgh ij", 18, measure, lines, 8);
    TEST_ASSERT_EQUAL(4, r.lineCount);
    TEST_ASSERT_FALSE(r.truncated);
    assertLine(lines[0], 0, 3);
    assertLine(lines[1], 3, 6);
    assertLine(lines[2], 6, 8);
    assertLine(lines[3], 9, 11);
}

static void test_layout_wrap_characters_mode() {
    int per = 6;
    const TextMeasure measure{&measureFixed, &per};
    TextLine lines[5];
    const WrapResult r =
        wrapLines("abc de", 12, measure, lines, 5, WrapMode::Characters);
    TEST_ASSERT_EQUAL(3, r.lineCount);
    TEST_ASSERT_FALSE(r.truncated);
    assertLine(lines[0], 0, 2);
    assertLine(lines[1], 2, 4);  // the space stays in character mode
    assertLine(lines[2], 4, 6);
}

static void test_layout_wrap_drops_spaces_at_breaks() {
    int per = 6;
    const TextMeasure measure{&measureFixed, &per};
    TextLine lines[4];
    const WrapResult r = wrapLines("ab   cd", 12, measure, lines, 4);
    TEST_ASSERT_EQUAL(2, r.lineCount);
    assertLine(lines[0], 0, 2);
    assertLine(lines[1], 5, 7);

    const WrapResult leading = wrapLines("  ab", 12, measure, lines, 4);
    TEST_ASSERT_EQUAL(1, leading.lineCount);
    assertLine(lines[0], 2, 4);

    const WrapResult blank = wrapLines("   ", 12, measure, lines, 4);
    TEST_ASSERT_EQUAL(0, blank.lineCount);
    TEST_ASSERT_FALSE(blank.truncated);
}

static void test_layout_wrap_max_lines_and_truncated() {
    int per = 6;
    const TextMeasure measure{&measureFixed, &per};
    const char* text = "one two three four";
    TextLine lines[8];

    const WrapResult two = wrapLines(text, 24, measure, lines, 2);
    TEST_ASSERT_EQUAL(2, two.lineCount);
    TEST_ASSERT_TRUE(two.truncated);
    assertLine(lines[0], 0, 3);
    assertLine(lines[1], 4, 7);

    const WrapResult one = wrapLines(text, 24, measure, lines, 1);
    TEST_ASSERT_EQUAL(1, one.lineCount);
    TEST_ASSERT_TRUE(one.truncated);

    const WrapResult all = wrapLines(text, 24, measure, lines, 8);
    TEST_ASSERT_EQUAL(5, all.lineCount);
    TEST_ASSERT_FALSE(all.truncated);
    TEST_ASSERT_EQUAL(5, wrapLineCount(text, 24, measure));
}

static void test_layout_wrap_empty_and_zero_width() {
    int per = 6;
    const TextMeasure measure{&measureFixed, &per};
    TextLine lines[4];

    const WrapResult empty = wrapLines("", 30, measure, lines, 4);
    TEST_ASSERT_EQUAL(0, empty.lineCount);
    TEST_ASSERT_FALSE(empty.truncated);

    const WrapResult zero = wrapLines("abc", 0, measure, lines, 2);
    TEST_ASSERT_EQUAL(2, zero.lineCount);
    TEST_ASSERT_TRUE(zero.truncated);
    TEST_ASSERT_EQUAL(3, wrapLineCount("abc", 0, measure));

    const WrapResult none = wrapLines("abc", 30, measure, nullptr, 0);
    TEST_ASSERT_EQUAL(0, none.lineCount);
    TEST_ASSERT_TRUE(none.truncated);

    const WrapResult counted = wrapLines("hello world", 36, measure, nullptr, 4);
    TEST_ASSERT_EQUAL(2, counted.lineCount);
    TEST_ASSERT_FALSE(counted.truncated);
}

static void test_layout_wrap_line_count_matches_limits() {
    int per = 6;
    const TextMeasure measure{&measureFixed, &per};
    const char* text = "alpha beta gamma";
    const int widths[] = {6, 12, 18, 24, 30, 36};
    for (int width : widths) {
        TextLine lines[16];
        const WrapResult r = wrapLines(text, width, measure, lines, 16);
        TEST_ASSERT_EQUAL(wrapLineCount(text, width, measure), r.lineCount);
        TEST_ASSERT_FALSE(r.truncated);
    }
    TEST_ASSERT_EQUAL(3, wrapLineCount(text, 30, measure));

    TextLine lines[8];
    const WrapResult limited = wrapLines(text, 30, measure, lines, 2);
    TEST_ASSERT_EQUAL(2, limited.lineCount);
    TEST_ASSERT_TRUE(limited.truncated);

    const std::string japanese = "日本語";
    TEST_ASSERT_EQUAL(2, wrapLineCount(japanese, 12, measure, WrapMode::Characters));
    TEST_ASSERT_EQUAL(3, wrapLineCount(japanese, 6, measure, WrapMode::Characters));
}

static void test_layout_ellipsize_short_text_and_force() {
    int per = 6;
    const TextMeasure measure{&measureFixed, &per};

    TEST_ASSERT_EQUAL_STRING("abc", ellipsize("abc", 42, measure).c_str());
    TEST_ASSERT_EQUAL_STRING("abc ...", ellipsize("abc", 42, measure, "...", true).c_str());
    TEST_ASSERT_EQUAL_STRING("abcdef", ellipsize("abcdef", 48, measure).c_str());
    TEST_ASSERT_EQUAL_STRING("abcd ...",
                             ellipsize("abcdef", 48, measure, "...", true).c_str());
    TEST_ASSERT_EQUAL_STRING("...", ellipsize("abcdef", 0, measure, "...", true).c_str());
    TEST_ASSERT_EQUAL_STRING("...", ellipsize("", 30, measure, "...", true).c_str());
}

static void test_layout_ellipsize_keeps_utf8_together() {
    int per = 6;
    const TextMeasure measure{&measureFixed, &per};
    const std::string japanese = "日本語です";
    TEST_ASSERT_EQUAL_STRING("日本 ...",
                             ellipsize(japanese, 36, measure, "...", true).c_str());
    TEST_ASSERT_EQUAL_STRING("日 ...",
                             ellipsize(japanese, 30, measure, "...", true).c_str());
}

static void test_layout_prefix_and_suffix_by_code_point() {
    int per = 6;
    const TextMeasure measure{&measureFixed, &per};
    const std::string text = "a日本b";
    TEST_ASSERT_EQUAL_UINT64(4, fitPrefix(text, 0, text.size(), 12, measure));
    TEST_ASSERT_EQUAL_UINT64(1, fitPrefix(text, 0, text.size(), 6, measure));
    TEST_ASSERT_EQUAL_UINT64(0, fitPrefix(text, 0, text.size(), 0, measure));
    TEST_ASSERT_EQUAL_UINT64(4, fitSuffix(text, 0, text.size(), 12, measure));
    TEST_ASSERT_EQUAL_UINT64(7, fitSuffix(text, 0, text.size(), 6, measure));
    TEST_ASSERT_EQUAL_UINT64(text.size(), fitSuffix(text, 0, text.size(), 0, measure));
    TEST_ASSERT_EQUAL_UINT64(0, fitSuffix(text, 0, text.size(), 100, measure));

    // Reversed and out-of-range spans are clamped, not read.
    TEST_ASSERT_EQUAL_UINT64(3, fitPrefix(text, 5, 3, 100, measure));
    TEST_ASSERT_EQUAL_UINT64(3, fitSuffix(text, 5, 3, 100, measure));
    TEST_ASSERT_EQUAL_UINT64(0, fitPrefix(text, 0, text.size(), -1, measure));
    TEST_ASSERT_EQUAL_UINT64(text.size(), fitSuffix(text, 0, text.size(), -1, measure));
}

// ---------------------------------------------------------------- UI font

static void test_uifont_ids_are_the_legacy_font_numbers() {
    TEST_ASSERT_EQUAL(0, fontId(UiFont::Small));
    TEST_ASSERT_EQUAL(1, fontId(UiFont::Base));
    TEST_ASSERT_EQUAL(2, fontId(UiFont::Title));
}

static void test_uifont_heights_match_the_builtin_fonts() {
    TEST_ASSERT_EQUAL(8, fontHeight(UiFont::Small));
    TEST_ASSERT_EQUAL(8, fontHeight(UiFont::Base));
    TEST_ASSERT_EQUAL(16, fontHeight(UiFont::Title));
    TEST_ASSERT_EQUAL(8, fontLineHeight(UiFont::Small));
    TEST_ASSERT_EQUAL(8, fontLineHeight(UiFont::Base));
    TEST_ASSERT_EQUAL(16, fontLineHeight(UiFont::Title));
}

static void test_uifont_scale_matches_set_text_size() {
    TEST_ASSERT_EQUAL(16, fontLineHeight(UiFont::Small, 2));
    TEST_ASSERT_EQUAL(32, fontLineHeight(UiFont::Title, 2));
    TEST_ASSERT_EQUAL(48, fontLineHeight(UiFont::Title, 3));
    // applyFont() clamps scale 0 to 1, so the pure math does the same.
    TEST_ASSERT_EQUAL(8, fontLineHeight(UiFont::Small, 0));
    TEST_ASSERT_EQUAL(16, fontLineHeight(UiFont::Title, 0));
}

static void test_uifont_widths_match_the_builtin_cells() {
    TEST_ASSERT_EQUAL(6, fontWidth(UiFont::Small));
    TEST_ASSERT_EQUAL(6, fontWidth(UiFont::Base));
    TEST_ASSERT_EQUAL(0, fontWidth(UiFont::Title));  // proportional
    TEST_ASSERT_TRUE(fontIsMonospace(UiFont::Small));
    TEST_ASSERT_TRUE(fontIsMonospace(UiFont::Base));
    TEST_ASSERT_FALSE(fontIsMonospace(UiFont::Title));
}

static void test_uifont_small_and_base_are_the_same_m5gfx_font() {
    // M5GFX maps setTextFont(1) to Font0; keep both ids but the same metrics.
    TEST_ASSERT_TRUE(fontId(UiFont::Small) != fontId(UiFont::Base));
    TEST_ASSERT_EQUAL(fontHeight(UiFont::Small), fontHeight(UiFont::Base));
    TEST_ASSERT_EQUAL(fontWidth(UiFont::Small), fontWidth(UiFont::Base));
    TEST_ASSERT_EQUAL(fontLineHeight(UiFont::Small, 2),
                      fontLineHeight(UiFont::Base, 2));
}

// ---------------------------------------------------------------- icons

static int iconInk(const Icon& icon) {
    int ink = 0;
    for (int y = 0; y < icon.height; ++y)
        for (int x = 0; x < icon.width; ++x)
            if (iconPixel(icon, x, y)) ++ink;
    return ink;
}

static int iconRowsWithInk(const Icon& icon) {
    int rows = 0;
    for (int y = 0; y < icon.height; ++y) {
        bool any = false;
        for (int x = 0; x < icon.width; ++x) any = any || iconPixel(icon, x, y);
        if (any) ++rows;
    }
    return rows;
}

static int iconColsWithInk(const Icon& icon) {
    int cols = 0;
    for (int x = 0; x < icon.width; ++x) {
        bool any = false;
        for (int y = 0; y < icon.height; ++y) any = any || iconPixel(icon, x, y);
        if (any) ++cols;
    }
    return cols;
}

static void test_icon_tables_dimensions() {
    TEST_ASSERT_EQUAL(7, kIconCount);
    TEST_ASSERT_EQUAL_UINT8(11, kIconWidth);
    TEST_ASSERT_EQUAL_UINT8(11, kIconHeight);
    for (int i = 0; i < kIconCount; ++i) {
        const Icon& icon = *kIcons[i];
        TEST_ASSERT_NOT_NULL(icon.bits);
        TEST_ASSERT_EQUAL_UINT8(kIconWidth, icon.width);
        TEST_ASSERT_EQUAL_UINT8(kIconHeight, icon.height);
        TEST_ASSERT_EQUAL_UINT8((icon.width + 7) / 8, icon.stride);
        TEST_ASSERT_TRUE(icon.width <= 16);
        TEST_ASSERT_TRUE(icon.height <= 16);
    }
}

static void test_icon_tables_have_ink() {
    for (int i = 0; i < kIconCount; ++i) {
        const Icon& icon = *kIcons[i];
        const int ink = iconInk(icon);
        TEST_ASSERT_TRUE(ink >= 20);
        TEST_ASSERT_TRUE(ink < icon.width * icon.height);
        TEST_ASSERT_TRUE(iconRowsWithInk(icon) >= 8);
        TEST_ASSERT_TRUE(iconColsWithInk(icon) >= 5);
    }
}

static void test_icon_padding_bits_are_clear() {
    for (int i = 0; i < kIconCount; ++i) {
        const Icon& icon = *kIcons[i];
        const int used = icon.width % 8;
        if (used == 0) continue;
        const uint8_t mask = static_cast<uint8_t>((1u << (8 - used)) - 1);
        for (int y = 0; y < icon.height; ++y) {
            const int last = y * icon.stride + icon.stride - 1;
            TEST_ASSERT_EQUAL_HEX8(0, icon.bits[last] & mask);
        }
    }
}

static void test_icon_pixel_bounds_and_known_bits() {
    // Out-of-range reads are clear for every icon, so a caller can clip safely.
    for (int i = 0; i < kIconCount; ++i) {
        const Icon& icon = *kIcons[i];
        TEST_ASSERT_FALSE(iconPixel(icon, -1, 0));
        TEST_ASSERT_FALSE(iconPixel(icon, 0, -1));
        TEST_ASSERT_FALSE(iconPixel(icon, icon.width, 0));
        TEST_ASSERT_FALSE(iconPixel(icon, 0, icon.height));
    }
    TEST_ASSERT_FALSE(iconPixel(kIconCheck, 0, 0));
    TEST_ASSERT_TRUE(iconPixel(kIconCheck, 3, 8));
    TEST_ASSERT_TRUE(iconPixel(kIconCross, 5, 5));
    TEST_ASSERT_FALSE(iconPixel(kIconCross, 5, 0));
    TEST_ASSERT_TRUE(iconPixel(kIconOutbox, 0, 9));
    TEST_ASSERT_TRUE(iconPixel(kIconOutbox, 10, 9));
    TEST_ASSERT_FALSE(iconPixel(kIconOutbox, 5, 5));

    const Icon empty;
    TEST_ASSERT_FALSE(iconPixel(empty, 0, 0));
}

static void test_icons_are_distinct() {
    for (int i = 0; i < kIconCount; ++i) {
        for (int j = i + 1; j < kIconCount; ++j) {
            const Icon& a = *kIcons[i];
            const Icon& b = *kIcons[j];
            TEST_ASSERT_TRUE(a.bits != b.bits);
            TEST_ASSERT_TRUE(std::memcmp(a.bits, b.bits,
                                         static_cast<size_t>(a.height) * a.stride) != 0);
        }
    }
}

}  // namespace

void run_ui_tests() {
    RUN_TEST(test_list_window_empty);
    RUN_TEST(test_list_window_short_list);
    RUN_TEST(test_list_window_seven_rows_fit);
    RUN_TEST(test_list_window_centres_selection);
    RUN_TEST(test_list_window_clamps_selection);
    RUN_TEST(test_list_window_wraps_selection);
    RUN_TEST(test_list_window_last_page_start);
    RUN_TEST(test_list_window_zero_visible);
    RUN_TEST(test_list_move_selection_non_wrap);
    RUN_TEST(test_list_move_selection_wrap);
    RUN_TEST(test_list_move_selection_edges);
    RUN_TEST(test_hint_fit_index_first_match);
    RUN_TEST(test_hint_fit_index_falls_back_to_last);
    RUN_TEST(test_hint_fit_index_empty);
    RUN_TEST(test_hint_bar_geometry);
    RUN_TEST(test_layout_wrap_words_breaks_at_spaces);
    RUN_TEST(test_layout_wrap_words_splits_long_word);
    RUN_TEST(test_layout_wrap_characters_mode);
    RUN_TEST(test_layout_wrap_drops_spaces_at_breaks);
    RUN_TEST(test_layout_wrap_max_lines_and_truncated);
    RUN_TEST(test_layout_wrap_empty_and_zero_width);
    RUN_TEST(test_layout_wrap_line_count_matches_limits);
    RUN_TEST(test_layout_ellipsize_short_text_and_force);
    RUN_TEST(test_layout_ellipsize_keeps_utf8_together);
    RUN_TEST(test_layout_prefix_and_suffix_by_code_point);
    RUN_TEST(test_uifont_ids_are_the_legacy_font_numbers);
    RUN_TEST(test_uifont_heights_match_the_builtin_fonts);
    RUN_TEST(test_uifont_scale_matches_set_text_size);
    RUN_TEST(test_uifont_widths_match_the_builtin_cells);
    RUN_TEST(test_uifont_small_and_base_are_the_same_m5gfx_font);
    RUN_TEST(test_icon_tables_dimensions);
    RUN_TEST(test_icon_tables_have_ink);
    RUN_TEST(test_icon_padding_bits_are_clear);
    RUN_TEST(test_icon_pixel_bounds_and_known_bits);
    RUN_TEST(test_icons_are_distinct);
}

// TNG-03: pure list-window math and hint-bar fallback selection.
#include <unity.h>

#include "ui/HintBar.h"
#include "ui/ListView.h"

using namespace kk::ui;

namespace {

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
}

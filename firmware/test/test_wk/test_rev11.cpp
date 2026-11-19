// REV-11: session summary mistake rows, stage text and list navigation.
#include <unity.h>

#include <cstdint>
#include <vector>

#include "wk/SessionStats.h"

using namespace kk;

namespace {

SessionItem completedItem(uint32_t assignmentId, uint32_t subjectId,
                          uint16_t incorrectMeaning, uint16_t incorrectReading) {
    SessionItem item;
    item.assignmentId = assignmentId;
    item.subjectId = subjectId;
    item.incorrectMeaning = incorrectMeaning;
    item.incorrectReading = incorrectReading;
    return item;
}

// ------------------------------------------------------------- REV-11

static void test_summary_mistakes_only_incomplete_items() {
    const std::vector<SessionItem> completed = {
        completedItem(11, 101, 0, 0),
        completedItem(12, 102, 2, 0),
        completedItem(13, 103, 0, 0),
        completedItem(14, 104, 0, 1),
    };
    const std::vector<SummaryMistake> misses = summaryMistakes(completed);
    TEST_ASSERT_EQUAL_UINT(2, static_cast<unsigned>(misses.size()));
    TEST_ASSERT_EQUAL_UINT32(12, misses[0].assignmentId);
    TEST_ASSERT_EQUAL_UINT32(102, misses[0].subjectId);
    TEST_ASSERT_EQUAL_UINT32(14, misses[1].assignmentId);
    TEST_ASSERT_EQUAL_UINT32(104, misses[1].subjectId);
    // Assignment order from `completed`, not the order of the mistake counts.
    TEST_ASSERT_TRUE(misses[0].assignmentId < misses[1].assignmentId);
}

static void test_summary_mistakes_counts_both_parts() {
    const std::vector<SessionItem> completed = {completedItem(21, 201, 3, 2)};
    const std::vector<SummaryMistake> misses = summaryMistakes(completed);
    TEST_ASSERT_EQUAL_UINT(1, static_cast<unsigned>(misses.size()));
    TEST_ASSERT_EQUAL_UINT(3, static_cast<unsigned>(misses[0].incorrectMeaning));
    TEST_ASSERT_EQUAL_UINT(2, static_cast<unsigned>(misses[0].incorrectReading));
}

static void test_summary_mistakes_empty() {
    TEST_ASSERT_TRUE(summaryMistakes({}).empty());
    const std::vector<SessionItem> clean = {
        completedItem(31, 301, 0, 0),
        completedItem(32, 302, 0, 0),
    };
    TEST_ASSERT_TRUE(summaryMistakes(clean).empty());
}

static void test_summary_stage_change_text() {
    char out[24];
    formatSummaryStage(true, 3, 4, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("SRS 3->4", out);
    formatSummaryStage(true, 4, 4, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("SRS 4", out);
    formatSummaryStage(false, 3, 4, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("pending", out);
    formatSummaryStage(true, 0, 4, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("pending", out);
    formatSummaryStage(true, 3, 0, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("pending", out);
}

static void test_summary_selection_wraps() {
    TEST_ASSERT_EQUAL(1, summaryMoveSelection(0, 3, 1));
    TEST_ASSERT_EQUAL(0, summaryMoveSelection(2, 3, 1));
    TEST_ASSERT_EQUAL(2, summaryMoveSelection(0, 3, -1));
    TEST_ASSERT_EQUAL(1, summaryMoveSelection(1, 3, 0));
    TEST_ASSERT_EQUAL(0, summaryMoveSelection(0, 0, 1));
    // A stale index is clamped before the move.
    TEST_ASSERT_EQUAL(0, summaryMoveSelection(9, 3, 1));
    // The three-row window follows the selection.
    TEST_ASSERT_EQUAL(0, summaryWindowStart(0, 7, 3));
    TEST_ASSERT_EQUAL(0, summaryWindowStart(2, 7, 3));
    TEST_ASSERT_EQUAL(1, summaryWindowStart(3, 7, 3));
    TEST_ASSERT_EQUAL(4, summaryWindowStart(6, 7, 3));
}

static void test_summary_can_next_batch_when_due() {
    TEST_ASSERT_FALSE(summaryCanNextBatch(0));
    TEST_ASSERT_TRUE(summaryCanNextBatch(1));
    TEST_ASSERT_TRUE(summaryCanNextBatch(7));
}

}  // namespace

void run_rev11_tests() {
    RUN_TEST(test_summary_mistakes_only_incomplete_items);
    RUN_TEST(test_summary_mistakes_counts_both_parts);
    RUN_TEST(test_summary_mistakes_empty);
    RUN_TEST(test_summary_stage_change_text);
    RUN_TEST(test_summary_selection_wraps);
    RUN_TEST(test_summary_can_next_batch_when_due);
}

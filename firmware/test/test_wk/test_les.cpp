// LES-01/LES-02/LES-03: batch browse, the local batch quiz and the start plan.
#include <unity.h>

#include <algorithm>
#include <vector>

#include "wk/LessonSession.h"
#include "wk/ReviewQueue.h"
#include "wk/Summary.h"
#include "wk/Types.h"

using namespace kk;

namespace {

Subject lessonKanji(uint32_t id) {
    Subject s;
    s.id = id;
    s.type = SubjectType::Kanji;
    s.level = 1;
    s.characters = "食";
    s.meanings = {{"eat", true, true}};
    s.readings = {{"しょく", true, true, ReadingType::Onyomi}};
    return s;
}

Subject lessonRadical(uint32_t id) {
    Subject s;
    s.id = id;
    s.type = SubjectType::Radical;
    s.level = 1;
    s.meanings = {{"ground", true, true}};
    return s;
}

Assignment lessonAssignment(uint32_t id, uint32_t subjectId) {
    Assignment a;
    a.id = id;
    a.subjectId = subjectId;
    a.type = SubjectType::Kanji;
    a.srsStage = 0;
    return a;
}

std::vector<uint32_t> ids(const ReviewQueue& queue) {
    std::vector<uint32_t> out;
    for (const SessionItem& item : queue.queue()) out.push_back(item.subjectId);
    return out;
}

}  // namespace

static void test_lesson_browse_selects_first_n() {
    LessonBrowse browse;
    browse.begin(5, 3);
    TEST_ASSERT_EQUAL_UINT32(3, browse.selectedCount());
    TEST_ASSERT_EQUAL_UINT32(0, browse.selected()[0]);
    TEST_ASSERT_EQUAL_UINT32(1, browse.selected()[1]);
    TEST_ASSERT_EQUAL_UINT32(2, browse.selected()[2]);

    browse.begin(5, 10);
    TEST_ASSERT_EQUAL_UINT32(5, browse.selectedCount());
    TEST_ASSERT_EQUAL_UINT32(4, browse.selected()[4]);
}

static void test_lesson_browse_toggle_cap() {
    LessonBrowse browse;
    browse.begin(5, 2);
    TEST_ASSERT_EQUAL_UINT32(2, browse.selectedCount());

    // At the cap an unselected row is rejected; a selected row can leave.
    TEST_ASSERT_FALSE(browse.toggle(2, 2));
    TEST_ASSERT_EQUAL_UINT32(2, browse.selectedCount());
    TEST_ASSERT_TRUE(browse.toggle(0, 2));
    TEST_ASSERT_EQUAL_UINT32(1, browse.selectedCount());
    TEST_ASSERT_EQUAL_UINT32(1, browse.selected()[0]);

    TEST_ASSERT_TRUE(browse.toggle(2, 2));
    TEST_ASSERT_EQUAL_UINT32(2, browse.selectedCount());
    TEST_ASSERT_EQUAL_UINT32(1, browse.selected()[0]);
    TEST_ASSERT_EQUAL_UINT32(2, browse.selected()[1]);

    TEST_ASSERT_TRUE(browse.toggle(1, 2));
    TEST_ASSERT_TRUE(browse.toggle(4, 2));
    TEST_ASSERT_EQUAL_UINT32(2, browse.selectedCount());
    TEST_ASSERT_EQUAL_UINT32(2, browse.selected()[0]);
    TEST_ASSERT_EQUAL_UINT32(4, browse.selected()[1]);

    TEST_ASSERT_FALSE(browse.toggle(3, 2));
    TEST_ASSERT_EQUAL_UINT32(2, browse.batchIndex(0));
    TEST_ASSERT_EQUAL_UINT32(4, browse.batchIndex(1));
    TEST_ASSERT_TRUE(browse.toggle(2, 2));
    TEST_ASSERT_EQUAL_UINT32(1, browse.selectedCount());
    TEST_ASSERT_EQUAL_UINT32(4, browse.selected()[0]);
}

static void test_lesson_browse_page_then_card() {
    LessonBrowse browse;
    browse.begin(3, 3);
    browse.setPageCount(3);
    TEST_ASSERT_EQUAL_UINT32(0, browse.card());
    TEST_ASSERT_EQUAL_UINT32(0, browse.page());

    TEST_ASSERT_FALSE(browse.nextPageOrCard());
    TEST_ASSERT_EQUAL_UINT32(1, browse.page());
    TEST_ASSERT_FALSE(browse.nextPageOrCard());
    TEST_ASSERT_EQUAL_UINT32(2, browse.page());

    TEST_ASSERT_TRUE(browse.nextPageOrCard());
    TEST_ASSERT_EQUAL_UINT32(1, browse.card());
    TEST_ASSERT_EQUAL_UINT32(0, browse.page());

    browse.setPageCount(1);
    TEST_ASSERT_TRUE(browse.nextPageOrCard());
    TEST_ASSERT_EQUAL_UINT32(2, browse.card());

    // Last card, last page: there is nowhere left to advance to.
    TEST_ASSERT_TRUE(browse.atEnd());
    TEST_ASSERT_FALSE(browse.nextPageOrCard());
    TEST_ASSERT_EQUAL_UINT32(2, browse.card());
    TEST_ASSERT_EQUAL_UINT32(0, browse.page());
}

static void test_lesson_browse_previous_page_then_card() {
    LessonBrowse browse;
    browse.begin(3, 3);
    browse.setCard(2);
    browse.setPageCount(3);
    browse.nextPageOrCard();
    browse.nextPageOrCard();
    TEST_ASSERT_EQUAL_UINT32(2, browse.card());
    TEST_ASSERT_EQUAL_UINT32(2, browse.page());

    TEST_ASSERT_FALSE(browse.previous());
    TEST_ASSERT_EQUAL_UINT32(1, browse.page());
    TEST_ASSERT_FALSE(browse.previous());
    TEST_ASSERT_EQUAL_UINT32(0, browse.page());

    TEST_ASSERT_TRUE(browse.previous());
    TEST_ASSERT_EQUAL_UINT32(1, browse.card());
    TEST_ASSERT_EQUAL_UINT32(0, browse.page());
}

static void test_lesson_browse_header_format() {
    LessonBrowse browse;
    browse.begin(5, 5);
    browse.setCard(1);
    browse.setPageCount(3);
    char header[24];
    browse.header(header, sizeof header);
    TEST_ASSERT_EQUAL_STRING("2/5 p1/3", header);

    browse.nextPageOrCard();
    browse.header(header, sizeof header);
    TEST_ASSERT_EQUAL_STRING("2/5 p2/3", header);

    browse.begin(0, 5);
    browse.header(header, sizeof header);
    TEST_ASSERT_EQUAL_STRING("0/0 p1/1", header);
}

static void test_lesson_quiz_all_parts_required() {
    std::vector<Subject> subjects = {lessonKanji(1), lessonRadical(2)};
    ReviewQueue queue;
    queue.buildPractice(subjects, 0, 7);
    TEST_ASSERT_EQUAL_UINT32(2, queue.remaining());

    while (!queue.empty()) {
        TEST_ASSERT_TRUE(queue.part() == ReviewPart::Meaning ||
                         queue.part() == ReviewPart::Reading);
        const AnswerOutcome out = queue.answer(AnswerStatus::Correct);
        TEST_ASSERT_TRUE(out.advanced);
    }

    TEST_ASSERT_EQUAL_UINT32(2, queue.completedCount());
    for (const SessionItem& item : queue.completed()) {
        TEST_ASSERT_EQUAL_UINT32(0, item.assignmentId);
        TEST_ASSERT_TRUE(item.meaningDone);
        if (item.subjectId == 1) {
            TEST_ASSERT_TRUE(item.needsReading);
            TEST_ASSERT_TRUE(item.readingDone);
        } else {
            TEST_ASSERT_FALSE(item.needsReading);
        }
    }
}

static void test_lesson_quiz_requeues_wrong_part() {
    std::vector<Subject> subjects = {lessonKanji(1)};
    ReviewQueue queue;
    queue.buildPractice(subjects, 0, 7);

    AnswerOutcome out = queue.answer(AnswerStatus::Incorrect);
    TEST_ASSERT_TRUE(out.counted);
    TEST_ASSERT_TRUE(out.advanced);
    TEST_ASSERT_FALSE(out.requeued);
    TEST_ASSERT_EQUAL(ReviewPart::Reading, queue.part());

    out = queue.answer(AnswerStatus::Correct);
    TEST_ASSERT_TRUE(out.advanced);
    TEST_ASSERT_TRUE(out.requeued);
    TEST_ASSERT_FALSE(out.completed);
    TEST_ASSERT_EQUAL_UINT32(1, queue.remaining());
    TEST_ASSERT_EQUAL_UINT32(0, queue.completedCount());
    TEST_ASSERT_EQUAL(ReviewPart::Meaning, queue.part());
    TEST_ASSERT_EQUAL_UINT32(1, queue.current()->incorrectMeaning);
    TEST_ASSERT_TRUE(queue.current()->readingDone);
    TEST_ASSERT_FALSE(queue.current()->meaningDone);
}

static void test_lesson_quiz_warning_retries_same_part() {
    std::vector<Subject> subjects = {lessonKanji(1)};
    ReviewQueue queue;
    queue.buildPractice(subjects, 0, 7);

    AnswerOutcome out = queue.answer(AnswerStatus::Warning);
    TEST_ASSERT_FALSE(out.counted);
    TEST_ASSERT_FALSE(out.advanced);
    TEST_ASSERT_EQUAL(ReviewPart::Meaning, queue.part());
    TEST_ASSERT_EQUAL_UINT32(1, queue.remaining());
    TEST_ASSERT_EQUAL_UINT32(0, queue.completedCount());

    out = queue.answer(AnswerStatus::Correct);
    TEST_ASSERT_TRUE(out.advanced);
    TEST_ASSERT_EQUAL(ReviewPart::Reading, queue.part());
}

static void test_lesson_quiz_shuffle_is_seeded() {
    std::vector<Subject> subjects;
    for (uint32_t id = 1; id <= 8; ++id) subjects.push_back(lessonKanji(id));

    ReviewQueue first;
    first.buildPractice(subjects, 0, 42);
    ReviewQueue again;
    again.buildPractice(subjects, 0, 42);
    TEST_ASSERT_TRUE(ids(first) == ids(again));

    ReviewQueue other;
    other.buildPractice(subjects, 0, 43);
    TEST_ASSERT_FALSE(ids(first) == ids(other));

    // Every subject is present exactly once, whatever the order.
    std::vector<uint32_t> sorted = ids(first);
    std::sort(sorted.begin(), sorted.end());
    for (uint32_t id = 1; id <= 8; ++id) TEST_ASSERT_EQUAL_UINT32(id, sorted[id - 1]);
}

static void test_lesson_quiz_skip_setting_bypasses_quiz() {
    TEST_ASSERT_EQUAL(LessonNextStep::Quiz, lessonNextStep(true));
    TEST_ASSERT_EQUAL(LessonNextStep::Start, lessonNextStep(false));
}

static void test_summary_remove_lesson_subjects() {
    Summary summary;
    SummaryBucket first;
    first.availableAt = 100;
    first.count = 3;
    first.subjectIds = {1, 2, 3};
    SummaryBucket second;
    second.availableAt = 200;
    second.count = 2;
    second.subjectIds = {4, 5};
    summary.lessons = {first, second};

    TEST_ASSERT_TRUE(summary.removeLessonSubjects({2, 4}));
    TEST_ASSERT_EQUAL_UINT32(2, summary.lessons.size());
    TEST_ASSERT_EQUAL_UINT32(2, summary.lessons[0].count);
    TEST_ASSERT_EQUAL_UINT32(1, summary.lessons[0].subjectIds[0]);
    TEST_ASSERT_EQUAL_UINT32(3, summary.lessons[0].subjectIds[1]);
    TEST_ASSERT_EQUAL_UINT32(1, summary.lessons[1].count);
    TEST_ASSERT_EQUAL_UINT32(5, summary.lessons[1].subjectIds[0]);
    TEST_ASSERT_EQUAL_UINT32(3, summary.totalLessons());
}

static void test_summary_remove_lesson_bucket_count() {
    Summary summary;
    SummaryBucket bucket;
    bucket.availableAt = 100;
    bucket.count = 2;
    bucket.subjectIds = {7, 8};
    summary.lessons = {bucket};

    TEST_ASSERT_TRUE(summary.removeLessonSubjects({7, 8}));
    TEST_ASSERT_TRUE(summary.lessons.empty());
    TEST_ASSERT_EQUAL_UINT32(0, summary.totalLessons());
    TEST_ASSERT_FALSE(summary.removeLessonSubjects({7}));
}

static void test_lesson_start_classify_http() {
    TEST_ASSERT_EQUAL(LessonStartResult::Started, classifyStartResult(200));
    TEST_ASSERT_EQUAL(LessonStartResult::Started, classifyStartResult(201));
    TEST_ASSERT_EQUAL(LessonStartResult::Started, classifyStartResult(204));
    TEST_ASSERT_EQUAL(LessonStartResult::AlreadyStarted, classifyStartResult(422));
    TEST_ASSERT_EQUAL(LessonStartResult::Auth, classifyStartResult(401));
    TEST_ASSERT_EQUAL(LessonStartResult::Auth, classifyStartResult(403));
    TEST_ASSERT_EQUAL(LessonStartResult::Failed, classifyStartResult(0));
    TEST_ASSERT_EQUAL(LessonStartResult::Failed, classifyStartResult(404));
    TEST_ASSERT_EQUAL(LessonStartResult::Failed, classifyStartResult(500));
}

static void test_lesson_start_plan_keeps_failed_items() {
    std::vector<Assignment> assignments = {
        lessonAssignment(10, 1),
        lessonAssignment(11, 2),
        lessonAssignment(12, 3),
    };
    LessonStartPlan plan;
    plan.build(assignments, {0, 1, 2});
    TEST_ASSERT_EQUAL_UINT32(3, plan.size());

    plan.markStarted(0);
    plan.markFailed(1);
    TEST_ASSERT_EQUAL_UINT32(1, plan.startedCount());
    TEST_ASSERT_EQUAL_UINT32(1, plan.failedCount());
    TEST_ASSERT_EQUAL_UINT32(2, plan.unstartedCount());
    TEST_ASSERT_FALSE(plan.allStarted());
    TEST_ASSERT_EQUAL_UINT32(3, plan.items().size());
    TEST_ASSERT_EQUAL(LessonStartItem::State::Failed, plan.items()[1].state);
    TEST_ASSERT_NOT_NULL(plan.nextPending());
    TEST_ASSERT_EQUAL_UINT32(12, plan.nextPending()->assignmentId);

    plan.resetFailed();
    TEST_ASSERT_EQUAL_UINT32(0, plan.failedCount());
    TEST_ASSERT_EQUAL_UINT32(2, plan.unstartedCount());
    TEST_ASSERT_NOT_NULL(plan.nextPending());
    TEST_ASSERT_EQUAL_UINT32(11, plan.nextPending()->assignmentId);
}

static void test_lesson_start_plan_all_started() {
    std::vector<Assignment> assignments = {
        lessonAssignment(10, 1),
        lessonAssignment(11, 2),
    };
    LessonStartPlan plan;
    plan.build(assignments, {0, 1});
    plan.markStarted(0);
    plan.markStarted(1);
    TEST_ASSERT_TRUE(plan.allStarted());
    TEST_ASSERT_EQUAL_UINT32(0, plan.unstartedCount());
    TEST_ASSERT_EQUAL_UINT32(0, plan.failedCount());
    TEST_ASSERT_EQUAL_PTR(nullptr, plan.nextPending());
}

void run_les_tests() {
    RUN_TEST(test_lesson_browse_selects_first_n);
    RUN_TEST(test_lesson_browse_toggle_cap);
    RUN_TEST(test_lesson_browse_page_then_card);
    RUN_TEST(test_lesson_browse_previous_page_then_card);
    RUN_TEST(test_lesson_browse_header_format);
    RUN_TEST(test_lesson_quiz_all_parts_required);
    RUN_TEST(test_lesson_quiz_requeues_wrong_part);
    RUN_TEST(test_lesson_quiz_warning_retries_same_part);
    RUN_TEST(test_lesson_quiz_shuffle_is_seeded);
    RUN_TEST(test_lesson_quiz_skip_setting_bypasses_quiz);
    RUN_TEST(test_summary_remove_lesson_subjects);
    RUN_TEST(test_summary_remove_lesson_bucket_count);
    RUN_TEST(test_lesson_start_classify_http);
    RUN_TEST(test_lesson_start_plan_keeps_failed_items);
    RUN_TEST(test_lesson_start_plan_all_started);
}

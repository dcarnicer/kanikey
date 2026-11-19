// FIX-09: a part answered wrong is not asked again right after the other part
// was answered correctly; the item returns kRequeueGap positions later.
#include <unity.h>

#include <vector>

#include "wk/ReviewQueue.h"
#include "wk/Types.h"

using namespace kk;

namespace {

Subject kanji(uint32_t id) {
    Subject s;
    s.id = id;
    s.type = SubjectType::Kanji;
    s.level = 5;
    s.meanings = {{"eat", true, true}};
    s.readings = {{"しょく", true, true, ReadingType::Onyomi}};
    return s;
}

Assignment assignment(uint32_t id, uint32_t subjectId) {
    Assignment a;
    a.id = id;
    a.subjectId = subjectId;
    a.type = SubjectType::Kanji;
    a.srsStage = 3;
    a.availableAt = 100;
    return a;
}

// Five due subjects, no limit and shuffled: the item that fails a part is
// followed by four others, so a requeue lands it at the back of the queue.
void buildFive(ReviewQueue* q, std::vector<Subject>* subjects,
               std::vector<Assignment>* assignments) {
    for (uint32_t i = 1; i <= 5; ++i) subjects->push_back(kanji(i));
    for (uint32_t i = 1; i <= 5; ++i) assignments->push_back(assignment(10 + i, i));
    q->build(*assignments, *subjects, 1000, 0);
}

// Two due subjects, no limit: interleaving must put the answered item behind
// exactly one other item regardless of the shuffle.
void buildTwo(ReviewQueue* q, std::vector<Subject>* subjects,
              std::vector<Assignment>* assignments) {
    for (uint32_t i = 1; i <= 2; ++i) subjects->push_back(kanji(i));
    for (uint32_t i = 1; i <= 2; ++i) assignments->push_back(assignment(10 + i, i));
    q->build(*assignments, *subjects, 1000, 0);
}

}  // namespace

static void test_wrong_meaning_then_correct_reading_requeues() {
    const Subject s = kanji(1);
    ReviewQueue q;
    q.build({assignment(10, s.id)}, {s}, 1000);

    AnswerOutcome o = q.answer(AnswerStatus::Incorrect);
    TEST_ASSERT_TRUE(o.counted);
    TEST_ASSERT_TRUE(o.advanced);
    TEST_ASSERT_FALSE(o.requeued);
    TEST_ASSERT_EQUAL(ReviewPart::Reading, q.part());

    o = q.answer(AnswerStatus::Correct);
    TEST_ASSERT_TRUE(o.advanced);
    TEST_ASSERT_TRUE(o.requeued);
    TEST_ASSERT_FALSE(o.completed);
    TEST_ASSERT_EQUAL(1, q.remaining());
    TEST_ASSERT_EQUAL(0, q.completedCount());
    // The failed meaning is pending again, not the reading that was just given.
    TEST_ASSERT_EQUAL(ReviewPart::Meaning, q.part());
    TEST_ASSERT_EQUAL(1, q.current()->incorrectMeaning);
    TEST_ASSERT_EQUAL(0, q.current()->incorrectReading);
    TEST_ASSERT_FALSE(q.current()->meaningDone);
    TEST_ASSERT_TRUE(q.current()->readingDone);
    // Both attempted flags were reset by the requeue.
    TEST_ASSERT_FALSE(q.current()->meaningAttempted);
    TEST_ASSERT_FALSE(q.current()->readingAttempted);

    // The failed part is asked and completed on the next pass.
    o = q.answer(AnswerStatus::Correct);
    TEST_ASSERT_TRUE(o.completed);
    TEST_ASSERT_TRUE(q.empty());
    TEST_ASSERT_EQUAL(1, q.completedCount());
    TEST_ASSERT_EQUAL(1, q.completed().front().incorrectMeaning);
}

static void test_wrong_reading_then_correct_meaning_requeues() {
    const Subject s = kanji(1);
    ReviewQueue q;
    q.setMeaningFirst(false);
    q.build({assignment(10, s.id)}, {s}, 1000);
    TEST_ASSERT_EQUAL(ReviewPart::Reading, q.part());

    AnswerOutcome o = q.answer(AnswerStatus::Incorrect);
    TEST_ASSERT_TRUE(o.counted);
    TEST_ASSERT_FALSE(o.requeued);
    TEST_ASSERT_EQUAL(ReviewPart::Meaning, q.part());

    o = q.answer(AnswerStatus::Correct);
    TEST_ASSERT_TRUE(o.advanced);
    TEST_ASSERT_TRUE(o.requeued);
    TEST_ASSERT_FALSE(o.completed);
    TEST_ASSERT_EQUAL(1, q.remaining());
    TEST_ASSERT_EQUAL(ReviewPart::Reading, q.part());
    TEST_ASSERT_EQUAL(1, q.current()->incorrectReading);
    TEST_ASSERT_EQUAL(0, q.current()->incorrectMeaning);
    TEST_ASSERT_TRUE(q.current()->meaningDone);
    TEST_ASSERT_FALSE(q.current()->readingDone);
    TEST_ASSERT_FALSE(q.current()->meaningAttempted);
    TEST_ASSERT_FALSE(q.current()->readingAttempted);

    o = q.answer(AnswerStatus::Correct);
    TEST_ASSERT_TRUE(o.completed);
    TEST_ASSERT_TRUE(q.empty());
    TEST_ASSERT_EQUAL(1, q.completed().front().incorrectReading);
}

static void test_requeued_item_returns_after_four() {
    std::vector<Subject> subjects;
    std::vector<Assignment> assignments;
    ReviewQueue q;
    buildFive(&q, &subjects, &assignments);
    TEST_ASSERT_EQUAL(5, q.remaining());

    const uint32_t failedSubject = q.current()->subjectId;
    const uint32_t secondSubject = q.queue()[1].subjectId;

    q.answer(AnswerStatus::Incorrect);  // meaning
    const AnswerOutcome o = q.answer(AnswerStatus::Correct);  // reading
    TEST_ASSERT_TRUE(o.requeued);
    TEST_ASSERT_EQUAL(5, q.remaining());

    // Four other items between the failed item and its return.
    TEST_ASSERT_EQUAL(secondSubject, q.current()->subjectId);
    TEST_ASSERT_EQUAL(failedSubject, q.queue()[4].subjectId);
    for (size_t i = 0; i < 4; ++i)
        TEST_ASSERT_NOT_EQUAL(failedSubject, q.queue()[i].subjectId);
    // The requeued copy holds the correct reading and waits for the meaning.
    TEST_ASSERT_FALSE(q.queue()[4].meaningDone);
    TEST_ASSERT_TRUE(q.queue()[4].readingDone);
    TEST_ASSERT_FALSE(q.queue()[4].meaningAttempted);
    TEST_ASSERT_FALSE(q.queue()[4].readingAttempted);
}

static void test_both_parts_correct_completes() {
    const Subject s = kanji(1);
    ReviewQueue q;
    q.build({assignment(10, s.id)}, {s}, 1000);

    AnswerOutcome o = q.answer(AnswerStatus::Correct);
    TEST_ASSERT_TRUE(o.advanced);
    TEST_ASSERT_FALSE(o.requeued);
    TEST_ASSERT_FALSE(o.completed);
    TEST_ASSERT_EQUAL(1, q.remaining());
    TEST_ASSERT_EQUAL(ReviewPart::Reading, q.part());

    o = q.answer(AnswerStatus::Correct);
    TEST_ASSERT_TRUE(o.advanced);
    TEST_ASSERT_FALSE(o.requeued);
    TEST_ASSERT_TRUE(o.completed);
    TEST_ASSERT_TRUE(q.empty());
    TEST_ASSERT_EQUAL(1, q.completedCount());
}

// ------------------------------------------------------------- REV-09

static void test_back_to_back_on_asks_other_part() {
    const Subject s = kanji(1);
    ReviewQueue q;
    q.setBackToBack(true);
    q.build({assignment(10, s.id)}, {s}, 1000);
    TEST_ASSERT_EQUAL(ReviewPart::Meaning, q.part());

    const AnswerOutcome o = q.answer(AnswerStatus::Correct);
    TEST_ASSERT_TRUE(o.advanced);
    TEST_ASSERT_FALSE(o.completed);
    TEST_ASSERT_EQUAL(ReviewPart::Reading, q.part());
    TEST_ASSERT_EQUAL(1, q.remaining());
    TEST_ASSERT_EQUAL(s.id, q.current()->subjectId);
}

static void test_back_to_back_off_interleaves_two_items() {
    std::vector<Subject> subjects;
    std::vector<Assignment> assignments;
    ReviewQueue q;
    q.setBackToBack(false);
    buildTwo(&q, &subjects, &assignments);
    const uint32_t first = q.current()->subjectId;
    const uint32_t second = q.queue()[1].subjectId;

    const AnswerOutcome o = q.answer(AnswerStatus::Correct);
    TEST_ASSERT_TRUE(o.advanced);
    TEST_ASSERT_FALSE(o.completed);
    TEST_ASSERT_EQUAL(2, q.remaining());
    TEST_ASSERT_EQUAL(second, q.current()->subjectId);
    TEST_ASSERT_EQUAL(first, q.queue()[1].subjectId);
}

static void test_back_to_back_off_preserves_done_part() {
    std::vector<Subject> subjects;
    std::vector<Assignment> assignments;
    ReviewQueue q;
    q.setBackToBack(false);
    buildTwo(&q, &subjects, &assignments);
    const uint32_t first = q.current()->subjectId;
    q.answer(AnswerStatus::Correct);  // meaning of the first item

    const SessionItem* moved = nullptr;
    for (const SessionItem& item : q.queue())
        if (item.subjectId == first) moved = &item;
    TEST_ASSERT_NOT_NULL(moved);
    TEST_ASSERT_TRUE(moved->meaningDone);
    TEST_ASSERT_FALSE(moved->readingDone);
    TEST_ASSERT_TRUE(moved->meaningAttempted);
    TEST_ASSERT_FALSE(moved->readingAttempted);
}

static void test_back_to_back_off_last_part_completes() {
    const Subject s = kanji(1);
    ReviewQueue q;
    q.setBackToBack(false);
    q.build({assignment(10, s.id)}, {s}, 1000);

    AnswerOutcome o = q.answer(AnswerStatus::Correct);
    TEST_ASSERT_FALSE(o.completed);
    TEST_ASSERT_EQUAL(ReviewPart::Reading, q.part());
    TEST_ASSERT_EQUAL(1, q.remaining());

    o = q.answer(AnswerStatus::Correct);
    TEST_ASSERT_TRUE(o.advanced);
    TEST_ASSERT_TRUE(o.completed);
    TEST_ASSERT_TRUE(q.empty());
    TEST_ASSERT_EQUAL(1, q.completedCount());
}

static void test_back_to_back_off_wrong_requeues_other_part() {
    std::vector<Subject> subjects;
    std::vector<Assignment> assignments;
    ReviewQueue q;
    q.setBackToBack(false);
    buildTwo(&q, &subjects, &assignments);
    const uint32_t first = q.current()->subjectId;
    const uint32_t second = q.queue()[1].subjectId;

    const AnswerOutcome o = q.answer(AnswerStatus::Incorrect);
    TEST_ASSERT_TRUE(o.counted);
    TEST_ASSERT_TRUE(o.advanced);
    TEST_ASSERT_FALSE(o.completed);
    TEST_ASSERT_EQUAL(second, q.current()->subjectId);
    TEST_ASSERT_EQUAL(first, q.queue()[1].subjectId);
    const SessionItem& failed = q.queue()[1];
    TEST_ASSERT_FALSE(failed.meaningDone);
    TEST_ASSERT_FALSE(failed.readingDone);
    TEST_ASSERT_TRUE(failed.meaningAttempted);
    TEST_ASSERT_FALSE(failed.readingAttempted);
}

static void test_back_to_back_off_single_item_does_not_stall() {
    const Subject s = kanji(1);
    ReviewQueue q;
    q.setBackToBack(false);
    q.build({assignment(10, s.id)}, {s}, 1000);

    // A wrong meaning still asks the reading (the same item) next, so the
    // single-item queue cannot spin on the failed part.
    AnswerOutcome o = q.answer(AnswerStatus::Incorrect);
    TEST_ASSERT_TRUE(o.advanced);
    TEST_ASSERT_EQUAL(ReviewPart::Reading, q.part());
    TEST_ASSERT_EQUAL(1, q.remaining());

    // The correct reading requeues the item; the failed meaning is asked again.
    o = q.answer(AnswerStatus::Correct);
    TEST_ASSERT_TRUE(o.advanced);
    TEST_ASSERT_FALSE(o.completed);
    TEST_ASSERT_EQUAL(ReviewPart::Meaning, q.part());
    TEST_ASSERT_EQUAL(1, q.remaining());

    o = q.answer(AnswerStatus::Correct);
    TEST_ASSERT_TRUE(o.completed);
    TEST_ASSERT_TRUE(q.empty());
}

static void test_back_to_back_off_same_item_not_first() {
    std::vector<Subject> subjects;
    std::vector<Assignment> assignments;
    ReviewQueue q;
    q.setBackToBack(false);
    buildTwo(&q, &subjects, &assignments);
    const uint32_t first = q.current()->subjectId;

    q.answer(AnswerStatus::Correct);
    TEST_ASSERT_NOT_EQUAL(first, q.current()->subjectId);
    TEST_ASSERT_NOT_EQUAL(first, q.queue().front().subjectId);
}

// ------------------------------------------------------------- REV-05

static void test_wrap_up_keeps_current_and_partial() {
    std::vector<Subject> subjects;
    std::vector<Assignment> assignments;
    ReviewQueue q;
    q.setBackToBack(false);
    buildFive(&q, &subjects, &assignments);

    const uint32_t done = q.current()->subjectId;
    q.answer(AnswerStatus::Correct);  // meaning of the first item
    const uint32_t current = q.current()->subjectId;
    TEST_ASSERT_NOT_EQUAL(done, current);
    const ReviewPart part = q.part();

    TEST_ASSERT_EQUAL(2, q.wrapUp());
    TEST_ASSERT_EQUAL(2, q.remaining());
    TEST_ASSERT_EQUAL(current, q.current()->subjectId);
    TEST_ASSERT_EQUAL(part, q.part());
    TEST_ASSERT_EQUAL(done, q.queue()[1].subjectId);
    TEST_ASSERT_TRUE(q.queue()[1].meaningDone);
    TEST_ASSERT_FALSE(q.queue()[1].readingDone);
}

static void test_wrap_up_drops_fresh_items() {
    std::vector<Subject> subjects;
    std::vector<Assignment> assignments;
    ReviewQueue q;
    buildFive(&q, &subjects, &assignments);
    const uint32_t current = q.current()->subjectId;
    const ReviewPart part = q.part();

    TEST_ASSERT_EQUAL(1, q.wrapUp());
    TEST_ASSERT_EQUAL(1, q.remaining());
    TEST_ASSERT_EQUAL(1, q.queue().size());
    TEST_ASSERT_EQUAL(current, q.current()->subjectId);
    TEST_ASSERT_EQUAL(part, q.part());
}

static void test_wrap_up_is_idempotent() {
    std::vector<Subject> subjects;
    std::vector<Assignment> assignments;
    ReviewQueue q;
    q.setBackToBack(false);
    buildFive(&q, &subjects, &assignments);

    q.answer(AnswerStatus::Correct);  // first item is now partial
    q.answer(AnswerStatus::Correct);  // second item is now partial
    TEST_ASSERT_EQUAL(3, q.wrapUp());

    std::vector<uint32_t> ids;
    for (const SessionItem& item : q.queue()) ids.push_back(item.subjectId);
    const ReviewPart part = q.part();

    TEST_ASSERT_EQUAL(3, q.wrapUp());
    TEST_ASSERT_EQUAL(3, q.remaining());
    TEST_ASSERT_EQUAL(part, q.part());
    TEST_ASSERT_EQUAL(static_cast<unsigned>(ids.size()), static_cast<unsigned>(q.queue().size()));
    for (size_t i = 0; i < ids.size(); ++i)
        TEST_ASSERT_EQUAL(ids[i], q.queue()[i].subjectId);
}

static void test_wrap_up_keeps_reading_only_item() {
    std::vector<Subject> subjects;
    std::vector<Assignment> assignments;
    ReviewQueue q;
    q.setMeaningFirst(false);
    q.setBackToBack(false);
    buildTwo(&q, &subjects, &assignments);

    const uint32_t readingDone = q.current()->subjectId;
    const uint32_t current = q.queue()[1].subjectId;
    TEST_ASSERT_EQUAL(ReviewPart::Reading, q.part());
    q.answer(AnswerStatus::Correct);  // reading done, item interleaved back
    TEST_ASSERT_EQUAL(2, q.remaining());
    TEST_ASSERT_EQUAL(current, q.current()->subjectId);
    TEST_ASSERT_EQUAL(ReviewPart::Reading, q.part());

    TEST_ASSERT_EQUAL(2, q.wrapUp());
    TEST_ASSERT_EQUAL(2, q.remaining());
    TEST_ASSERT_EQUAL(readingDone, q.queue()[1].subjectId);
    TEST_ASSERT_TRUE(q.queue()[1].readingDone);
    TEST_ASSERT_FALSE(q.queue()[1].meaningDone);
}

static void test_wrap_up_count_after_current_completes() {
    std::vector<Subject> subjects;
    std::vector<Assignment> assignments;
    ReviewQueue q;
    buildFive(&q, &subjects, &assignments);

    q.answer(AnswerStatus::Correct);  // current meaning, reading still pending
    TEST_ASSERT_EQUAL(1, q.wrapUp());  // the untouched rest is dropped
    TEST_ASSERT_EQUAL(1, q.remaining());
    TEST_ASSERT_EQUAL(ReviewPart::Reading, q.part());

    const AnswerOutcome o = q.answer(AnswerStatus::Correct);
    TEST_ASSERT_TRUE(o.completed);
    TEST_ASSERT_EQUAL(0, q.remaining());
    TEST_ASSERT_EQUAL(1, q.completedCount());
    // A later wrap up with nothing left is harmless.
    TEST_ASSERT_EQUAL(0, q.wrapUp());
}

void run_queue_tests() {
    RUN_TEST(test_wrong_meaning_then_correct_reading_requeues);
    RUN_TEST(test_wrong_reading_then_correct_meaning_requeues);
    RUN_TEST(test_requeued_item_returns_after_four);
    RUN_TEST(test_both_parts_correct_completes);
    RUN_TEST(test_back_to_back_on_asks_other_part);
    RUN_TEST(test_back_to_back_off_interleaves_two_items);
    RUN_TEST(test_back_to_back_off_preserves_done_part);
    RUN_TEST(test_back_to_back_off_last_part_completes);
    RUN_TEST(test_back_to_back_off_wrong_requeues_other_part);
    RUN_TEST(test_back_to_back_off_single_item_does_not_stall);
    RUN_TEST(test_back_to_back_off_same_item_not_first);
    RUN_TEST(test_wrap_up_keeps_current_and_partial);
    RUN_TEST(test_wrap_up_drops_fresh_items);
    RUN_TEST(test_wrap_up_is_idempotent);
    RUN_TEST(test_wrap_up_keeps_reading_only_item);
    RUN_TEST(test_wrap_up_count_after_current_completes);
}

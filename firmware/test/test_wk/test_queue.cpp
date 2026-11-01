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

void run_queue_tests() {
    RUN_TEST(test_wrong_meaning_then_correct_reading_requeues);
    RUN_TEST(test_wrong_reading_then_correct_meaning_requeues);
    RUN_TEST(test_requeued_item_returns_after_four);
    RUN_TEST(test_both_parts_correct_completes);
}

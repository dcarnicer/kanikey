// FIX-09: a part answered wrong is not asked again right after the other part
// was answered correctly; the item returns kRequeueGap positions later.
#include <unity.h>

#include <utility>
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
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
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
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
    q.setFirstPart(FirstPart::Reading);
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
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
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
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
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
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
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
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
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
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
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
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
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
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
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
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
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
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
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
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
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
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
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
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
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
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
    q.setFirstPart(FirstPart::Reading);
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
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
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

// ------------------------------------------------------------- R4 / R5 (WaniKani)

// Builds `count` due kanji (ids 1..count) with the queue's current settings.
static void buildKanji(ReviewQueue* q, std::vector<Subject>* subjects,
                       std::vector<Assignment>* assignments, uint32_t count, uint32_t seed) {
    for (uint32_t i = 1; i <= count; ++i) subjects->push_back(kanji(i));
    for (uint32_t i = 1; i <= count; ++i) assignments->push_back(assignment(10 + i, i));
    q->build(*assignments, *subjects, 1000, 0, seed);
}

// Answers every part correctly; returns (subjectId, part) in the order asked.
static std::vector<std::pair<uint32_t, ReviewPart>> answerAllCorrect(ReviewQueue* q) {
    std::vector<std::pair<uint32_t, ReviewPart>> log;
    while (!q->empty()) {
        log.emplace_back(q->current()->subjectId, q->part());
        q->answer(AnswerStatus::Correct);
    }
    return log;
}

static void test_first_part_nvs_values() {
    TEST_ASSERT_EQUAL(FirstPart::Meaning, firstPartFromValue(1));
    TEST_ASSERT_EQUAL(FirstPart::Reading, firstPartFromValue(0));
    TEST_ASSERT_EQUAL(FirstPart::Random, firstPartFromValue(2));
    // Unknown values fall back to the R4 default.
    TEST_ASSERT_EQUAL(FirstPart::Random, firstPartFromValue(7));
    TEST_ASSERT_EQUAL(FirstPart::Random, firstPartFromValue(-1));
}

static void test_queue_defaults_are_random_and_back_to_back_off() {
    ReviewQueue q;
    TEST_ASSERT_EQUAL(FirstPart::Random, q.firstPart());
    TEST_ASSERT_FALSE(q.backToBack());
}

static void test_random_first_part_draws_both_orders() {
    std::vector<Subject> subjects;
    std::vector<Assignment> assignments;
    ReviewQueue q;
    buildKanji(&q, &subjects, &assignments, 40, 0x1234u);
    int readingFirst = 0;
    for (const SessionItem& item : q.queue())
        if (item.readingFirst) ++readingFirst;
    TEST_ASSERT_TRUE(readingFirst > 0);
    TEST_ASSERT_TRUE(readingFirst < 40);
    // The question on screen follows the front item's draw.
    TEST_ASSERT_EQUAL(q.current()->readingFirst ? ReviewPart::Reading : ReviewPart::Meaning,
                      q.part());
}

static void test_random_first_part_is_fixed_by_seed() {
    std::vector<Subject> subjectsA, subjectsB;
    std::vector<Assignment> assignmentsA, assignmentsB;
    ReviewQueue a, b;
    buildKanji(&a, &subjectsA, &assignmentsA, 20, 77u);
    buildKanji(&b, &subjectsB, &assignmentsB, 20, 77u);
    TEST_ASSERT_EQUAL(static_cast<int>(a.remaining()), static_cast<int>(b.remaining()));
    const auto logA = answerAllCorrect(&a);
    const auto logB = answerAllCorrect(&b);
    TEST_ASSERT_EQUAL(static_cast<int>(logA.size()), static_cast<int>(logB.size()));
    for (size_t i = 0; i < logA.size(); ++i) {
        TEST_ASSERT_EQUAL(logA[i].first, logB[i].first);
        TEST_ASSERT_EQUAL(logA[i].second, logB[i].second);
    }
}

static void test_random_each_item_asks_both_parts_once() {
    for (const bool backToBack : {true, false}) {
        std::vector<Subject> subjects;
        std::vector<Assignment> assignments;
        ReviewQueue q;
        q.setBackToBack(backToBack);
        buildKanji(&q, &subjects, &assignments, 12, 0x51u);
        const auto log = answerAllCorrect(&q);
        TEST_ASSERT_EQUAL(12, q.completedCount());
        TEST_ASSERT_EQUAL(24, static_cast<int>(log.size()));
        for (uint32_t id = 1; id <= 12; ++id) {
            int meaning = 0;
            int reading = 0;
            for (const auto& entry : log) {
                if (entry.first != id) continue;
                if (entry.second == ReviewPart::Meaning) ++meaning;
                if (entry.second == ReviewPart::Reading) ++reading;
            }
            TEST_ASSERT_EQUAL(1, meaning);
            TEST_ASSERT_EQUAL(1, reading);
        }
    }
}

static void test_random_back_to_back_on_keeps_parts_adjacent() {
    std::vector<Subject> subjects;
    std::vector<Assignment> assignments;
    ReviewQueue q;
    q.setBackToBack(true);
    buildKanji(&q, &subjects, &assignments, 12, 0x51u);
    const auto log = answerAllCorrect(&q);
    int readingFirstItems = 0;
    for (size_t i = 0; i + 1 < log.size(); i += 2) {
        // Both parts of one item in a row, in the item's own random order.
        TEST_ASSERT_EQUAL(log[i].first, log[i + 1].first);
        TEST_ASSERT_NOT_EQUAL(log[i].second, log[i + 1].second);
        if (log[i].second == ReviewPart::Reading) ++readingFirstItems;
    }
    // The order differs between items, so both kinds show up in the batch.
    TEST_ASSERT_TRUE(readingFirstItems > 0);
    TEST_ASSERT_TRUE(readingFirstItems < 12);
}

static void test_random_back_to_back_off_interleaves() {
    std::vector<Subject> subjects;
    std::vector<Assignment> assignments;
    ReviewQueue q;
    q.setBackToBack(false);
    buildKanji(&q, &subjects, &assignments, 5, 0x51u);
    const auto log = answerAllCorrect(&q);
    TEST_ASSERT_EQUAL(10, static_cast<int>(log.size()));
    // The first item's second part is never the very next question.
    TEST_ASSERT_NOT_EQUAL(log[0].first, log[1].first);
}

static void test_random_mistake_asks_failed_part_again() {
    const Subject s = kanji(1);
    ReviewQueue q;
    q.setBackToBack(true);
    q.build({assignment(10, s.id)}, {s}, 1000, 0, 0x7Fu);
    const bool readingFirst = q.current()->readingFirst;
    const ReviewPart failed = readingFirst ? ReviewPart::Reading : ReviewPart::Meaning;
    const ReviewPart other = readingFirst ? ReviewPart::Meaning : ReviewPart::Reading;
    TEST_ASSERT_EQUAL(failed, q.part());

    q.answer(AnswerStatus::Incorrect);
    TEST_ASSERT_EQUAL(other, q.part());  // the other part comes next
    const AnswerOutcome o = q.answer(AnswerStatus::Correct);
    TEST_ASSERT_TRUE(o.requeued);
    TEST_ASSERT_EQUAL(failed, q.part());  // the failed part is asked again
    // Requeues keep the drawn order; they do not redraw it.
    TEST_ASSERT_EQUAL(readingFirst, q.current()->readingFirst);
    TEST_ASSERT_TRUE(q.answer(AnswerStatus::Correct).completed);
}

static void test_random_radical_asks_meaning_only() {
    Subject radical;
    radical.id = 9;
    radical.type = SubjectType::Radical;
    radical.level = 1;
    radical.meanings = {{"ground", true, true}};
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        ReviewQueue q;
        q.build({assignment(19, radical.id)}, {radical}, 1000, 0, seed);
        TEST_ASSERT_EQUAL(ReviewPart::Meaning, q.part());
        TEST_ASSERT_TRUE(q.answer(AnswerStatus::Correct).completed);
    }
}

static void test_practice_is_random_and_back_to_back_regardless_of_settings() {
    std::vector<Subject> subjects;
    for (uint32_t i = 1; i <= 12; ++i) subjects.push_back(kanji(i));
    ReviewQueue q;
    q.setFirstPart(FirstPart::Meaning);  // the lesson quiz ignores R4/R5
    q.setBackToBack(false);
    q.buildPractice(subjects, 0, 0x51u);
    const auto log = answerAllCorrect(&q);
    TEST_ASSERT_EQUAL(24, static_cast<int>(log.size()));
    int readingFirstItems = 0;
    for (size_t i = 0; i + 1 < log.size(); i += 2) {
        TEST_ASSERT_EQUAL(log[i].first, log[i + 1].first);
        if (log[i].second == ReviewPart::Reading) ++readingFirstItems;
    }
    TEST_ASSERT_TRUE(readingFirstItems > 0);
    TEST_ASSERT_TRUE(readingFirstItems < 12);
}

void run_queue_tests() {
    RUN_TEST(test_first_part_nvs_values);
    RUN_TEST(test_queue_defaults_are_random_and_back_to_back_off);
    RUN_TEST(test_random_first_part_draws_both_orders);
    RUN_TEST(test_random_first_part_is_fixed_by_seed);
    RUN_TEST(test_random_each_item_asks_both_parts_once);
    RUN_TEST(test_random_back_to_back_on_keeps_parts_adjacent);
    RUN_TEST(test_random_back_to_back_off_interleaves);
    RUN_TEST(test_random_mistake_asks_failed_part_again);
    RUN_TEST(test_random_radical_asks_meaning_only);
    RUN_TEST(test_practice_is_random_and_back_to_back_regardless_of_settings);
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

// REV-01/REV-03: accepted-answer display tokens and the first-sentence hint.
#include <unity.h>

#include <algorithm>
#include <string>
#include <vector>

#include "wk/AnswerDisplay.h"
#include "wk/ReviewQueue.h"
#include "wk/Text.h"
#include "wk/Types.h"

using namespace kk;

namespace {

Subject answerSubject() {
    Subject s;
    s.id = 301;
    s.type = SubjectType::Kanji;
    s.level = 3;
    s.characters = "作";
    s.meanings = {
        {"Composition", true, true},
        {"Writing", false, true},
        {"Essay", false, true},
        {"Novel", false, false},
    };
    s.readings = {
        {"じょう", true, true, ReadingType::Onyomi},
        {"せい", false, true, ReadingType::Onyomi},
        {"うえ", false, true, ReadingType::Kunyomi},
        {"した", false, false, ReadingType::Kunyomi},
    };
    s.meaningSynonyms = {"essay", "text"};
    return s;
}

Subject unknownReadingSubject() {
    Subject s;
    s.id = 302;
    s.type = SubjectType::Kanji;
    s.level = 3;
    s.characters = "人";
    s.meanings = {{"person", true, true}};
    s.readings = {
        {"ひと", true, true},
        {"にん", false, true},
        {"じん", false, false},
    };
    return s;
}

void assertTokens(const std::vector<std::string>& want, const std::vector<std::string>& got) {
    TEST_ASSERT_EQUAL_UINT(static_cast<unsigned>(want.size()),
                           static_cast<unsigned>(got.size()));
    for (size_t i = 0; i < want.size(); ++i)
        TEST_ASSERT_EQUAL_STRING(want[i].c_str(), got[i].c_str());
}

bool hasToken(const std::vector<std::string>& tokens, const char* text) {
    for (const std::string& token : tokens)
        if (token == text) return true;
    return false;
}

// ------------------------------------------------------------- REV-01

static void test_accepted_meanings_all_in_order() {
    const Subject s = answerSubject();
    assertTokens({"Composition", "Writing", "Essay"}, acceptedMeaningTokens(s));
}

static void test_accepted_meanings_skip_unaccepted() {
    const Subject s = answerSubject();
    const std::vector<std::string> tokens = acceptedMeaningTokens(s);
    TEST_ASSERT_FALSE(hasToken(tokens, "Novel"));
}

static void test_accepted_meanings_do_not_include_synonyms() {
    const Subject s = answerSubject();
    const std::vector<std::string> tokens = acceptedMeaningTokens(s);
    TEST_ASSERT_FALSE(hasToken(tokens, "essay"));
    TEST_ASSERT_FALSE(hasToken(tokens, "text"));
}

static void test_accepted_readings_grouped_kanji() {
    const Subject s = answerSubject();
    // On'yomi first (WaniKani order), a group break, then kun'yomi.
    assertTokens({"じょう", "せい", "", "うえ"}, acceptedReadingTokens(s));
}

static void test_accepted_readings_unknown_type_flat() {
    const Subject s = unknownReadingSubject();
    assertTokens({"ひと", "にん"}, acceptedReadingTokens(s));
}

static void test_accepted_readings_skip_unaccepted() {
    const Subject s = answerSubject();
    TEST_ASSERT_FALSE(hasToken(acceptedReadingTokens(s), "した"));
}

static void test_accepted_readings_strip_okurigana_marks() {
    Subject s;
    s.type = SubjectType::Vocabulary;
    s.readings = {{"た.べる", true, true, ReadingType::Kunyomi}};
    assertTokens({"たべる"}, acceptedReadingTokens(s));
}

// ------------------------------------------------------------- REV-03

static void test_first_sentence_period() {
    TEST_ASSERT_EQUAL_STRING("First one.", firstSentence("First one. Second one.").c_str());
}

static void test_first_sentence_question_and_exclamation() {
    TEST_ASSERT_EQUAL_STRING("Really?", firstSentence("Really? Yes! No.").c_str());
    TEST_ASSERT_EQUAL_STRING("Yes!", firstSentence("Yes! No.").c_str());
}

static void test_first_sentence_japanese_stop() {
    TEST_ASSERT_EQUAL_STRING("これはヒント。", firstSentence("これはヒント。次の文。").c_str());
    TEST_ASSERT_EQUAL_STRING("本当？", firstSentence("本当？はい！").c_str());
}

static void test_first_sentence_without_stop() {
    TEST_ASSERT_EQUAL_STRING("No stop here", firstSentence("No stop here").c_str());
}

static void test_first_sentence_trims_leading_space() {
    TEST_ASSERT_EQUAL_STRING("First.", firstSentence("   First. Second.").c_str());
}

static void test_first_sentence_empty() {
    TEST_ASSERT_EQUAL_STRING("", firstSentence("").c_str());
    TEST_ASSERT_EQUAL_STRING("", firstSentence("   ").c_str());
}

// ------------------------------------------------------------- REV-06

static void test_auto_advance_due() {
    TEST_ASSERT_FALSE(autoAdvanceDue(0, 10000));  // Off never advances
    TEST_ASSERT_FALSE(autoAdvanceDue(500, 0));
    TEST_ASSERT_FALSE(autoAdvanceDue(500, 499));
    TEST_ASSERT_TRUE(autoAdvanceDue(500, 500));
    TEST_ASSERT_TRUE(autoAdvanceDue(500, 501));
    TEST_ASSERT_TRUE(autoAdvanceDue(2000, 2000));
}

// ------------------------------------------------------------- REV-04

Subject queueKanji(uint32_t id) {
    Subject s;
    s.id = id;
    s.type = SubjectType::Kanji;
    s.level = 4;
    s.meanings = {{"eat", true, true}};
    s.readings = {{"たべ", true, true, ReadingType::Kunyomi}};
    return s;
}

Subject queueRadical(uint32_t id) {
    Subject s;
    s.id = id;
    s.type = SubjectType::Radical;
    s.level = 4;
    s.meanings = {{"ground", true, true}};
    return s;
}

Assignment queueAssignment(uint32_t id, const Subject& subject) {
    Assignment a;
    a.id = id;
    a.subjectId = subject.id;
    a.type = subject.type;
    a.srsStage = 3;
    a.availableAt = 100;
    return a;
}

void buildFiveQueue(ReviewQueue* q, std::vector<Subject>* subjects,
                    std::vector<Assignment>* assignments, bool radicals) {
    for (uint32_t i = 1; i <= 5; ++i) {
        subjects->push_back(radicals ? queueRadical(i) : queueKanji(i));
        assignments->push_back(queueAssignment(10 + i, subjects->back()));
    }
    q->build(*assignments, *subjects, 1000, 0);
}

static void test_undo_after_other_part() {
    const Subject s = queueKanji(1);
    ReviewQueue q;
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
    q.build({queueAssignment(10, s)}, {s}, 1000);

    const uint32_t failed = q.current()->subjectId;
    const AnswerOutcome o = q.answer(AnswerStatus::Incorrect);
    TEST_ASSERT_TRUE(o.counted);
    TEST_ASSERT_EQUAL(ReviewPart::Reading, q.part());
    TEST_ASSERT_EQUAL(1, q.current()->incorrectMeaning);
    TEST_ASSERT_TRUE(q.current()->meaningAttempted);

    TEST_ASSERT_TRUE(q.undoLastIncorrect());
    TEST_ASSERT_EQUAL(failed, q.current()->subjectId);
    TEST_ASSERT_EQUAL(ReviewPart::Meaning, q.part());
    TEST_ASSERT_EQUAL(0, q.current()->incorrectMeaning);
    TEST_ASSERT_FALSE(q.current()->meaningAttempted);
    TEST_ASSERT_EQUAL(1, q.remaining());
    TEST_ASSERT_EQUAL(0, q.completedCount());
}

static void test_undo_after_requeue() {
    std::vector<Subject> subjects;
    std::vector<Assignment> assignments;
    ReviewQueue q;
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
    buildFiveQueue(&q, &subjects, &assignments, true);
    const uint32_t failed = q.current()->subjectId;

    const AnswerOutcome o = q.answer(AnswerStatus::Incorrect);
    TEST_ASSERT_TRUE(o.requeued);
    TEST_ASSERT_EQUAL(5, q.remaining());
    TEST_ASSERT_NOT_EQUAL(failed, q.current()->subjectId);
    TEST_ASSERT_EQUAL(failed, q.queue()[ReviewQueue::kRequeueGap].subjectId);

    TEST_ASSERT_TRUE(q.undoLastIncorrect());
    TEST_ASSERT_EQUAL(failed, q.current()->subjectId);
    TEST_ASSERT_EQUAL(ReviewPart::Meaning, q.part());
    TEST_ASSERT_EQUAL(0, q.current()->incorrectMeaning);
    TEST_ASSERT_FALSE(q.current()->meaningAttempted);
    TEST_ASSERT_EQUAL(5, q.remaining());
}

static void test_undo_without_incorrect_is_noop() {
    const Subject s = queueKanji(1);
    ReviewQueue q;
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
    q.build({queueAssignment(10, s)}, {s}, 1000);

    TEST_ASSERT_FALSE(q.undoLastIncorrect());
    TEST_ASSERT_EQUAL(ReviewPart::Meaning, q.part());
    TEST_ASSERT_EQUAL(1, q.remaining());

    q.answer(AnswerStatus::Incorrect);
    TEST_ASSERT_TRUE(q.undoLastIncorrect());
    TEST_ASSERT_FALSE(q.undoLastIncorrect());
    TEST_ASSERT_EQUAL(ReviewPart::Meaning, q.part());
}

static void test_accept_marks_part_correct() {
    const Subject s = queueKanji(1);
    ReviewQueue q;
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
    q.build({queueAssignment(10, s)}, {s}, 1000);

    q.answer(AnswerStatus::Incorrect);  // meaning, part moves to reading
    TEST_ASSERT_TRUE(q.undoLastIncorrect());
    q.setWrongAnswerOverride('C');
    const AnswerOutcome o = q.answer(AnswerStatus::Correct);

    TEST_ASSERT_FALSE(o.completed);
    TEST_ASSERT_EQUAL(1, q.remaining());
    TEST_ASSERT_EQUAL(0, q.completedCount());
    TEST_ASSERT_EQUAL(ReviewPart::Reading, q.part());
    TEST_ASSERT_TRUE(q.current()->meaningDone);
    TEST_ASSERT_TRUE(q.current()->meaningAttempted);
    TEST_ASSERT_EQUAL(0, q.current()->incorrectMeaning);
    TEST_ASSERT_EQUAL_CHAR('C', q.current()->wrongAnswerOverride);
}

static void test_accept_completes_when_other_part_done() {
    const Subject s = queueKanji(1);
    ReviewQueue q;
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
    q.setFirstPart(FirstPart::Reading);
    q.build({queueAssignment(10, s)}, {s}, 1000);

    AnswerOutcome o = q.answer(AnswerStatus::Correct);  // reading done
    TEST_ASSERT_EQUAL(ReviewPart::Meaning, q.part());
    o = q.answer(AnswerStatus::Incorrect);  // failed meaning requeues the item
    TEST_ASSERT_TRUE(o.requeued);
    TEST_ASSERT_EQUAL(1, q.remaining());
    TEST_ASSERT_TRUE(q.current()->readingDone);
    TEST_ASSERT_EQUAL(1, q.current()->incorrectMeaning);

    TEST_ASSERT_TRUE(q.undoLastIncorrect());
    q.setWrongAnswerOverride('C');
    o = q.answer(AnswerStatus::Correct);

    TEST_ASSERT_TRUE(o.completed);
    TEST_ASSERT_TRUE(q.empty());
    TEST_ASSERT_EQUAL(1, q.completedCount());
    const SessionItem& done = q.completed().front();
    TEST_ASSERT_TRUE(done.meaningDone);
    TEST_ASSERT_TRUE(done.readingDone);
    TEST_ASSERT_EQUAL(0, done.incorrectMeaning);
    TEST_ASSERT_EQUAL_CHAR('C', done.wrongAnswerOverride);
}

static void test_ask_again_requeues_unanswered() {
    std::vector<Subject> subjects;
    std::vector<Assignment> assignments;
    ReviewQueue q;
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
    buildFiveQueue(&q, &subjects, &assignments, false);
    const uint32_t failed = q.current()->subjectId;

    q.answer(AnswerStatus::Incorrect);  // meaning, part moves to reading
    TEST_ASSERT_TRUE(q.undoLastIncorrect());
    q.setWrongAnswerOverride('A');
    q.askAgain();

    TEST_ASSERT_EQUAL(5, q.remaining());
    TEST_ASSERT_NOT_EQUAL(failed, q.current()->subjectId);
    TEST_ASSERT_EQUAL(failed, q.queue()[ReviewQueue::kRequeueGap].subjectId);
    const SessionItem& later = q.queue()[ReviewQueue::kRequeueGap];
    TEST_ASSERT_EQUAL(0, later.incorrectMeaning);
    TEST_ASSERT_EQUAL(0, later.incorrectReading);
    TEST_ASSERT_FALSE(later.meaningDone);
    TEST_ASSERT_FALSE(later.readingDone);
    TEST_ASSERT_FALSE(later.meaningAttempted);
    TEST_ASSERT_FALSE(later.readingAttempted);
    TEST_ASSERT_EQUAL_CHAR('A', later.wrongAnswerOverride);
}

static void test_ask_again_keeps_other_part_done() {
    const Subject s = queueKanji(1);
    ReviewQueue q;
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
    q.build({queueAssignment(10, s)}, {s}, 1000);

    q.answer(AnswerStatus::Correct);  // meaning done
    q.answer(AnswerStatus::Incorrect);  // reading fails
    TEST_ASSERT_TRUE(q.undoLastIncorrect());
    q.setWrongAnswerOverride('A');
    q.askAgain();

    TEST_ASSERT_EQUAL(1, q.remaining());
    TEST_ASSERT_EQUAL(ReviewPart::Reading, q.part());
    TEST_ASSERT_TRUE(q.current()->meaningDone);
    TEST_ASSERT_FALSE(q.current()->readingDone);
    TEST_ASSERT_EQUAL(0, q.current()->incorrectReading);
    TEST_ASSERT_FALSE(q.current()->readingAttempted);
    TEST_ASSERT_EQUAL_CHAR('A', q.current()->wrongAnswerOverride);
}

// ------------------------------------------------------------- REV-08

Assignment orderAssignment(uint32_t subjectId, uint8_t stage, uint32_t available,
                           SubjectType type = SubjectType::Kanji) {
    Assignment a;
    a.id = 1000 + subjectId;
    a.subjectId = subjectId;
    a.type = type;
    a.srsStage = stage;
    a.availableAt = available;
    return a;
}

Subject orderSubject(uint32_t id, uint8_t level, SubjectType type = SubjectType::Kanji) {
    Subject s;
    s.id = id;
    s.type = type;
    s.level = level;
    s.meanings = {{"x", true, true}};
    if (type != SubjectType::Radical) s.readings = {{"x", true, true, ReadingType::Kunyomi}};
    return s;
}

void assertOrder(const std::vector<uint32_t>& want, const std::vector<uint32_t>& got) {
    TEST_ASSERT_EQUAL_UINT(static_cast<unsigned>(want.size()),
                           static_cast<unsigned>(got.size()));
    for (size_t i = 0; i < want.size(); ++i) TEST_ASSERT_EQUAL_UINT32(want[i], got[i]);
}

std::vector<uint32_t> queueIds(const ReviewQueue& q) {
    std::vector<uint32_t> ids;
    for (const SessionItem& item : q.queue()) ids.push_back(item.subjectId);
    return ids;
}

static void test_order_random_is_a_permutation() {
    std::vector<Assignment> assignments;
    std::vector<uint32_t> input;
    for (uint32_t id = 1; id <= 8; ++id) {
        assignments.push_back(orderAssignment(id, 3, 100 + id));
        input.push_back(id);
    }
    const std::vector<uint32_t> got = selectDueSubjectIds(
        assignments, {}, ReviewOrderStrategy::Random, 0, false, 1000, 0, 777);
    TEST_ASSERT_EQUAL_UINT(8, got.size());
    std::vector<uint32_t> sorted = got;
    std::sort(sorted.begin(), sorted.end());
    for (uint32_t id = 1; id <= 8; ++id) TEST_ASSERT_EQUAL_UINT32(id, sorted[id - 1]);
    TEST_ASSERT_TRUE(got != input);

    // build() with the same strategy is also a permutation of the due set.
    std::vector<Subject> subjects;
    for (uint32_t id = 1; id <= 8; ++id) subjects.push_back(orderSubject(id, 3));
    ReviewQueue q;
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
    q.setOrderStrategy(ReviewOrderStrategy::Random);
    q.build(assignments, subjects, 1000, 0, 777);
    TEST_ASSERT_EQUAL(8, q.remaining());
    std::vector<uint32_t> built = queueIds(q);
    std::sort(built.begin(), built.end());
    for (uint32_t id = 1; id <= 8; ++id) TEST_ASSERT_EQUAL_UINT32(id, built[id - 1]);
}

static void test_order_lowest_level_first() {
    const std::vector<Assignment> assignments = {
        orderAssignment(1, 1, 900), orderAssignment(2, 8, 800),
        orderAssignment(3, 1, 700), orderAssignment(4, 1, 600)};
    const std::vector<ReviewLevel> levels = {{1, 5}, {2, 1}, {3, 3}, {4, 1}};
    const std::vector<uint32_t> got = selectDueSubjectIds(
        assignments, levels, ReviewOrderStrategy::LowestLevelFirst, 0, false, 1000, 0, 1);
    assertOrder({2, 4, 3, 1}, got);

    // build() re-applies the same comparator even when the input is reversed.
    std::vector<Subject> subjects;
    for (uint32_t id = 1; id <= 4; ++id) subjects.push_back(orderSubject(id, levels[id - 1].level));
    std::vector<Assignment> reversed(assignments.rbegin(), assignments.rend());
    ReviewQueue q;
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
    q.setOrderStrategy(ReviewOrderStrategy::LowestLevelFirst);
    q.build(reversed, subjects, 1000, 0, 1);
    assertOrder(got, queueIds(q));
}

static void test_order_current_level_first() {
    const std::vector<Assignment> assignments = {
        orderAssignment(1, 3, 100), orderAssignment(2, 3, 100), orderAssignment(3, 3, 100),
        orderAssignment(4, 3, 100), orderAssignment(5, 3, 100)};
    const std::vector<ReviewLevel> levels = {{1, 1}, {2, 4}, {3, 6}, {4, 2}, {5, 9}};
    const std::vector<uint32_t> got = selectDueSubjectIds(
        assignments, levels, ReviewOrderStrategy::CurrentLevelFirst, 4, false, 1000, 0, 1);
    assertOrder({2, 1, 4, 3, 5}, got);
}

static void test_order_srs_ascending() {
    const std::vector<Assignment> assignments = {
        orderAssignment(1, 5, 100), orderAssignment(2, 1, 200),
        orderAssignment(3, 8, 300), orderAssignment(4, 3, 400)};
    const std::vector<uint32_t> got = selectDueSubjectIds(
        assignments, {}, ReviewOrderStrategy::SrsAscending, 0, false, 1000, 0, 1);
    assertOrder({2, 4, 1, 3}, got);

    std::vector<Subject> subjects;
    for (uint32_t id = 1; id <= 4; ++id) subjects.push_back(orderSubject(id, 3));
    std::vector<Assignment> reversed(assignments.rbegin(), assignments.rend());
    ReviewQueue q;
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
    q.setOrderStrategy(ReviewOrderStrategy::SrsAscending);
    q.build(reversed, subjects, 1000, 0, 1);
    assertOrder(got, queueIds(q));
}

static void test_order_srs_descending() {
    const std::vector<Assignment> assignments = {
        orderAssignment(1, 5, 100), orderAssignment(2, 1, 200),
        orderAssignment(3, 8, 300), orderAssignment(4, 3, 400)};
    const std::vector<uint32_t> got = selectDueSubjectIds(
        assignments, {}, ReviewOrderStrategy::SrsDescending, 0, false, 1000, 0, 1);
    assertOrder({3, 1, 4, 2}, got);
}

static void test_order_oldest_available_first() {
    const std::vector<Assignment> assignments = {
        orderAssignment(1, 1, 300), orderAssignment(2, 8, 100),
        orderAssignment(3, 3, 500), orderAssignment(4, 5, 200)};
    const std::vector<uint32_t> got = selectDueSubjectIds(
        assignments, {}, ReviewOrderStrategy::OldestAvailableFirst, 0, false, 1000, 0, 1);
    assertOrder({2, 4, 1, 3}, got);
}

static void test_order_type_radical_kanji_vocab() {
    std::vector<Assignment> assignments;
    for (uint32_t id = 1; id <= 8; ++id) {
        SubjectType type = SubjectType::Kanji;
        if (id <= 2) type = SubjectType::Radical;
        if (id >= 7) type = SubjectType::KanaVocabulary;
        else if (id >= 5) type = SubjectType::Vocabulary;
        assignments.push_back(orderAssignment(id, 3, 100, type));
    }
    const std::vector<uint32_t> base = selectDueSubjectIds(
        assignments, {}, ReviewOrderStrategy::Random, 0, false, 1000, 0, 42);
    const std::vector<uint32_t> typed = selectDueSubjectIds(
        assignments, {}, ReviewOrderStrategy::Random, 0, true, 1000, 0, 42);
    TEST_ASSERT_EQUAL_UINT(8, typed.size());

    const auto rank = [](uint32_t id) -> int {
        if (id <= 2) return 0;
        if (id <= 4) return 1;
        return 2;
    };
    for (size_t i = 1; i < typed.size(); ++i)
        TEST_ASSERT_TRUE(rank(typed[i - 1]) <= rank(typed[i]));

    // Stable inside each group: the typed order is the base order filtered.
    std::vector<uint32_t> expected;
    for (int r = 0; r <= 2; ++r)
        for (uint32_t id : base)
            if (rank(id) == r) expected.push_back(id);
    assertOrder(expected, typed);

    // build() groups the same way after its shuffle.
    std::vector<Subject> subjects;
    for (uint32_t id = 1; id <= 8; ++id) {
        SubjectType type = SubjectType::Kanji;
        if (id <= 2) type = SubjectType::Radical;
        if (id >= 7) type = SubjectType::KanaVocabulary;
        else if (id >= 5) type = SubjectType::Vocabulary;
        subjects.push_back(orderSubject(id, 3, type));
    }
    ReviewQueue q;
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
    q.setOrderStrategy(ReviewOrderStrategy::Random);
    q.setTypeOrder(true);
    q.build(assignments, subjects, 1000, 0, 42);
    const std::vector<uint32_t> built = queueIds(q);
    for (size_t i = 0; i < built.size(); ++i) {
        if (i > 0) TEST_ASSERT_TRUE(rank(built[i - 1]) <= rank(built[i]));
    }
}

static void test_order_limit_keeps_first_n() {
    std::vector<Assignment> assignments;
    std::vector<ReviewLevel> levels;
    std::vector<Subject> subjects;
    for (uint32_t id = 1; id <= 6; ++id) {
        assignments.push_back(orderAssignment(id, 3, 100));
        const uint8_t level = static_cast<uint8_t>(7 - id);
        levels.push_back({id, level});
        subjects.push_back(orderSubject(id, level));
    }
    const std::vector<uint32_t> all = selectDueSubjectIds(
        assignments, levels, ReviewOrderStrategy::LowestLevelFirst, 0, false, 1000, 0, 1);
    const std::vector<uint32_t> first = selectDueSubjectIds(
        assignments, levels, ReviewOrderStrategy::LowestLevelFirst, 0, false, 1000, 3, 1);
    TEST_ASSERT_EQUAL_UINT(3, first.size());
    for (size_t i = 0; i < first.size(); ++i) TEST_ASSERT_EQUAL_UINT32(all[i], first[i]);

    ReviewQueue q;
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
    q.setOrderStrategy(ReviewOrderStrategy::LowestLevelFirst);
    q.build(assignments, subjects, 1000, 3, 1);
    assertOrder(first, queueIds(q));
}

static void test_order_account_lower_levels_first() {
    std::vector<Subject> subjects = {
        orderSubject(1, 3), orderSubject(2, 1), orderSubject(3, 5),
        orderSubject(4, 2), orderSubject(5, 4)};
    std::vector<Assignment> assignments;
    for (uint32_t id = 5; id >= 1; --id) assignments.push_back(orderAssignment(id, 3, 100));

    ReviewQueue q;
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
    q.setOrderStrategy(ReviewOrderStrategy::Account);
    q.setLowerLevelsFirst(true);
    q.build(assignments, subjects, 1000, 0, 99);
    TEST_ASSERT_EQUAL(5, q.remaining());
    uint8_t previous = 0;
    for (const SessionItem& item : q.queue()) {
        TEST_ASSERT_TRUE(item.level >= previous);
        previous = item.level;
    }

    // Account with shuffled presentation is a permutation of the same set.
    ReviewQueue shuffled;
    shuffled.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    shuffled.setBackToBack(true);  // pinned: written for back-to-back on
    shuffled.setOrderStrategy(ReviewOrderStrategy::Account);
    shuffled.setLowerLevelsFirst(false);
    shuffled.build(assignments, subjects, 1000, 0, 99);
    std::vector<uint32_t> ids = queueIds(shuffled);
    std::sort(ids.begin(), ids.end());
    TEST_ASSERT_EQUAL_UINT32(1, ids[0]);
    TEST_ASSERT_EQUAL_UINT32(5, ids[4]);
}

static void test_order_unknown_level_last() {
    const std::vector<Assignment> assignments = {
        orderAssignment(1, 3, 100), orderAssignment(2, 3, 100),
        orderAssignment(3, 3, 100), orderAssignment(4, 3, 100),
        orderAssignment(5, 3, 100)};
    // Subjects 3 and 5 are missing from the catalog.
    const std::vector<ReviewLevel> levels = {{1, 5}, {2, 1}, {4, 3}};
    assertOrder({2, 4, 1, 3, 5},
                selectDueSubjectIds(assignments, levels,
                                    ReviewOrderStrategy::LowestLevelFirst, 0, false, 1000, 0, 1));
    assertOrder({4, 2, 1, 3, 5},
                selectDueSubjectIds(assignments, levels,
                                    ReviewOrderStrategy::CurrentLevelFirst, 3, false, 1000, 0, 1));
}

static void test_session_item_copies_available_at() {
    const Subject s = orderSubject(1, 3);
    ReviewQueue q;
    q.setFirstPart(FirstPart::Meaning);  // pinned: written for meaning first
    q.setBackToBack(true);  // pinned: written for back-to-back on
    q.build({orderAssignment(1, 3, 4242)}, {s}, 10000);
    TEST_ASSERT_EQUAL(1, q.remaining());
    TEST_ASSERT_EQUAL_UINT32(4242, q.current()->availableAt);
}

}  // namespace

void run_rev_tests() {
    RUN_TEST(test_accepted_meanings_all_in_order);
    RUN_TEST(test_accepted_meanings_skip_unaccepted);
    RUN_TEST(test_accepted_meanings_do_not_include_synonyms);
    RUN_TEST(test_accepted_readings_grouped_kanji);
    RUN_TEST(test_accepted_readings_unknown_type_flat);
    RUN_TEST(test_accepted_readings_skip_unaccepted);
    RUN_TEST(test_accepted_readings_strip_okurigana_marks);
    RUN_TEST(test_first_sentence_period);
    RUN_TEST(test_first_sentence_question_and_exclamation);
    RUN_TEST(test_first_sentence_japanese_stop);
    RUN_TEST(test_first_sentence_without_stop);
    RUN_TEST(test_first_sentence_trims_leading_space);
    RUN_TEST(test_first_sentence_empty);
    RUN_TEST(test_auto_advance_due);
    RUN_TEST(test_undo_after_other_part);
    RUN_TEST(test_undo_after_requeue);
    RUN_TEST(test_undo_without_incorrect_is_noop);
    RUN_TEST(test_accept_marks_part_correct);
    RUN_TEST(test_accept_completes_when_other_part_done);
    RUN_TEST(test_ask_again_requeues_unanswered);
    RUN_TEST(test_ask_again_keeps_other_part_done);
    RUN_TEST(test_order_random_is_a_permutation);
    RUN_TEST(test_order_lowest_level_first);
    RUN_TEST(test_order_current_level_first);
    RUN_TEST(test_order_srs_ascending);
    RUN_TEST(test_order_srs_descending);
    RUN_TEST(test_order_oldest_available_first);
    RUN_TEST(test_order_type_radical_kanji_vocab);
    RUN_TEST(test_order_limit_keeps_first_n);
    RUN_TEST(test_order_account_lower_levels_first);
    RUN_TEST(test_order_unknown_level_last);
    RUN_TEST(test_session_item_copies_available_at);
}

// REV-01/REV-03: accepted-answer display tokens and the first-sentence hint.
#include <unity.h>

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
    q.setMeaningFirst(false);
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
}

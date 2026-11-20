#include <unity.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "ui/KeyInput.h"
#include "ota/Manifest.h"
#include "wk/AnswerChecker.h"
#include "wk/Assignment.h"
#include "wk/AudioIndex.h"
#include "wk/Cache.h"
#include "wk/Catalog.h"
#include "wk/CriticalItems.h"
#include "wk/Crc32.h"
#include "wk/HelpTable.h"
#include "wk/HttpRetry.h"
#include "wk/Outbox.h"
#include "wk/Practice.h"
#include "wk/Journal.h"
#include "wk/Json.h"
#include "wk/JsonStream.h"
#include "wk/LessonOrder.h"
#include "wk/LessonQuiz.h"
#include "wk/LevelProgress.h"
#include "wk/RateLimit.h"
#include "wk/StudyMaterial.h"
#include "wk/SubjectParser.h"
#include "wk/Text.h"
#include "wk/Summary.h"
#include "wk/SummaryParser.h"
#include "wk/Time.h"
#include "wk/User.h"
#include "wk/ReviewQueue.h"
#include "wk/ReviewResponse.h"
#include "wk/ReviewStatistics.h"
#include "wk/RomajiIme.h"
#include "wk/SessionStats.h"
#include "wk/SettingsModel.h"
#include "wk/SrsCounts.h"
#include "wk/Stats.h"
#include "wk/SubjectCodec.h"
#include "wk/SubmitPolicy.h"
#include "wk/SyncPlan.h"
#include "wk/SyncSteps.h"
#include "wk/Text.h"
#include "wk/WifiSetup.h"

using namespace kk;
using namespace kk::ota;

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------- romaji
static void test_romaji_common_words() {
    TEST_ASSERT_EQUAL_STRING("にほん", romajiToHiragana("nihon").c_str());
    TEST_ASSERT_EQUAL_STRING("こんにちは", romajiToHiragana("konnichiha").c_str());
    TEST_ASSERT_EQUAL_STRING("がっこう", romajiToHiragana("gakkou").c_str());
    TEST_ASSERT_EQUAL_STRING("きって", romajiToHiragana("kitte").c_str());
    TEST_ASSERT_EQUAL_STRING("りょこう", romajiToHiragana("ryokou").c_str());
    TEST_ASSERT_EQUAL_STRING("しんじゅく", romajiToHiragana("shinjuku").c_str());
    TEST_ASSERT_EQUAL_STRING("せんぱい", romajiToHiragana("senpai").c_str());
    TEST_ASSERT_EQUAL_STRING("かんじ", romajiToHiragana("kanji").c_str());
    TEST_ASSERT_EQUAL_STRING("とうきょう", romajiToHiragana("toukyou").c_str());
    TEST_ASSERT_EQUAL_STRING("おおさか", romajiToHiragana("oosaka").c_str());
    TEST_ASSERT_EQUAL_STRING("まっちゃ", romajiToHiragana("matcha").c_str());
}

static void test_romaji_n_and_small_tsu() {
    TEST_ASSERT_EQUAL_STRING("ん", romajiToHiragana("n").c_str());
    TEST_ASSERT_EQUAL_STRING("ん", romajiToHiragana("nn").c_str());
    TEST_ASSERT_EQUAL_STRING("んな", romajiToHiragana("nna").c_str());
    TEST_ASSERT_EQUAL_STRING("んか", romajiToHiragana("nka").c_str());
    TEST_ASSERT_EQUAL_STRING("きんようび", romajiToHiragana("kin'youbi").c_str());
    TEST_ASSERT_EQUAL_STRING("しんよう", romajiToHiragana("shin'you").c_str());
    TEST_ASSERT_EQUAL_STRING("にゃん", romajiToHiragana("nyan").c_str());
    TEST_ASSERT_EQUAL_STRING("こんにちは", romajiToHiragana("konnichiha").c_str());
}

static void test_romaji_ime_editing() {
    RomajiIme ime;
    ime.feed('t');
    ime.feed('a');
    ime.feed('b');
    ime.feed('e');
    TEST_ASSERT_EQUAL_STRING("たべ", ime.text().c_str());
    TEST_ASSERT_EQUAL_STRING("tabe", ime.raw().c_str());
    ime.backspace();
    TEST_ASSERT_EQUAL_STRING("た", ime.text().c_str());
    TEST_ASSERT_EQUAL_STRING("ta", ime.raw().c_str());
    ime.clear();
    TEST_ASSERT_TRUE(ime.empty());
}

// ---------------------------------------------------------------- answers
static Subject kanji() {
    Subject s;
    s.id = 1;
    s.type = SubjectType::Kanji;
    s.level = 5;
    s.characters = "食";
    s.meanings = {{"eat", true, true}, {"food", false, true}};
    s.readings = {
        {"しょく", true, true, ReadingType::Onyomi},
        {"た.べる", false, true, ReadingType::Kunyomi},
    };
    s.meaningSynonyms = {"consume"};
    s.whitelist = {"devour"};
    s.blacklist = {"drink"};
    return s;
}

static Subject vocabulary() {
    Subject s;
    s.id = 2;
    s.type = SubjectType::Vocabulary;
    s.level = 5;
    s.characters = "大人";
    s.meanings = {{"adult", true, true}};
    s.readings = {{"おとな", true, true, ReadingType::Kunyomi}};
    return s;
}

static Subject radical() {
    Subject s;
    s.id = 3;
    s.type = SubjectType::Radical;
    s.level = 1;
    s.meanings = {{"ground", true, true}};
    s.blacklist = {"floor"};
    return s;
}

static void test_meaning_accepts_meanings_synonyms_and_whitelist() {
    const Subject s = kanji();
    TEST_ASSERT_EQUAL(AnswerStatus::Correct, checkAnswer(s, AnswerKind::Meaning, "eat").status);
    TEST_ASSERT_EQUAL(AnswerStatus::Correct, checkAnswer(s, AnswerKind::Meaning, "EAT.").status);
    TEST_ASSERT_EQUAL(AnswerStatus::Correct, checkAnswer(s, AnswerKind::Meaning, "Food").status);
    TEST_ASSERT_EQUAL(AnswerStatus::Correct,
                      checkAnswer(s, AnswerKind::Meaning, "consume").status);
    TEST_ASSERT_EQUAL(AnswerStatus::Correct, checkAnswer(s, AnswerKind::Meaning, "devour").status);
}

static void test_meaning_blacklist_wins() {
    const Subject s = kanji();
    TEST_ASSERT_EQUAL(AnswerStatus::Incorrect, checkAnswer(s, AnswerKind::Meaning, "drink").status);
    const Subject r = radical();
    TEST_ASSERT_EQUAL(AnswerStatus::Incorrect, checkAnswer(r, AnswerKind::Meaning, "floor").status);
}

static void test_meaning_typo_is_a_warning_in_strict_mode() {
    const Subject s = kanji();
    const AnswerCheck strict = checkAnswer(s, AnswerKind::Meaning, "consum");
    TEST_ASSERT_EQUAL(AnswerStatus::Warning, strict.status);
    TEST_ASSERT_TRUE(strict.close);
    TEST_ASSERT_EQUAL(AnswerStatus::Correct,
                      checkAnswer(s, AnswerKind::Meaning, "consum", false).status);
}

static void test_meaning_and_reading_confusion() {
    const Subject s = kanji();
    TEST_ASSERT_EQUAL(AnswerStatus::Warning,
                      checkAnswer(s, AnswerKind::Meaning, "taberu").status);
    TEST_ASSERT_EQUAL(AnswerStatus::Warning,
                      checkAnswer(s, AnswerKind::Reading, "eat").status);
    TEST_ASSERT_EQUAL(AnswerStatus::Warning,
                      checkAnswer(s, AnswerKind::Reading, "食").status);
}

static void test_reading_accepts_romaji_kana_and_katakana() {
    const Subject s = kanji();
    TEST_ASSERT_EQUAL(AnswerStatus::Correct,
                      checkAnswer(s, AnswerKind::Reading, "shoku").status);
    TEST_ASSERT_EQUAL(AnswerStatus::Correct,
                      checkAnswer(s, AnswerKind::Reading, "ショク").status);
    TEST_ASSERT_EQUAL(AnswerStatus::Correct,
                      checkAnswer(s, AnswerKind::Reading, "taberu", false).status);
    TEST_ASSERT_EQUAL(AnswerStatus::Correct,
                      checkAnswer(s, AnswerKind::Reading, "たべる", false).status);
}

static void test_reading_primary_and_valid_both_correct() {
    const Subject s = kanji();
    const AnswerCheck strict = checkAnswer(s, AnswerKind::Reading, "taberu");
    TEST_ASSERT_EQUAL(AnswerStatus::Correct, strict.status);
    TEST_ASSERT_EQUAL(AnswerStatus::Correct,
                      checkAnswer(s, AnswerKind::Reading, "taberu", false).status);
}

static void test_reading_okurigana_and_n_conversion() {
    const Subject s = kanji();
    const AnswerCheck stem = checkAnswer(s, AnswerKind::Reading, "ta");
    TEST_ASSERT_EQUAL(AnswerStatus::Warning, stem.status);
    TEST_ASSERT_TRUE(stem.message.find("okurigana") != std::string::npos);

    Subject k;
    k.type = SubjectType::Kanji;
    k.characters = "今";
    k.meanings = {{"now", true, true}};
    k.readings = {{"こんや", true, true, ReadingType::Kunyomi}};
    const AnswerCheck n = checkAnswer(k, AnswerKind::Reading, "konya");
    TEST_ASSERT_EQUAL(AnswerStatus::Warning, n.status);
    TEST_ASSERT_TRUE(n.message.find("n'") != std::string::npos);
}

static void test_vocabulary_and_radical_rules() {
    const Subject v = vocabulary();
    TEST_ASSERT_EQUAL(AnswerStatus::Correct,
                      checkAnswer(v, AnswerKind::Reading, "otona").status);
    TEST_ASSERT_EQUAL(AnswerStatus::Correct,
                      checkAnswer(v, AnswerKind::Reading, "おとな").status);
    TEST_ASSERT_EQUAL(AnswerStatus::Correct,
                      checkAnswer(v, AnswerKind::Meaning, "adult").status);

    const Subject r = radical();
    TEST_ASSERT_EQUAL(AnswerStatus::Incorrect,
                      checkAnswer(r, AnswerKind::Reading, "ground").status);
}

static void test_empty_and_non_kana_readings() {
    const Subject s = kanji();
    TEST_ASSERT_EQUAL(AnswerStatus::Incorrect,
                      checkAnswer(s, AnswerKind::Meaning, "   ").status);
    TEST_ASSERT_EQUAL(AnswerStatus::Incorrect,
                      checkAnswer(s, AnswerKind::Reading, "   ").status);
    TEST_ASSERT_EQUAL(AnswerStatus::Warning,
                      checkAnswer(s, AnswerKind::Reading, "abc123").status);
}

// ---------------------------------------------------------------- lesson quiz
static void test_lesson_quiz_meaning_then_reading() {
    const Subject s = kanji();
    LessonQuiz q;
    q.start(s);
    TEST_ASSERT_TRUE(q.active());
    TEST_ASSERT_FALSE(q.complete());
    TEST_ASSERT_EQUAL(AnswerKind::Meaning, q.part());

    q.submit("eat");
    TEST_ASSERT_TRUE(q.feedback());
    TEST_ASSERT_EQUAL(AnswerStatus::Correct, q.result().status);
    q.advance();
    TEST_ASSERT_EQUAL(AnswerKind::Reading, q.part());

    q.submit("shoku");
    TEST_ASSERT_EQUAL(AnswerStatus::Correct, q.result().status);
    q.advance();
    TEST_ASSERT_TRUE(q.complete());
    TEST_ASSERT_FALSE(q.active());
    TEST_ASSERT_EQUAL(0, q.mistakes());
}

static void test_lesson_quiz_retries_wrong_and_warning() {
    const Subject s = kanji();
    LessonQuiz q;
    q.start(s);

    q.submit("drink");
    TEST_ASSERT_EQUAL(AnswerStatus::Incorrect, q.result().status);
    TEST_ASSERT_EQUAL(1, q.mistakes());
    q.advance();
    TEST_ASSERT_FALSE(q.feedback());
    TEST_ASSERT_EQUAL(AnswerKind::Meaning, q.part());

    q.submit("consum");
    TEST_ASSERT_EQUAL(AnswerStatus::Warning, q.result().status);
    TEST_ASSERT_EQUAL(1, q.mistakes());
    q.advance();
    TEST_ASSERT_EQUAL(AnswerKind::Meaning, q.part());

    q.submit("consume");
    q.advance();
    TEST_ASSERT_EQUAL(AnswerKind::Reading, q.part());

    q.submit("eat");
    TEST_ASSERT_EQUAL(AnswerStatus::Warning, q.result().status);
    q.advance();
    TEST_ASSERT_EQUAL(AnswerKind::Reading, q.part());

    q.submit("shoku");
    q.advance();
    TEST_ASSERT_TRUE(q.complete());
    TEST_ASSERT_EQUAL(1, q.mistakes());
}

static void test_lesson_quiz_radical_only() {
    const Subject s = radical();
    LessonQuiz q;
    q.start(s);
    TEST_ASSERT_FALSE(q.needsReading());
    q.submit("ground");
    q.advance();
    TEST_ASSERT_TRUE(q.complete());
}

// ---------------------------------------------------------------- lesson order
static LessonCandidate lessonCandidate(uint32_t assignmentId, uint32_t subjectId, uint8_t level,
                                       uint16_t position,
                                       SubjectType type = SubjectType::Kanji) {
    LessonCandidate c;
    c.assignmentId = assignmentId;
    c.subjectId = subjectId;
    c.level = level;
    c.lessonPosition = position;
    c.type = type;
    return c;
}

static void test_lesson_order_wanikani_level_then_position() {
    std::vector<LessonCandidate> candidates = {
        lessonCandidate(101, 11, 2, 1),
        lessonCandidate(102, 12, 1, 5),
        lessonCandidate(103, 13, 1, 2),
        lessonCandidate(104, 14, 3, 0),
    };
    sortLessonCandidates(&candidates);
    TEST_ASSERT_EQUAL_UINT32(103, candidates[0].assignmentId);
    TEST_ASSERT_EQUAL_UINT32(102, candidates[1].assignmentId);
    TEST_ASSERT_EQUAL_UINT32(101, candidates[2].assignmentId);
    TEST_ASSERT_EQUAL_UINT32(104, candidates[3].assignmentId);
}

static void test_lesson_order_ties_break_by_assignment_id() {
    std::vector<LessonCandidate> candidates = {
        lessonCandidate(300, 30, 1, 4),
        lessonCandidate(200, 20, 1, 4),
        lessonCandidate(100, 10, 1, 4),
    };
    sortLessonCandidates(&candidates);
    TEST_ASSERT_EQUAL_UINT32(100, candidates[0].assignmentId);
    TEST_ASSERT_EQUAL_UINT32(200, candidates[1].assignmentId);
    TEST_ASSERT_EQUAL_UINT32(300, candidates[2].assignmentId);
}

static void test_lesson_order_keeps_unknown_level_last() {
    std::vector<LessonCandidate> candidates = {
        lessonCandidate(100, 10, 0, 0),
        lessonCandidate(200, 20, 5, 9),
    };
    sortLessonCandidates(&candidates);
    TEST_ASSERT_EQUAL_UINT32(200, candidates[0].assignmentId);
    TEST_ASSERT_EQUAL_UINT32(100, candidates[1].assignmentId);
}

static void test_lesson_order_sorts_the_whole_candidate_pool() {
    std::vector<LessonCandidate> candidates;
    for (uint32_t i = 0; i < 60; ++i)
        candidates.push_back(
            lessonCandidate(500 + i, 1000 + i, 2, static_cast<uint16_t>(i + 1),
                            SubjectType::Vocabulary));
    candidates.push_back(lessonCandidate(999, 2000, 1, 99, SubjectType::Radical));
    sortLessonCandidates(&candidates);
    TEST_ASSERT_EQUAL_UINT32(999, candidates[0].assignmentId);
    TEST_ASSERT_EQUAL_UINT32(500, candidates[1].assignmentId);
}

// ---------------------------------------------------------------- session queue
static Assignment assignment(uint32_t id, uint32_t subjectId, uint8_t stage, uint32_t available,
                             SubjectType type = SubjectType::Kanji) {
    Assignment a;
    a.id = id;
    a.subjectId = subjectId;
    a.type = type;
    a.srsStage = stage;
    a.availableAt = available;
    return a;
}

static void test_session_meaning_then_reading() {
    const Subject s = kanji();
    ReviewQueue q;
    q.build({assignment(10, s.id, 3, 100)}, {s}, 1000);
    TEST_ASSERT_EQUAL(1, q.remaining());
    TEST_ASSERT_EQUAL(3, q.current()->srsStage);
    TEST_ASSERT_EQUAL(ReviewPart::Meaning, q.part());
    AnswerOutcome o = q.answer(AnswerStatus::Correct);
    TEST_ASSERT_TRUE(o.advanced);
    TEST_ASSERT_FALSE(o.completed);
    TEST_ASSERT_EQUAL(ReviewPart::Reading, q.part());
    o = q.answer(AnswerStatus::Correct);
    TEST_ASSERT_TRUE(o.completed);
    TEST_ASSERT_TRUE(q.empty());
    TEST_ASSERT_EQUAL(1, q.completedCount());
}

static void test_session_reading_first() {
    const Subject s = kanji();
    ReviewQueue q;
    q.setMeaningFirst(false);
    q.build({assignment(12, s.id, 3, 100)}, {s}, 1000);
    TEST_ASSERT_EQUAL(ReviewPart::Reading, q.part());
    AnswerOutcome o = q.answer(AnswerStatus::Correct);
    TEST_ASSERT_TRUE(o.advanced);
    TEST_ASSERT_FALSE(o.completed);
    TEST_ASSERT_EQUAL(ReviewPart::Meaning, q.part());
    o = q.answer(AnswerStatus::Correct);
    TEST_ASSERT_TRUE(o.completed);
    TEST_ASSERT_TRUE(q.empty());
}

static void test_session_wrong_parts_requeue_and_count() {
    const Subject s = kanji();
    ReviewQueue q;
    q.build({assignment(11, s.id, 3, 100)}, {s}, 1000);
    AnswerOutcome o = q.answer(AnswerStatus::Incorrect);
    TEST_ASSERT_TRUE(o.counted);
    TEST_ASSERT_EQUAL(ReviewPart::Reading, q.part());
    o = q.answer(AnswerStatus::Incorrect);
    TEST_ASSERT_TRUE(o.requeued);
    TEST_ASSERT_EQUAL(1, q.remaining());
    TEST_ASSERT_EQUAL(ReviewPart::Meaning, q.part());
    TEST_ASSERT_EQUAL(1, q.current()->incorrectMeaning);
    TEST_ASSERT_EQUAL(1, q.current()->incorrectReading);
    q.answer(AnswerStatus::Correct);
    TEST_ASSERT_EQUAL(ReviewPart::Reading, q.part());
    q.answer(AnswerStatus::Correct);
    TEST_ASSERT_TRUE(q.empty());
    TEST_ASSERT_EQUAL(1, q.completed().front().incorrectMeaning);
    TEST_ASSERT_EQUAL(1, q.completed().front().incorrectReading);
}

static void test_session_warning_does_not_advance() {
    const Subject s = kanji();
    ReviewQueue q;
    q.build({assignment(12, s.id, 3, 100)}, {s}, 1000);
    const AnswerOutcome o = q.answer(AnswerStatus::Warning);
    TEST_ASSERT_FALSE(o.advanced);
    TEST_ASSERT_FALSE(o.counted);
    TEST_ASSERT_EQUAL(ReviewPart::Meaning, q.part());
    TEST_ASSERT_EQUAL(0, q.current()->incorrectMeaning);
}

static void test_session_radical_only_meaning() {
    const Subject s = radical();
    ReviewQueue q;
    q.build({assignment(13, s.id, 1, 100, SubjectType::Radical)}, {s}, 1000);
    TEST_ASSERT_EQUAL(ReviewPart::Meaning, q.part());
    AnswerOutcome o = q.answer(AnswerStatus::Incorrect);
    TEST_ASSERT_TRUE(o.requeued);
    TEST_ASSERT_EQUAL(ReviewPart::Meaning, q.part());
    o = q.answer(AnswerStatus::Correct);
    TEST_ASSERT_TRUE(o.completed);
}

static void test_session_filters_and_limit() {
    const Subject s = kanji();
    std::vector<Assignment> as = {
        assignment(1, s.id, 3, 100),   // due
        assignment(2, s.id, 0, 100),   // lesson
        assignment(3, s.id, 9, 100),   // burned
        assignment(4, s.id, 3, 2000),  // future
        assignment(5, s.id, 3, 100),   // due
        assignment(6, s.id, 3, 100),   // hidden
        assignment(7, s.id, 3, 100),   // pending
    };
    as[5].hidden = true;
    as[6].pendingReview = true;
    ReviewQueue q;
    q.build(as, {s}, 1000, 0);
    TEST_ASSERT_EQUAL(2, q.remaining());
    q.build(as, {s}, 1000, 1);
    TEST_ASSERT_EQUAL(1, q.remaining());
}

static void queueIds(const ReviewQueue& q, std::vector<uint32_t>* out) {
    out->clear();
    for (const SessionItem& item : q.queue()) out->push_back(item.subjectId);
}

static void test_queue_critical_items_first() {
    Subject a = kanji();
    a.id = 1;
    Subject b = vocabulary();
    b.id = 2;
    Subject c = kanji();
    c.id = 3;
    ReviewQueue q;
    q.setCriticalSubjectIds({2});
    q.build({assignment(1, 1, 3, 100), assignment(2, 2, 3, 100), assignment(3, 3, 3, 100)},
            {a, b, c}, 1000, 3, 7);
    TEST_ASSERT_EQUAL(3, q.remaining());
    TEST_ASSERT_EQUAL_UINT32(2, q.queue()[0].subjectId);
}

static void test_queue_critical_limit_keeps_only_critical() {
    Subject a = kanji();
    a.id = 1;
    Subject b = vocabulary();
    b.id = 2;
    Subject c = kanji();
    c.id = 3;
    ReviewQueue q;
    q.setCriticalSubjectIds({3});
    q.build({assignment(1, 1, 3, 100), assignment(2, 2, 3, 100), assignment(3, 3, 3, 100)},
            {a, b, c}, 1000, 1, 7);
    TEST_ASSERT_EQUAL(1, q.remaining());
    TEST_ASSERT_EQUAL_UINT32(3, q.queue()[0].subjectId);
}

static void test_queue_critical_preserves_normal_order() {
    Subject a = kanji();
    a.id = 1;
    Subject b = vocabulary();
    b.id = 2;
    Subject c = kanji();
    c.id = 3;
    std::vector<Assignment> as = {assignment(1, 1, 3, 100), assignment(2, 2, 3, 100),
                                  assignment(3, 3, 3, 100)};
    ReviewQueue base;
    base.build(as, {a, b, c}, 1000, 3, 7);
    std::vector<uint32_t> baseIds;
    queueIds(base, &baseIds);
    ReviewQueue critical;
    critical.setCriticalSubjectIds({2});
    critical.build(as, {a, b, c}, 1000, 3, 7);
    std::vector<uint32_t> criticalIds;
    queueIds(critical, &criticalIds);
    TEST_ASSERT_EQUAL_UINT32(2, criticalIds[0]);
    std::vector<uint32_t> expected;
    for (uint32_t id : baseIds)
        if (id != 2) expected.push_back(id);
    TEST_ASSERT_EQUAL(static_cast<int>(expected.size()), static_cast<int>(criticalIds.size() - 1));
    for (size_t i = 0; i < expected.size(); ++i)
        TEST_ASSERT_EQUAL_UINT32(expected[i], criticalIds[i + 1]);
}

static void test_queue_without_critical_matches_previous_order() {
    Subject a = kanji();
    a.id = 1;
    Subject b = vocabulary();
    b.id = 2;
    std::vector<Assignment> as = {assignment(1, 1, 3, 100), assignment(2, 2, 3, 100)};
    ReviewQueue one;
    one.build(as, {a, b}, 1000, 2, 11);
    ReviewQueue two;
    two.build(as, {a, b}, 1000, 2, 11);
    std::vector<uint32_t> oneIds;
    std::vector<uint32_t> twoIds;
    queueIds(one, &oneIds);
    queueIds(two, &twoIds);
    TEST_ASSERT_TRUE(oneIds == twoIds);
}

static void test_session_skip() {
    const Subject s = kanji();
    ReviewQueue q;
    q.build({assignment(20, s.id, 3, 100)}, {s}, 1000);
    q.skip();
    TEST_ASSERT_TRUE(q.empty());
    TEST_ASSERT_EQUAL(0, q.completedCount());
}

static void test_session_lower_levels_first() {
    Subject low;
    low.id = 1;
    low.level = 1;
    low.type = SubjectType::Radical;
    low.meanings = {{"ground", true, true}};
    Subject mid;
    mid.id = 2;
    mid.level = 5;
    mid.type = SubjectType::Kanji;
    mid.meanings = {{"eat", true, true}};
    Subject high;
    high.id = 3;
    high.level = 9;
    high.type = SubjectType::Vocabulary;
    high.meanings = {{"adult", true, true}};
    const std::vector<Assignment> as = {
        assignment(1, high.id, 3, 100), assignment(2, low.id, 3, 100),
        assignment(3, mid.id, 3, 100)};
    ReviewQueue q;
    q.setLowerLevelsFirst(true);
    q.build(as, {high, low, mid}, 1000, 123);
    TEST_ASSERT_EQUAL(3, q.remaining());
    TEST_ASSERT_EQUAL(1, q.queue()[0].level);
    TEST_ASSERT_EQUAL(5, q.queue()[1].level);
    TEST_ASSERT_EQUAL(9, q.queue()[2].level);
}

static void test_session_stats_accuracy() {
    SessionItem bothMistakes;
    bothMistakes.needsMeaning = true;
    bothMistakes.needsReading = true;
    bothMistakes.incorrectMeaning = 1;
    bothMistakes.incorrectReading = 1;
    SessionItem meaningOnly;
    meaningOnly.needsMeaning = true;
    meaningOnly.needsReading = false;
    SessionItem secondMeaningOnly;
    secondMeaningOnly.needsMeaning = true;
    secondMeaningOnly.needsReading = false;
    const SessionStats stats =
        summarizeSession({bothMistakes, meaningOnly, secondMeaningOnly});
    TEST_ASSERT_EQUAL_UINT32(4, stats.parts);
    TEST_ASSERT_EQUAL_UINT32(2, stats.mistakes);
    TEST_ASSERT_EQUAL_UINT32(67, stats.accuracy);

    TEST_ASSERT_EQUAL_UINT32(0, sessionAccuracyPercent(0, 0));
    TEST_ASSERT_EQUAL_UINT32(0, sessionAccuracyPercent(0, 4));
    TEST_ASSERT_EQUAL_UINT32(100, sessionAccuracyPercent(5, 0));
    TEST_ASSERT_EQUAL_UINT32(75, sessionAccuracyPercent(3, 1));
    TEST_ASSERT_EQUAL_UINT32(67, sessionAccuracyPercent(2, 1));
    TEST_ASSERT_EQUAL_UINT32(33, sessionAccuracyPercent(1, 2));
}

static void test_session_duration_text() {
    char out[16];
    formatSessionDuration(0, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("0s", out);
    formatSessionDuration(42900, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("42s", out);
    formatSessionDuration((12u * 60u + 59u) * 1000u, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("12m", out);
    formatSessionDuration((1u * 3600u + 5u * 60u) * 1000u, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("1h 5m", out);
    formatSessionDuration(3600u * 1000u, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("1h 0m", out);
}


// ---------------------------------------------------------------- key map
static uint8_t codeAt(int x, int y) {
    const int r = x / 2;
    const int c = y + (x % 2 ? 4 : 0);
    return static_cast<uint8_t>(1 + r * 10 + c);
}

static void test_cardputer_characters() {
    TEST_ASSERT_EQUAL_CHAR('a', tgc::cardputerCharAt(2, 2, false));
    TEST_ASSERT_EQUAL_CHAR('A', tgc::cardputerCharAt(2, 2, true));
    TEST_ASSERT_EQUAL_CHAR(';', tgc::cardputerCharAt(11, 2, false));
    TEST_ASSERT_EQUAL_CHAR(':', tgc::cardputerCharAt(11, 2, true));
    TEST_ASSERT_EQUAL_CHAR(',', tgc::cardputerCharAt(10, 3, false));
    TEST_ASSERT_EQUAL_CHAR('<', tgc::cardputerCharAt(10, 3, true));
    TEST_ASSERT_EQUAL_CHAR(' ', tgc::cardputerCharAt(13, 3, false));
    TEST_ASSERT_EQUAL_CHAR(0, tgc::cardputerCharAt(0, 2, false));   // fn
    TEST_ASSERT_EQUAL_CHAR(0, tgc::cardputerCharAt(13, 2, false));  // enter
}

static void test_cardputer_modifiers() {
    TEST_ASSERT_EQUAL(tgc::Modifier::Shift, tgc::cardputerModifierAt(1, 2));
    TEST_ASSERT_EQUAL(tgc::Modifier::Fn, tgc::cardputerModifierAt(0, 2));
    TEST_ASSERT_EQUAL(tgc::Modifier::Ctrl, tgc::cardputerModifierAt(0, 3));
    TEST_ASSERT_EQUAL(tgc::Modifier::Opt, tgc::cardputerModifierAt(1, 3));
    TEST_ASSERT_EQUAL(tgc::Modifier::Alt, tgc::cardputerModifierAt(2, 3));
    TEST_ASSERT_EQUAL(tgc::Modifier::None, tgc::cardputerModifierAt(2, 2));
}

static void test_cardputer_event_fields() {
    tgc::KeyEvent e;
    TEST_ASSERT_TRUE(tgc::cardputerAdvEvent(static_cast<uint8_t>(0x80 | codeAt(2, 2)), &e));
    TEST_ASSERT_TRUE(e.down);
    TEST_ASSERT_EQUAL(2, e.x);
    TEST_ASSERT_EQUAL(2, e.y);
    TEST_ASSERT_EQUAL(codeAt(2, 2), e.code);
    TEST_ASSERT_EQUAL(tgc::Key::Other, e.key);
    TEST_ASSERT_EQUAL(tgc::KeySource::Matrix, e.source);
    TEST_ASSERT_EQUAL_CHAR(0, e.ch);
    TEST_ASSERT_EQUAL_UINT8(0, e.mods);
    TEST_ASSERT_TRUE(tgc::cardputerAdvEvent(static_cast<uint8_t>(codeAt(11, 2)), &e));
    TEST_ASSERT_FALSE(e.down);
    TEST_ASSERT_EQUAL(tgc::Key::Up, e.key);
}

// ---------------------------------------------------------------- help overlay
static void test_help_question_mark_is_shift_slash() {
    // CFG-04: / is the Right arrow, so the help chord must be recognised by
    // grid position and Shift rather than by Key::Other.
    TEST_ASSERT_EQUAL(tgc::Key::Right, tgc::cardputerKeyAt(12, 3));
    TEST_ASSERT_EQUAL_CHAR('?', tgc::cardputerCharAt(12, 3, true));
    TEST_ASSERT_EQUAL_CHAR('/', tgc::cardputerCharAt(12, 3, false));
}

static void test_help_tables_are_short() {
    const kk::HelpTable none = kk::helpFor(kk::HelpScreen::None);
    TEST_ASSERT_NULL(none.rows);
    TEST_ASSERT_EQUAL(0, static_cast<int>(none.count));
    for (kk::HelpScreen screen : kk::kHelpScreens) {
        const kk::HelpTable table = kk::helpFor(screen);
        TEST_ASSERT_NOT_NULL(table.rows);
        TEST_ASSERT_GREATER_THAN(0, static_cast<int>(table.count));
        TEST_ASSERT_LESS_OR_EQUAL(static_cast<int>(kk::kHelpMaxRows),
                                   static_cast<int>(table.count));
        TEST_ASSERT_TRUE(kk::helpScreenTitle(screen)[0] != '\0');
        for (size_t i = 0; i < table.count; ++i) {
            TEST_ASSERT_NOT_NULL(table.rows[i].keys);
            TEST_ASSERT_NOT_NULL(table.rows[i].action);
            TEST_ASSERT_TRUE(table.rows[i].keys[0] != '\0');
            TEST_ASSERT_TRUE(table.rows[i].action[0] != '\0');
            // Font0 is 6 px per character: 34 characters leave a 20 px gap in
            // the 224 px between x=8 and x=232.
            const size_t width = std::strlen(table.rows[i].keys) +
                                 std::strlen(table.rows[i].action);
            TEST_ASSERT_LESS_OR_EQUAL(34, static_cast<int>(width));
            for (size_t j = i + 1; j < table.count; ++j)
                TEST_ASSERT_TRUE(std::strcmp(table.rows[i].keys, table.rows[j].keys) != 0);
        }
    }
}


// ---------------------------------------------------------------- json/time
static void test_json_values_and_escapes() {
    JsonParser p;
    JsonValue v;
    const char* text =
        "{\"a\":1,\"b\":[true,null,\"x\\n\\u3042\"],\"c\":{\"d\":-2.5e1},"
        "\"e\":\"\\uD83D\\uDE00\"}";
    TEST_ASSERT_TRUE(p.parse(text, &v));
    TEST_ASSERT_TRUE(v.isObject());
    TEST_ASSERT_EQUAL(1, static_cast<int>(v.find("a")->number));
    const JsonValue* b = v.find("b");
    TEST_ASSERT_TRUE(b && b->isArray());
    TEST_ASSERT_EQUAL(3, b->size());
    TEST_ASSERT_TRUE(b->at(0)->boolean);
    TEST_ASSERT_EQUAL(JsonType::Null, b->at(1)->type);
    TEST_ASSERT_EQUAL_STRING("x\nあ", b->at(2)->string.c_str());
    TEST_ASSERT_EQUAL(-25, static_cast<int>(v.find("c")->find("d")->number));
    TEST_ASSERT_EQUAL_STRING("😀", v.find("e")->string.c_str());
}

static void test_json_rejects_bad_input() {
    JsonParser p;
    JsonValue v;
    TEST_ASSERT_FALSE(p.parse("{\"a\":1,}", &v));
    TEST_ASSERT_FALSE(p.parse("{\"a\":", &v));
    TEST_ASSERT_FALSE(p.parse("{\"a\":1} trailing", &v));
    TEST_ASSERT_FALSE(p.parse("", &v));
    TEST_ASSERT_FALSE(p.error().empty());
}

// ---------------------------------------------------------------- JSON stream
struct StreamEvent {
    enum Kind : uint8_t {
        ObjectStart,
        ObjectEnd,
        ArrayStart,
        ArrayEnd,
        Key,
        String,
        Number,
        Bool,
        Null,
    };
    Kind kind;
    std::string text;
    double number = 0;
    bool boolean = false;
};

class StreamEventSink : public JsonSink {
public:
    std::vector<StreamEvent> events;

    bool onObjectStart() override { return add(StreamEvent::ObjectStart); }
    bool onObjectEnd() override { return add(StreamEvent::ObjectEnd); }
    bool onArrayStart() override { return add(StreamEvent::ArrayStart); }
    bool onArrayEnd() override { return add(StreamEvent::ArrayEnd); }
    bool onKey(std::string_view key) override {
        return add(StreamEvent::Key, std::string(key));
    }
    bool onString(std::string_view value) override {
        return add(StreamEvent::String, std::string(value));
    }
    bool onNumber(double value) override {
        StreamEvent e{StreamEvent::Number};
        e.number = value;
        events.push_back(e);
        return true;
    }
    bool onBool(bool value) override {
        StreamEvent e{StreamEvent::Bool};
        e.boolean = value;
        events.push_back(e);
        return true;
    }
    bool onNull() override { return add(StreamEvent::Null); }

private:
    bool add(StreamEvent::Kind kind, std::string text = std::string()) {
        StreamEvent e{kind};
        e.text = std::move(text);
        events.push_back(e);
        return true;
    }
};

static void test_json_stream_events() {
    StreamEventSink sink;
    JsonStreamParser p(&sink);
    TEST_ASSERT_TRUE(p.parse(R"json({"a":1,"b":[true,null,"x\n\u3042"],"c":{"d":-25}})json"));
    TEST_ASSERT_EQUAL(15, sink.events.size());
    TEST_ASSERT_EQUAL(StreamEvent::ObjectStart, sink.events[0].kind);
    TEST_ASSERT_EQUAL(StreamEvent::Key, sink.events[1].kind);
    TEST_ASSERT_EQUAL_STRING("a", sink.events[1].text.c_str());
    TEST_ASSERT_EQUAL(StreamEvent::Number, sink.events[2].kind);
    TEST_ASSERT_TRUE(std::fabs(sink.events[2].number - 1.0) < 1e-9);
    TEST_ASSERT_EQUAL(StreamEvent::Key, sink.events[3].kind);
    TEST_ASSERT_EQUAL_STRING("b", sink.events[3].text.c_str());
    TEST_ASSERT_EQUAL(StreamEvent::ArrayStart, sink.events[4].kind);
    TEST_ASSERT_EQUAL(StreamEvent::Bool, sink.events[5].kind);
    TEST_ASSERT_TRUE(sink.events[5].boolean);
    TEST_ASSERT_EQUAL(StreamEvent::Null, sink.events[6].kind);
    TEST_ASSERT_EQUAL(StreamEvent::String, sink.events[7].kind);
    TEST_ASSERT_EQUAL_STRING("x\nあ", sink.events[7].text.c_str());
    TEST_ASSERT_EQUAL(StreamEvent::ArrayEnd, sink.events[8].kind);
    TEST_ASSERT_EQUAL(StreamEvent::Key, sink.events[9].kind);
    TEST_ASSERT_EQUAL(StreamEvent::ObjectStart, sink.events[10].kind);
    TEST_ASSERT_EQUAL(StreamEvent::Key, sink.events[11].kind);
    TEST_ASSERT_EQUAL(StreamEvent::Number, sink.events[12].kind);
    TEST_ASSERT_TRUE(std::fabs(sink.events[12].number + 25.0) < 1e-9);
    TEST_ASSERT_EQUAL(StreamEvent::ObjectEnd, sink.events[13].kind);
    TEST_ASSERT_EQUAL(StreamEvent::ObjectEnd, sink.events[14].kind);
}

static void test_json_stream_escapes() {
    StreamEventSink sink;
    JsonStreamParser p(&sink);
    TEST_ASSERT_TRUE(p.parse(
        R"json({"a\u0062":"quote:\" slash:\\ solidus:\/ tab:\t unicode:\u3042 emoji:\uD83D\uDE00"})json"));
    TEST_ASSERT_EQUAL(4, sink.events.size());
    TEST_ASSERT_EQUAL(StreamEvent::Key, sink.events[1].kind);
    TEST_ASSERT_EQUAL_STRING("ab", sink.events[1].text.c_str());
    TEST_ASSERT_EQUAL_STRING("quote:\" slash:\\ solidus:/ tab:\t unicode:あ emoji:😀",
                             sink.events[2].text.c_str());

    const char* bad[] = {
        R"json({"s":"\uD800"})json",
        R"json({"s":"\uDC00"})json",
        R"json({"s":"\u12"})json",
        R"json({"s":"\q"})json",
    };
    for (const char* text : bad) {
        StreamEventSink rejected;
        JsonStreamParser invalid(&rejected);
        TEST_ASSERT_FALSE(invalid.parse(text));
        TEST_ASSERT_FALSE(invalid.error().empty());
    }

    std::string control = "{\"s\":\"a";
    control.push_back('\x01');
    control += "\"}";
    StreamEventSink rejected;
    JsonStreamParser invalid(&rejected);
    TEST_ASSERT_FALSE(invalid.parse(control));
}

static void test_json_stream_numbers() {
    StreamEventSink sink;
    JsonStreamParser p(&sink);
    TEST_ASSERT_TRUE(p.parse("[0,-0,3,12.5,-2.5e1,1E+2,1e-2]"));
    TEST_ASSERT_EQUAL(9, sink.events.size());
    const double expected[] = {0, 0, 3, 12.5, -25, 100, 0.01};
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        const StreamEvent& e = sink.events[i + 1];
        TEST_ASSERT_EQUAL(StreamEvent::Number, e.kind);
        TEST_ASSERT_TRUE(std::fabs(e.number - expected[i]) < 1e-9);
    }

    const char* bad[] = {
        "01",  "-",   "1.",  ".5",    "+1",     "1e",
        "1e+", "NaN", "Infinity", "1e9999", "--1",
    };
    for (const char* text : bad) {
        StreamEventSink rejected;
        JsonStreamParser invalid(&rejected);
        TEST_ASSERT_FALSE(invalid.parse(text));
    }
}

static void test_json_stream_truncation_and_depth() {
    const char* truncated[] = {
        "",
        "   ",
        "{",
        "[",
        R"json({"a":[1,2)json",
        R"json({"a":"x)json",
        R"json({"a":)json",
        R"json({"a":tru})json",
    };
    for (const char* text : truncated) {
        StreamEventSink sink;
        JsonStreamParser p(&sink);
        TEST_ASSERT_FALSE(p.parse(text));
        TEST_ASSERT_FALSE(p.error().empty());
    }

    const char* malformed[] = {
        R"json({"a":1,})json",
        R"json([1,])json",
        R"json({"a" 1})json",
        R"json({"a":1 "b":2})json",
        R"json({"a":1}x)json",
    };
    for (const char* text : malformed) {
        StreamEventSink sink;
        JsonStreamParser p(&sink);
        TEST_ASSERT_FALSE(p.parse(text));
    }

    auto nested = [](int count) {
        std::string text;
        for (int i = 0; i < count; ++i) text.push_back('[');
        text.push_back('0');
        for (int i = 0; i < count; ++i) text.push_back(']');
        return text;
    };
    StreamEventSink sink;
    JsonStreamParser p(&sink);
    TEST_ASSERT_TRUE(p.parse(nested(32)));
    TEST_ASSERT_FALSE(p.parse(nested(33)));
    TEST_ASSERT_FALSE(p.error().empty());

    JsonStreamParser shallow(&sink, 2);
    TEST_ASSERT_TRUE(shallow.parse("[[0]]"));
    TEST_ASSERT_FALSE(shallow.parse("[[[0]]]"));

    JsonStreamParser noSink(nullptr);
    TEST_ASSERT_FALSE(noSink.parse("null"));
}

class RejectingSink : public JsonSink {
public:
    int values = 0;
    int events = 0;

    bool onArrayStart() override {
        ++events;
        return true;
    }
    bool onNumber(double) override {
        ++events;
        return ++values < 2;
    }
    bool onArrayEnd() override {
        ++events;
        return true;
    }
};

static void test_json_stream_sink_rejection() {
    RejectingSink sink;
    JsonStreamParser p(&sink);
    TEST_ASSERT_FALSE(p.parse("[1,2,3]"));
    TEST_ASSERT_EQUAL(2, sink.values);
    TEST_ASSERT_EQUAL(3, sink.events);
    TEST_ASSERT_FALSE(p.error().empty());
}

static void test_iso8601_round_trip() {
    uint32_t e = 0;
    TEST_ASSERT_TRUE(parseIso8601("2026-10-25T12:38:37.375268Z", &e));
    TEST_ASSERT_EQUAL_STRING("2026-10-25T12:38:37Z", formatIso8601(e).c_str());
    TEST_ASSERT_TRUE(parseIso8601("1970-01-01T00:00:00Z", &e));
    TEST_ASSERT_EQUAL_UINT32(0, e);
    TEST_ASSERT_FALSE(parseIso8601("2026-10-25 12:38:37", &e));
    TEST_ASSERT_FALSE(parseIso8601("2026-10-25T12:38:37+02:00", &e));
}

static void test_rate_limit_reset_in_42s() {
    TEST_ASSERT_EQUAL_UINT32(42, rateLimitWaitSeconds(1760000042u, 1760000000u, true));
}

static void test_rate_limit_past_or_zero_falls_back() {
    TEST_ASSERT_EQUAL_UINT32(kRateLimitFallbackSeconds,
                             rateLimitWaitSeconds(0, 1760000000u, true));
    TEST_ASSERT_EQUAL_UINT32(kRateLimitFallbackSeconds,
                             rateLimitWaitSeconds(1759999999u, 1760000000u, true));
}

static void test_rate_limit_untrusted_clock_falls_back() {
    TEST_ASSERT_EQUAL_UINT32(kRateLimitFallbackSeconds,
                             rateLimitWaitSeconds(1760000042u, 1760000000u, false));
}

static void test_rate_limit_far_future_is_capped() {
    TEST_ASSERT_EQUAL_UINT32(kRateLimitMaxSeconds,
                             rateLimitWaitSeconds(1760003600u, 1760000000u, true));
}

static void test_rate_limit_exactly_now_falls_back() {
    TEST_ASSERT_EQUAL_UINT32(kRateLimitFallbackSeconds,
                             rateLimitWaitSeconds(1760000000u, 1760000000u, true));
}

static void test_user_parse() {
    JsonParser p;
    JsonValue v;
    const char* text =
        "{\"data\":{\"username\":\"tester\",\"level\":8,"
        "\"current_vacation_started_at\":\"2026-10-20T12:00:00Z\","
        "\"subscription\":{\"active\":true,\"max_level_granted\":60},"
        "\"preferences\":{\"lessons_batch_size\":10,"
        "\"reviews_presentation_order\":\"lower_levels_first\"}}}";
    TEST_ASSERT_TRUE(p.parse(text, &v));
    User u;
    TEST_ASSERT_TRUE(parseUser(v, &u));
    TEST_ASSERT_EQUAL_STRING("tester", u.username.c_str());
    TEST_ASSERT_EQUAL(8, u.level);
    TEST_ASSERT_TRUE(u.subscriptionActive);
    TEST_ASSERT_EQUAL(60, u.maxLevelGranted);
    TEST_ASSERT_EQUAL(10, u.lessonsBatchSize);
    TEST_ASSERT_EQUAL(ReviewOrder::LowerLevelsFirst, u.reviewsOrder);
    uint32_t vacation = 0;
    TEST_ASSERT_TRUE(parseIso8601("2026-10-20T12:00:00Z", &vacation));
    TEST_ASSERT_EQUAL_UINT32(vacation, u.vacationStartedAt);
}

static void test_summary_parse_and_counts() {
    JsonParser p;
    JsonValue v;
    const char* text =
        "{\"object\":\"report\",\"data\":{"
        "\"lessons\":[{\"available_at\":\"2026-10-25T12:00:00Z\",\"subject_ids\":[1,2,3]}],"
        "\"next_reviews_at\":\"2026-10-25T13:00:00Z\","
        "\"reviews\":["
        "{\"available_at\":\"2026-10-25T12:00:00Z\",\"subject_ids\":[10,11]},"
        "{\"available_at\":\"2026-10-25T13:00:00Z\",\"subject_ids\":[12]}]}}";
    TEST_ASSERT_TRUE(p.parse(text, &v));
    Summary s;
    TEST_ASSERT_TRUE(parseSummary(v, &s));
    TEST_ASSERT_EQUAL(3, s.totalReviews());
    TEST_ASSERT_EQUAL(3, s.totalLessons());
    TEST_ASSERT_EQUAL_STRING("2026-10-25T13:00:00Z", s.nextReviewsAt.c_str());
    uint32_t now = 0;
    TEST_ASSERT_TRUE(parseIso8601("2026-10-25T12:30:00Z", &now));
    TEST_ASSERT_EQUAL(2, s.dueReviews(now));
    uint32_t next = 0;
    TEST_ASSERT_TRUE(parseIso8601("2026-10-25T13:00:00Z", &next));
    TEST_ASSERT_EQUAL_UINT32(next, s.nextReviewAt(now));
}

static void addReviewBucket(Summary* s, uint32_t at, uint16_t count) {
    SummaryBucket b;
    b.availableAt = at;
    b.count = count;
    s->reviews.push_back(b);
}

static void test_forecast_line_due_with_next() {
    Summary s;
    addReviewBucket(&s, 100, 5);
    addReviewBucket(&s, 3600, 12);
    char out[20];
    formatNextReview(s, 200, "18:00", out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("+12 at 18:00", out);
}

static void test_forecast_line_idle_with_next() {
    Summary s;
    addReviewBucket(&s, 3600, 12);
    char out[20];
    formatNextReview(s, 200, "18:00", out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("18:00 (+12)", out);
}

static void test_forecast_line_due_without_next() {
    Summary s;
    addReviewBucket(&s, 100, 5);
    char out[20];
    formatNextReview(s, 200, "18:00", out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("now", out);
}

static void test_forecast_line_idle_without_next() {
    Summary s;
    char out[20];
    formatNextReview(s, 200, "18:00", out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("all clear", out);
}

static void test_forecast_count_single_and_zero() {
    Summary s;
    addReviewBucket(&s, 3600, 1);
    TEST_ASSERT_EQUAL(1, s.nextReviewCount(200));
    TEST_ASSERT_EQUAL(0, s.nextReviewCount(4000));
    addReviewBucket(&s, 3600, 3);
    TEST_ASSERT_EQUAL(4, s.nextReviewCount(200));
}

static void test_summary_counts_advance_with_time() {
    const uint32_t t = 1760000000u;
    Summary s;
    addReviewBucket(&s, t, 4);
    addReviewBucket(&s, t + 3600, 6);
    TEST_ASSERT_EQUAL_UINT32(4, s.dueReviews(t));
    TEST_ASSERT_EQUAL_UINT32(10, s.dueReviews(t + 3600));
    TEST_ASSERT_EQUAL_UINT32(t + 3600, s.nextReviewAt(t));
    TEST_ASSERT_EQUAL(6, s.nextReviewCount(t));
}

static void test_summary_next_review_after_last_bucket() {
    const uint32_t t = 1760000000u;
    Summary s;
    addReviewBucket(&s, t, 2);
    addReviewBucket(&s, t + 3600, 3);
    TEST_ASSERT_EQUAL_UINT32(0, s.nextReviewAt(t + 7200));
    TEST_ASSERT_EQUAL(0, s.nextReviewCount(t + 7200));
    TEST_ASSERT_EQUAL_UINT32(5, s.dueReviews(t + 7200));
    char out[20];
    formatNextReview(s, t + 7200, "09:00", out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("now", out);
}

static void test_forecast_24_slots_fill_gaps() {
    const uint32_t now = 10000;
    const uint32_t start = (now / 3600u) * 3600u;
    Summary s;
    addReviewBucket(&s, start, 2);
    addReviewBucket(&s, start + 2 * 3600, 5);
    addReviewBucket(&s, start + 23 * 3600, 1);
    addReviewBucket(&s, start + 24 * 3600, 9);
    ForecastStats forecast;
    buildForecast(s, now, &forecast);
    TEST_ASSERT_EQUAL_UINT32(start, forecast.startHour);
    TEST_ASSERT_EQUAL(2, forecast.counts[0]);
    TEST_ASSERT_EQUAL(0, forecast.counts[1]);
    TEST_ASSERT_EQUAL(5, forecast.counts[2]);
    TEST_ASSERT_EQUAL(1, forecast.counts[23]);
    TEST_ASSERT_EQUAL(5, forecast.max);
    TEST_ASSERT_FALSE(forecast.empty());
}

static void test_forecast_includes_current_hour() {
    const uint32_t now = 10000;
    const uint32_t start = (now / 3600u) * 3600u;
    Summary s;
    addReviewBucket(&s, start + 1800, 4);
    ForecastStats forecast;
    buildForecast(s, now, &forecast);
    TEST_ASSERT_EQUAL(4, forecast.counts[0]);
}

static void test_forecast_ignores_past_buckets() {
    const uint32_t now = 10000;
    const uint32_t start = (now / 3600u) * 3600u;
    Summary s;
    addReviewBucket(&s, start - 3600, 7);
    ForecastStats forecast;
    buildForecast(s, now, &forecast);
    TEST_ASSERT_TRUE(forecast.empty());
    TEST_ASSERT_EQUAL(0, forecast.max);
}

static void test_forecast_max_and_empty() {
    Summary s;
    ForecastStats forecast;
    buildForecast(s, 10000, &forecast);
    TEST_ASSERT_TRUE(forecast.empty());
    TEST_ASSERT_EQUAL(0, forecast.max);
}

static void test_kanji_needed_rounds_up() {
    TEST_ASSERT_EQUAL(19, kanjiNeededForNinetyPercent(17, 40));
}

static void test_kanji_needed_zero_when_reached() {
    TEST_ASSERT_EQUAL(0, kanjiNeededForNinetyPercent(36, 40));
    TEST_ASSERT_EQUAL(0, kanjiNeededForNinetyPercent(40, 40));
}

static void test_kanji_needed_unknown_total() {
    TEST_ASSERT_EQUAL(0, kanjiNeededForNinetyPercent(0, 0));
}

static void test_kanji_needed_small_levels() {
    TEST_ASSERT_EQUAL(2, kanjiNeededForNinetyPercent(1, 3));
    TEST_ASSERT_EQUAL(1, kanjiNeededForNinetyPercent(2, 3));
}

// ---------------------------------------------------------------- journal
static void test_journal_record_roundtrip() {
    JournalRecord in;
    in.completedAt = 1760000000;
    in.subjectId = 42;
    in.assignmentId = 4242;
    in.srsBefore = 3;
    in.srsAfter = 4;
    in.srsAfterKnown = true;
    in.offline = true;
    in.wrongAnswerOverride = 'A';
    in.incorrectMeaning = 2;
    in.incorrectReading = 1;
    uint8_t encoded[kJournalRecordBytes];
    journalEncodeRecord(in, encoded);
    JournalRecord out;
    TEST_ASSERT_TRUE(journalDecodeRecord(encoded, &out));
    TEST_ASSERT_EQUAL_UINT32(in.completedAt, out.completedAt);
    TEST_ASSERT_EQUAL_UINT32(in.subjectId, out.subjectId);
    TEST_ASSERT_EQUAL_UINT32(in.assignmentId, out.assignmentId);
    TEST_ASSERT_EQUAL(in.srsBefore, out.srsBefore);
    TEST_ASSERT_EQUAL(in.srsAfter, out.srsAfter);
    TEST_ASSERT_TRUE(out.srsAfterKnown);
    TEST_ASSERT_TRUE(out.offline);
    TEST_ASSERT_EQUAL_CHAR('A', out.wrongAnswerOverride);
    TEST_ASSERT_FALSE(out.practice);
    TEST_ASSERT_EQUAL(in.incorrectMeaning, out.incorrectMeaning);
    TEST_ASSERT_EQUAL(in.incorrectReading, out.incorrectReading);

    in.wrongAnswerOverride = 'C';
    journalEncodeRecord(in, encoded);
    TEST_ASSERT_TRUE(journalDecodeRecord(encoded, &out));
    TEST_ASSERT_EQUAL_CHAR('C', out.wrongAnswerOverride);

    in.wrongAnswerOverride = 0;
    journalEncodeRecord(in, encoded);
    TEST_ASSERT_TRUE(journalDecodeRecord(encoded, &out));
    TEST_ASSERT_EQUAL_CHAR(0, out.wrongAnswerOverride);
}

static void test_journal_block_crc() {
    JournalRecord records[kJournalRecordsPerBlock];
    for (size_t i = 0; i < kJournalRecordsPerBlock; ++i) {
        records[i].subjectId = static_cast<uint32_t>(i + 1);
        records[i].assignmentId = static_cast<uint32_t>(100 + i);
        records[i].srsBefore = static_cast<uint8_t>(i % 9);
    }
    uint8_t block[kJournalBlockBytes];
    journalEncodeBlock(records, 7, block);
    JournalRecord decoded[kJournalRecordsPerBlock];
    uint32_t seq = 0;
    TEST_ASSERT_TRUE(journalDecodeBlock(block, decoded, &seq));
    TEST_ASSERT_EQUAL_UINT32(7, seq);
    TEST_ASSERT_EQUAL_UINT32(9, decoded[8].subjectId);
    block[5] ^= 0x01;
    TEST_ASSERT_FALSE(journalDecodeBlock(block, decoded, &seq));
}

static void test_journal_ring_wraps() {
    TEST_ASSERT_EQUAL_UINT32(0, journalNextIndex(kJournalMaxRecords - 1));
    TEST_ASSERT_EQUAL_UINT32(5, journalNextIndex(4));
    TEST_ASSERT_EQUAL_UINT32(0, journalSlot(0));
    TEST_ASSERT_EQUAL_UINT32(1, journalSlot(33));
}

static void test_journal_patch_srs_after() {
    JournalRecord record;
    journalPatchSrsAfter(&record, 7);
    TEST_ASSERT_TRUE(record.srsAfterKnown);
    TEST_ASSERT_EQUAL(7, record.srsAfter);
    uint8_t encoded[kJournalRecordBytes];
    journalEncodeRecord(record, encoded);
    JournalRecord decoded;
    TEST_ASSERT_TRUE(journalDecodeRecord(encoded, &decoded));
    TEST_ASSERT_TRUE(decoded.srsAfterKnown);
    TEST_ASSERT_EQUAL(7, decoded.srsAfter);
}

static void test_journal_block_rejects_zero_crc() {
    uint8_t block[kJournalBlockBytes] = {};
    JournalRecord decoded[kJournalRecordsPerBlock];
    uint32_t seq = 0;
    TEST_ASSERT_FALSE(journalDecodeBlock(block, decoded, &seq));
}

static void test_journal_seven_day_counts() {
    const DayStats days[3] = {{100, 3, 2, 1}, {99, 2, 2, 0}, {94, 1, 1, 0}};
    HistoryStats stats;
    buildHistory(days, 3, 100, &stats);
    TEST_ASSERT_TRUE(stats.any);
    TEST_ASSERT_EQUAL(3, stats.counts[6]);
    TEST_ASSERT_EQUAL(2, stats.counts[5]);
    TEST_ASSERT_EQUAL(1, stats.counts[0]);
}

static void test_journal_today_accuracy() {
    const DayStats days[1] = {{100, 3, 2, 1}};
    HistoryStats stats;
    buildHistory(days, 1, 100, &stats);
    TEST_ASSERT_TRUE(stats.hasToday);
    TEST_ASSERT_EQUAL(67, stats.todayAccuracy);
}

static void test_journal_current_streak_today_empty() {
    const DayStats days[2] = {{99, 2, 2, 0}, {98, 1, 1, 0}};
    HistoryStats stats;
    buildHistory(days, 2, 100, &stats);
    TEST_ASSERT_FALSE(stats.hasToday);
    TEST_ASSERT_EQUAL(2, stats.currentStreak);
}

static void test_journal_best_streak_with_gaps() {
    const DayStats days[5] = {{90, 1, 1, 0},
                              {91, 1, 1, 0},
                              {92, 1, 1, 0},
                              {94, 1, 1, 0},
                              {95, 1, 1, 0}};
    HistoryStats stats;
    buildHistory(days, 5, 96, &stats);
    TEST_ASSERT_EQUAL(3, stats.bestStreak);
    TEST_ASSERT_EQUAL(2, stats.currentStreak);
}

static void test_journal_day_key_time_zone_boundaries() {
    uint32_t epoch = 0;
    TEST_ASSERT_TRUE(parseIso8601("2026-01-01T00:30:00Z", &epoch));
    TEST_ASSERT_EQUAL_UINT32(civilDayKey(2026, 1, 1), localDayKeyAtOffset(epoch, 2 * 3600));
    TEST_ASSERT_EQUAL_UINT32(civilDayKey(2025, 12, 31), localDayKeyAtOffset(epoch, -5 * 3600));
}

static void test_journal_empty() {
    HistoryStats stats;
    buildHistory(nullptr, 0, 100, &stats);
    TEST_ASSERT_FALSE(stats.any);
    TEST_ASSERT_EQUAL(0, stats.currentStreak);
    TEST_ASSERT_EQUAL(0, stats.bestStreak);
}

// ---------------------------------------------------------------- audio
static void test_audio_index_round_trip() {
    const std::vector<AudioEntry> entries = {{10, 1200, 5, 0, AudioFormat::Mp3},
                                             {11, 800, 7, 1, AudioFormat::Ogg}};
    std::vector<uint8_t> bytes;
    TEST_ASSERT_TRUE(encodeAudioIndex(entries, 42, 99, &bytes));
    std::vector<AudioEntry> out;
    TEST_ASSERT_TRUE(decodeAudioIndex(bytes.data(), bytes.size(), 42, &out));
    TEST_ASSERT_EQUAL(2, out.size());
    TEST_ASSERT_EQUAL_UINT32(10, out[0].subjectId);
    TEST_ASSERT_EQUAL(800, out[1].bytes);
    TEST_ASSERT_EQUAL(static_cast<int>(AudioFormat::Ogg), static_cast<int>(out[1].format));
    TEST_ASSERT_FALSE(decodeAudioIndex(bytes.data(), bytes.size(), 43, &out));
}

static void test_audio_choose_voice_fallback() {
    const std::vector<AudioEntry> entries = {{10, 100, 0, 0, AudioFormat::Mp3}};
    AudioEntry out;
    TEST_ASSERT_TRUE(audioChooseEntry(entries, 10, 1, &out));
    TEST_ASSERT_EQUAL(0, out.voice);
    TEST_ASSERT_FALSE(audioChooseEntry(entries, 11, 0, &out));
}

static void test_audio_file_name_and_evict() {
    AudioEntry entry{10, 100, 0, 1, AudioFormat::Ogg};
    char name[32];
    TEST_ASSERT_TRUE(audioFileName(entry, name, sizeof name));
    TEST_ASSERT_EQUAL_STRING("10-1.ogg", name);
    const std::vector<AudioEntry> entries = {{1, 60, 100, 0, AudioFormat::Mp3},
                                             {2, 60, 50, 0, AudioFormat::Mp3},
                                             {3, 60, 75, 0, AudioFormat::Mp3}};
    const std::vector<AudioEntry> victims =
        audioEvict(entries, kAudioCacheMaxBytes - 100, 150);
    TEST_ASSERT_EQUAL(1, victims.size());
    TEST_ASSERT_EQUAL_UINT32(2, victims[0].subjectId);
}


// ---------------------------------------------------------------- subject catalog
static Subject catalogSubject() {
    Subject s = kanji();
    s.lessonPosition = 12;
    s.meaningMnemonic = "A person eats.";
    s.readingMnemonic = "shoku";
    s.meaningHint = "food";
    s.readingHint = "long o";
    s.meaningNote = "note";
    s.readingNote = "reading note";
    s.componentSubjectIds = {1, 2, 3};
    s.visuallySimilarSubjectIds = {7, 8};
    s.amalgamationSubjectIds = {9, 10};
    s.partsOfSpeech = {"noun", "proper noun"};
    s.contextSentences = {{"これは日本です。", "This is Japan."}};
    s.meaningCorrect = 8;
    s.meaningIncorrect = 2;
    s.readingCorrect = 7;
    s.readingIncorrect = 1;
    s.meaningMaxStreak = 5;
    s.meaningCurrentStreak = 3;
    s.readingMaxStreak = 4;
    s.readingCurrentStreak = 2;
    s.percentageCorrect = 86;
    s.hasReviewStats = true;
    return s;
}

static void test_subject_codec_roundtrip() {
    const Subject in = catalogSubject();
    std::vector<uint8_t> payload;
    TEST_ASSERT_TRUE(subjectEncodePayload(in, &payload));
    Subject out;
    TEST_ASSERT_TRUE(subjectDecodePayload(payload.data(), payload.size(), &out));
    TEST_ASSERT_EQUAL_UINT32(in.id, out.id);
    TEST_ASSERT_EQUAL(static_cast<int>(in.type), static_cast<int>(out.type));
    TEST_ASSERT_EQUAL(in.level, out.level);
    // KKCS and the catalog payload do not persist lesson_position; it stays a
    // parse-time field used by the lesson ordering.
    TEST_ASSERT_EQUAL_STRING(in.characters.c_str(), out.characters.c_str());
    TEST_ASSERT_EQUAL_STRING(in.meaningMnemonic.c_str(), out.meaningMnemonic.c_str());
    TEST_ASSERT_EQUAL_STRING(in.readingMnemonic.c_str(), out.readingMnemonic.c_str());
    TEST_ASSERT_EQUAL_STRING(in.meaningNote.c_str(), out.meaningNote.c_str());
    TEST_ASSERT_EQUAL_STRING(in.readingNote.c_str(), out.readingNote.c_str());
    TEST_ASSERT_EQUAL(static_cast<int>(in.componentSubjectIds.size()),
                      static_cast<int>(out.componentSubjectIds.size()));
    TEST_ASSERT_EQUAL(static_cast<int>(in.partsOfSpeech.size()),
                      static_cast<int>(out.partsOfSpeech.size()));
    TEST_ASSERT_EQUAL(static_cast<int>(in.contextSentences.size()),
                      static_cast<int>(out.contextSentences.size()));
    TEST_ASSERT_EQUAL_UINT32(in.meaningCorrect, out.meaningCorrect);
    TEST_ASSERT_EQUAL(in.percentageCorrect, out.percentageCorrect);
    TEST_ASSERT_TRUE(out.hasReviewStats);
}

static void test_subject_codec_matches_cache_subject() {
    SubjectCache cache;
    cache.meta = {0, 0};
    cache.subjects.push_back(catalogSubject());
    std::vector<uint8_t> cacheBytes;
    TEST_ASSERT_TRUE(encodeSubjectCache(cache, &cacheBytes));
    std::vector<uint8_t> payload;
    TEST_ASSERT_TRUE(subjectEncodePayload(cache.subjects[0], &payload));
    TEST_ASSERT_TRUE(cacheBytes.size() == kCacheHeaderBytes + 2 + payload.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload.data(), cacheBytes.data() + kCacheHeaderBytes + 2,
                                 payload.size());
}

static void test_catalog_meta_account_mismatch() {
    CatalogMeta meta;
    meta.accountKey = 123;
    meta.savedAt = 456;
    meta.count = 7;
    meta.bytes = 890;
    uint8_t bytes[kCatalogMetaBytes];
    catalogMetaEncode(meta, bytes);
    CatalogMeta out;
    TEST_ASSERT_TRUE(catalogMetaDecode(bytes, &out));
    TEST_ASSERT_EQUAL_UINT32(123, out.accountKey);
    TEST_ASSERT_TRUE(catalogMetaAccountMatches(out, 123));
    TEST_ASSERT_FALSE(catalogMetaAccountMatches(out, 124));
    bytes[10] ^= 1;
    TEST_ASSERT_FALSE(catalogMetaDecode(bytes, &out));
}

static void test_catalog_lru_victim() {
    const CatalogEntry entries[3] = {{1, 50}, {2, 10}, {3, 10}};
    TEST_ASSERT_EQUAL(1, catalogEvictIndex(entries, 3));
    TEST_ASSERT_EQUAL(0, catalogEvictIndex(nullptr, 0));
    const CatalogEntry single[1] = {{9, 0}};
    TEST_ASSERT_EQUAL(0, catalogEvictIndex(single, 1));
}

static void test_catalog_cap_math() {
    TEST_ASSERT_FALSE(catalogNeedsEvict(1999, 0, 100));
    TEST_ASSERT_TRUE(catalogNeedsEvict(2000, 0, 0));
    TEST_ASSERT_TRUE(catalogNeedsEvict(1, kCatalogMaxBytes, 1));
    TEST_ASSERT_FALSE(catalogNeedsEvict(1, 0, 1024));
}

// ---------------------------------------------------------------- critical items
static ReviewStatistic criticalStat(uint32_t id, uint32_t subjectId, uint8_t percent,
                                    uint32_t answers, bool hidden = false) {
    ReviewStatistic stat;
    stat.id = id;
    stat.subjectId = subjectId;
    stat.percentageCorrect = percent;
    stat.meaningCorrect = answers;
    stat.hidden = hidden;
    return stat;
}

static Assignment criticalAssignment(uint32_t subjectId, uint8_t stage, bool hidden = false) {
    Assignment a;
    a.id = 1000 + subjectId;
    a.subjectId = subjectId;
    a.srsStage = stage;
    a.hidden = hidden;
    return a;
}

static void test_review_statistics_parses_id_and_total_answers() {
    JsonParser p;
    JsonValue v;
    const char* text =
        "{\"data\":[{\"id\":77,\"data\":{\"subject_id\":10,\"meaning_correct\":3,"
        "\"meaning_incorrect\":1,\"reading_correct\":2,\"reading_incorrect\":4,"
        "\"percentage_correct\":60,\"hidden\":false}}]}";
    TEST_ASSERT_TRUE(p.parse(text, &v));
    const std::vector<ReviewStatistic> stats = parseReviewStatistics(v);
    TEST_ASSERT_EQUAL(1, stats.size());
    TEST_ASSERT_EQUAL_UINT32(77, stats[0].id);
    TEST_ASSERT_EQUAL_UINT32(10, stats[0].totalAnswers());
}

static void test_critical_strictly_below_threshold() {
    const std::vector<ReviewStatistic> stats = {
        criticalStat(1, 10, 74, 10), criticalStat(2, 11, 75, 10),
        criticalStat(3, 12, 60, 10)};
    const std::vector<Assignment> assignments = {
        criticalAssignment(10, 2), criticalAssignment(11, 2), criticalAssignment(12, 2)};
    const std::vector<CriticalItem> items = selectCriticalItems(stats, assignments, 75, 5);
    TEST_ASSERT_EQUAL(2, items.size());
    TEST_ASSERT_EQUAL_UINT32(12, items[0].subjectId);
    TEST_ASSERT_EQUAL_UINT32(10, items[1].subjectId);
}

static void test_critical_minimum_answers() {
    const std::vector<ReviewStatistic> stats = {criticalStat(1, 10, 50, 4),
                                                criticalStat(2, 11, 50, 5)};
    const std::vector<Assignment> assignments = {criticalAssignment(10, 2),
                                                  criticalAssignment(11, 2)};
    const std::vector<CriticalItem> items = selectCriticalItems(stats, assignments, 75, 5);
    TEST_ASSERT_EQUAL(1, items.size());
    TEST_ASSERT_EQUAL_UINT32(11, items[0].subjectId);
}

static void test_critical_excludes_hidden_and_burned() {
    const std::vector<ReviewStatistic> stats = {
        criticalStat(1, 10, 50, 10, true), criticalStat(2, 11, 50, 10),
        criticalStat(3, 12, 50, 10)};
    const std::vector<Assignment> assignments = {criticalAssignment(11, 2, true),
                                                  criticalAssignment(12, 9)};
    TEST_ASSERT_TRUE(selectCriticalItems(stats, assignments, 75, 5).empty());
}

static void test_critical_custom_thresholds() {
    const std::vector<ReviewStatistic> stats = {criticalStat(1, 10, 79, 3),
                                                criticalStat(2, 11, 80, 3)};
    const std::vector<Assignment> assignments = {criticalAssignment(10, 2),
                                                  criticalAssignment(11, 2)};
    const std::vector<CriticalItem> items = selectCriticalItems(stats, assignments, 80, 3);
    TEST_ASSERT_EQUAL(1, items.size());
    TEST_ASSERT_EQUAL_UINT32(10, items[0].subjectId);
}

static void test_critical_missing_assignment_skipped() {
    const std::vector<ReviewStatistic> stats = {criticalStat(1, 10, 50, 10)};
    TEST_ASSERT_TRUE(selectCriticalItems(stats, {}, 75, 5).empty());
}

static void test_critical_sorted_worst_first() {
    const std::vector<ReviewStatistic> stats = {
        criticalStat(1, 10, 60, 10), criticalStat(2, 11, 20, 10),
        criticalStat(3, 12, 40, 10)};
    const std::vector<Assignment> assignments = {
        criticalAssignment(10, 2), criticalAssignment(11, 2), criticalAssignment(12, 2)};
    const std::vector<CriticalItem> items = selectCriticalItems(stats, assignments, 75, 5);
    TEST_ASSERT_EQUAL(3, items.size());
    TEST_ASSERT_EQUAL_UINT32(11, items[0].subjectId);
    TEST_ASSERT_EQUAL_UINT32(12, items[1].subjectId);
    TEST_ASSERT_EQUAL_UINT32(10, items[2].subjectId);
}

static void test_critical_list_round_trip() {
    const std::vector<CriticalItem> items = {{10, 42}, {11, 17}};
    std::vector<uint8_t> bytes;
    TEST_ASSERT_TRUE(encodeCriticalList(items, 123, 456, &bytes));
    std::vector<CriticalItem> out;
    TEST_ASSERT_TRUE(decodeCriticalList(bytes.data(), bytes.size(), 123, &out));
    TEST_ASSERT_EQUAL(2, out.size());
    TEST_ASSERT_EQUAL_UINT32(10, out[0].subjectId);
    TEST_ASSERT_EQUAL(42, out[0].percentageCorrect);
    TEST_ASSERT_FALSE(decodeCriticalList(bytes.data(), bytes.size(), 124, &out));
}

static Assignment practiceAssignment(uint32_t subjectId, uint8_t stage, uint32_t startedAt,
                                     SubjectType type = SubjectType::Kanji,
                                     uint16_t position = 1) {
    Assignment a;
    a.id = 1000 + subjectId;
    a.subjectId = subjectId;
    a.srsStage = stage;
    a.startedAt = startedAt;
    a.type = type;
    a.lessonPosition = position;
    a.level = 5;
    return a;
}

static void test_practice_recent_lessons_stage_or_window() {
    const uint32_t now = 1000000;
    const std::vector<Assignment> assignments = {
        practiceAssignment(1, 1, now - 60), practiceAssignment(2, 5, now - 60),
        practiceAssignment(3, 1, now - 10 * 86400)};
    const std::vector<Assignment> apprentice =
        selectRecentLessons(assignments, 0, now, 10);
    TEST_ASSERT_EQUAL(2, apprentice.size());
    TEST_ASSERT_EQUAL_UINT32(1, apprentice[0].subjectId);
    TEST_ASSERT_EQUAL_UINT32(3, apprentice[1].subjectId);
    const std::vector<Assignment> day = selectRecentLessons(assignments, 1, now, 10);
    TEST_ASSERT_EQUAL(1, day.size());
    TEST_ASSERT_EQUAL_UINT32(1, day[0].subjectId);
}

static void test_practice_current_level_kanji_below_guru() {
    const std::vector<Assignment> assignments = {
        practiceAssignment(1, 1, 0, SubjectType::Kanji, 2),
        practiceAssignment(2, 1, 0, SubjectType::Vocabulary, 1),
        practiceAssignment(3, 5, 0, SubjectType::Kanji, 1),
        practiceAssignment(4, 1, 0, SubjectType::Kanji, 1)};
    const std::vector<Assignment> items = selectCurrentLevel(assignments, 5, 10);
    TEST_ASSERT_EQUAL(2, items.size());
    TEST_ASSERT_EQUAL_UINT32(4, items[0].subjectId);
    TEST_ASSERT_EQUAL_UINT32(1, items[1].subjectId);
}

static void test_practice_limit_and_empty() {
    const std::vector<Assignment> assignments = {practiceAssignment(1, 1, 0),
                                                  practiceAssignment(2, 1, 0)};
    TEST_ASSERT_EQUAL(1, selectRecentLessons(assignments, 0, 100, 1).size());
    TEST_ASSERT_TRUE(selectRecentLessons({}, 0, 100, 10).empty());
    TEST_ASSERT_EQUAL(1, selectCurrentLevel(assignments, 5, 1).size());
}

static void test_practice_pool_round_trip() {
    Assignment a = practiceAssignment(10, 3, 12345, SubjectType::Vocabulary, 7);
    a.availableAt = 999;
    a.hidden = true;
    a.level = 9;
    std::vector<Assignment> items = {a};
    std::vector<uint8_t> bytes;
    TEST_ASSERT_TRUE(encodePracticePool(items, 55, 66, &bytes));
    std::vector<Assignment> out;
    TEST_ASSERT_TRUE(decodePracticePool(bytes.data(), bytes.size(), 55, &out));
    TEST_ASSERT_EQUAL(1, out.size());
    TEST_ASSERT_EQUAL_UINT32(10, out[0].subjectId);
    TEST_ASSERT_EQUAL(3, out[0].srsStage);
    TEST_ASSERT_EQUAL_UINT32(12345, out[0].startedAt);
    TEST_ASSERT_EQUAL(7, out[0].lessonPosition);
    TEST_ASSERT_EQUAL(9, out[0].level);
    TEST_ASSERT_TRUE(out[0].hidden);
    TEST_ASSERT_FALSE(decodePracticePool(bytes.data(), bytes.size(), 56, &out));
}


// ---------------------------------------------------------------- API records
static void test_srs_total_count_and_groups() {
    JsonParser p;
    JsonValue v;
    const char* text =
        "{\"object\":\"collection\",\"pages\":{\"per_page\":500,\"next_url\":null,"
        "\"previous_url\":null},\"total_count\":42,\"data\":[]}";
    TEST_ASSERT_TRUE(p.parse(text, &v));
    uint32_t total = 0;
    TEST_ASSERT_TRUE(parseCollectionTotal(v, &total));
    TEST_ASSERT_EQUAL_UINT32(42, total);

    const SrsCounts counts{42, 18, 9, 4, 2, true};
    TEST_ASSERT_EQUAL_UINT32(75, counts.total());
    TEST_ASSERT_EQUAL_UINT32(42, counts.group(SrsGroup::Apprentice));
    TEST_ASSERT_EQUAL_UINT32(18, counts.group(SrsGroup::Guru));
    TEST_ASSERT_EQUAL_UINT32(9, counts.group(SrsGroup::Master));
    TEST_ASSERT_EQUAL_UINT32(4, counts.group(SrsGroup::Enlightened));
    TEST_ASSERT_EQUAL_UINT32(2, counts.group(SrsGroup::Burned));
    TEST_ASSERT_EQUAL_UINT32(0, counts.group(SrsGroup::Count));

    TEST_ASSERT_EQUAL(SrsGroup::Apprentice, srsGroupForStage(1));
    TEST_ASSERT_EQUAL(SrsGroup::Apprentice, srsGroupForStage(4));
    TEST_ASSERT_EQUAL(SrsGroup::Guru, srsGroupForStage(5));
    TEST_ASSERT_EQUAL(SrsGroup::Guru, srsGroupForStage(6));
    TEST_ASSERT_EQUAL(SrsGroup::Master, srsGroupForStage(7));
    TEST_ASSERT_EQUAL(SrsGroup::Enlightened, srsGroupForStage(8));
    TEST_ASSERT_EQUAL(SrsGroup::Burned, srsGroupForStage(9));
    TEST_ASSERT_EQUAL(SrsGroup::Count, srsGroupForStage(0));
    TEST_ASSERT_EQUAL(SrsGroup::Count, srsGroupForStage(10));

    SrsCounts moved{42, 18, 9, 4, 2, true};
    TEST_ASSERT_TRUE(moved.moveStage(4, 5));
    TEST_ASSERT_EQUAL_UINT32(41, moved.apprentice);
    TEST_ASSERT_EQUAL_UINT32(19, moved.guru);
    TEST_ASSERT_EQUAL_UINT32(75, moved.total());
    TEST_ASSERT_TRUE(moved.moveStage(5, 6));  // same bucket
    TEST_ASSERT_EQUAL_UINT32(41, moved.apprentice);
    TEST_ASSERT_EQUAL_UINT32(19, moved.guru);
    TEST_ASSERT_TRUE(moved.moveStage(8, 9));
    TEST_ASSERT_EQUAL_UINT32(3, moved.enlightened);
    TEST_ASSERT_EQUAL_UINT32(3, moved.burned);
    TEST_ASSERT_FALSE(moved.moveStage(0, 1));
    TEST_ASSERT_FALSE(moved.moveStage(9, 10));
    SrsCounts empty;
    TEST_ASSERT_FALSE(empty.moveStage(1, 2));

    JsonValue missing;
    TEST_ASSERT_TRUE(p.parse("{\"data\":[]}", &missing));
    TEST_ASSERT_FALSE(parseCollectionTotal(missing, &total));
    JsonValue negative;
    TEST_ASSERT_TRUE(p.parse("{\"total_count\":-1}", &negative));
    TEST_ASSERT_FALSE(parseCollectionTotal(negative, &total));
}

static void test_assignment_parse() {
    JsonParser p;
    JsonValue v;
    const char* text =
        "{\"data\":["
        "{\"id\":101,\"object\":\"assignment\",\"data\":{\"subject_id\":10,"
        "\"subject_type\":\"kanji\",\"srs_stage\":3,\"lesson_position\":7,"
        "\"available_at\":\"2026-10-25T14:00:00Z\",\"hidden\":false}},"
        "{\"id\":102,\"object\":\"assignment\",\"data\":{\"subject_id\":11,"
        "\"subject_type\":\"radical\",\"srs_stage\":1,\"lesson_position\":2,"
        "\"available_at\":\"2026-10-25T15:00:00Z\",\"hidden\":true}}]}";
    TEST_ASSERT_TRUE(p.parse(text, &v));
    const std::vector<Assignment> as = parseAssignments(v);
    TEST_ASSERT_EQUAL(2, as.size());
    TEST_ASSERT_EQUAL_UINT32(101, as[0].id);
    TEST_ASSERT_EQUAL_UINT32(10, as[0].subjectId);
    TEST_ASSERT_EQUAL(SubjectType::Kanji, as[0].type);
    TEST_ASSERT_EQUAL(3, as[0].srsStage);
    TEST_ASSERT_EQUAL(7, as[0].lessonPosition);
    TEST_ASSERT_TRUE(as[0].availableAt > 0);
    TEST_ASSERT_TRUE(as[1].hidden);
    TEST_ASSERT_EQUAL(SubjectType::Radical, as[1].type);
}

static void test_subject_parse() {
    JsonParser p;
    JsonValue v;
    const char* text =
        "{\"data\":["
        "{\"id\":10,\"object\":\"kanji\",\"data\":{\"level\":5,\"characters\":\"食\","
        "\"meanings\":[{\"meaning\":\"eat\",\"primary\":true,\"accepted_answer\":true}],"
        "\"readings\":[{\"reading\":\"た.べる\",\"primary\":false,"
        "\"accepted_answer\":true,\"type\":\"kunyomi\"}],"
        "\"auxiliary_meanings\":[{\"meaning\":\"food\",\"type\":\"blacklist\"}]}},"
        "{\"id\":11,\"object\":\"radical\",\"data\":{\"level\":1,\"characters\":null,"
        "\"meanings\":[{\"meaning\":\"ground\",\"primary\":true,\"accepted_answer\":true}]}}]}";
    TEST_ASSERT_TRUE(p.parse(text, &v));
    const std::vector<Subject> ss = parseSubjects(v);
    TEST_ASSERT_EQUAL(2, ss.size());
    TEST_ASSERT_EQUAL(SubjectType::Kanji, ss[0].type);
    TEST_ASSERT_EQUAL_STRING("食", ss[0].characters.c_str());
    TEST_ASSERT_EQUAL(1, ss[0].meanings.size());
    TEST_ASSERT_EQUAL(1, ss[0].readings.size());
    TEST_ASSERT_EQUAL(ReadingType::Kunyomi, ss[0].readings[0].type);
    TEST_ASSERT_EQUAL(1, ss[0].blacklist.size());
    TEST_ASSERT_EQUAL(SubjectType::Radical, ss[1].type);
    TEST_ASSERT_TRUE(ss[1].characters.empty());
}

static void assertAssignmentFieldsEqual(const Assignment& want, const Assignment& got) {
    TEST_ASSERT_EQUAL_UINT32(want.id, got.id);
    TEST_ASSERT_EQUAL_UINT32(want.subjectId, got.subjectId);
    TEST_ASSERT_EQUAL(static_cast<int>(want.type), static_cast<int>(got.type));
    TEST_ASSERT_EQUAL(want.srsStage, got.srsStage);
    TEST_ASSERT_EQUAL(want.lessonPosition, got.lessonPosition);
    TEST_ASSERT_EQUAL_UINT32(want.availableAt, got.availableAt);
    TEST_ASSERT_EQUAL_UINT32(want.dataUpdatedAt, got.dataUpdatedAt);
    TEST_ASSERT_EQUAL(want.hidden, got.hidden);
}

static void assertIdsEqual(const std::vector<uint32_t>& want, const std::vector<uint32_t>& got) {
    TEST_ASSERT_EQUAL(static_cast<int>(want.size()), static_cast<int>(got.size()));
    for (size_t i = 0; i < want.size(); ++i) TEST_ASSERT_EQUAL_UINT32(want[i], got[i]);
}

static void assertSubjectFieldsEqual(const Subject& want, const Subject& got) {
    TEST_ASSERT_EQUAL_UINT32(want.id, got.id);
    TEST_ASSERT_EQUAL(static_cast<int>(want.type), static_cast<int>(got.type));
    TEST_ASSERT_EQUAL(want.level, got.level);
    TEST_ASSERT_EQUAL(want.lessonPosition, got.lessonPosition);
    TEST_ASSERT_EQUAL_STRING(want.characters.c_str(), got.characters.c_str());

    TEST_ASSERT_EQUAL(static_cast<int>(want.meanings.size()),
                      static_cast<int>(got.meanings.size()));
    for (size_t i = 0; i < want.meanings.size(); ++i) {
        TEST_ASSERT_EQUAL_STRING(want.meanings[i].text.c_str(), got.meanings[i].text.c_str());
        TEST_ASSERT_EQUAL(want.meanings[i].primary, got.meanings[i].primary);
        TEST_ASSERT_EQUAL(want.meanings[i].accepted, got.meanings[i].accepted);
    }
    TEST_ASSERT_EQUAL(static_cast<int>(want.readings.size()),
                      static_cast<int>(got.readings.size()));
    for (size_t i = 0; i < want.readings.size(); ++i) {
        TEST_ASSERT_EQUAL_STRING(want.readings[i].text.c_str(), got.readings[i].text.c_str());
        TEST_ASSERT_EQUAL(want.readings[i].primary, got.readings[i].primary);
        TEST_ASSERT_EQUAL(want.readings[i].accepted, got.readings[i].accepted);
        TEST_ASSERT_EQUAL(static_cast<int>(want.readings[i].type),
                          static_cast<int>(got.readings[i].type));
    }

    assertIdsEqual(want.componentSubjectIds, got.componentSubjectIds);
    assertIdsEqual(want.visuallySimilarSubjectIds, got.visuallySimilarSubjectIds);
    assertIdsEqual(want.amalgamationSubjectIds, got.amalgamationSubjectIds);
    TEST_ASSERT_EQUAL_STRING(want.meaningMnemonic.c_str(), got.meaningMnemonic.c_str());
    TEST_ASSERT_EQUAL_STRING(want.readingMnemonic.c_str(), got.readingMnemonic.c_str());
    TEST_ASSERT_EQUAL_STRING(want.meaningHint.c_str(), got.meaningHint.c_str());
    TEST_ASSERT_EQUAL_STRING(want.readingHint.c_str(), got.readingHint.c_str());
    TEST_ASSERT_EQUAL_STRING(want.meaningNote.c_str(), got.meaningNote.c_str());
    TEST_ASSERT_EQUAL_STRING(want.readingNote.c_str(), got.readingNote.c_str());

    TEST_ASSERT_EQUAL(static_cast<int>(want.partsOfSpeech.size()),
                      static_cast<int>(got.partsOfSpeech.size()));
    for (size_t i = 0; i < want.partsOfSpeech.size(); ++i)
        TEST_ASSERT_EQUAL_STRING(want.partsOfSpeech[i].c_str(), got.partsOfSpeech[i].c_str());
    TEST_ASSERT_EQUAL(static_cast<int>(want.contextSentences.size()),
                      static_cast<int>(got.contextSentences.size()));
    for (size_t i = 0; i < want.contextSentences.size(); ++i) {
        TEST_ASSERT_EQUAL_STRING(want.contextSentences[i].en.c_str(),
                                 got.contextSentences[i].en.c_str());
        TEST_ASSERT_EQUAL_STRING(want.contextSentences[i].ja.c_str(),
                                 got.contextSentences[i].ja.c_str());
    }
    TEST_ASSERT_EQUAL(static_cast<int>(want.whitelist.size()),
                      static_cast<int>(got.whitelist.size()));
    for (size_t i = 0; i < want.whitelist.size(); ++i)
        TEST_ASSERT_EQUAL_STRING(want.whitelist[i].c_str(), got.whitelist[i].c_str());
    TEST_ASSERT_EQUAL(static_cast<int>(want.blacklist.size()),
                      static_cast<int>(got.blacklist.size()));
    for (size_t i = 0; i < want.blacklist.size(); ++i)
        TEST_ASSERT_EQUAL_STRING(want.blacklist[i].c_str(), got.blacklist[i].c_str());
    TEST_ASSERT_EQUAL(static_cast<int>(want.meaningSynonyms.size()),
                      static_cast<int>(got.meaningSynonyms.size()));

    TEST_ASSERT_EQUAL_UINT32(want.meaningCorrect, got.meaningCorrect);
    TEST_ASSERT_EQUAL_UINT32(want.meaningIncorrect, got.meaningIncorrect);
    TEST_ASSERT_EQUAL_UINT32(want.readingCorrect, got.readingCorrect);
    TEST_ASSERT_EQUAL_UINT32(want.readingIncorrect, got.readingIncorrect);
    TEST_ASSERT_EQUAL(want.meaningMaxStreak, got.meaningMaxStreak);
    TEST_ASSERT_EQUAL(want.meaningCurrentStreak, got.meaningCurrentStreak);
    TEST_ASSERT_EQUAL(want.readingMaxStreak, got.readingMaxStreak);
    TEST_ASSERT_EQUAL(want.readingCurrentStreak, got.readingCurrentStreak);
    TEST_ASSERT_EQUAL(want.percentageCorrect, got.percentageCorrect);
    TEST_ASSERT_EQUAL(want.hasReviewStats, got.hasReviewStats);
}

static void test_assignment_stream_matches_dom() {
    const char* text = R"json({
        "object":"collection",
        "data":[
            {"id":101,"object":"assignment","data_updated_at":"2026-10-26T01:02:03.456789Z",
             "data":{"subject_id":10,"subject_type":"kanji","srs_stage":3,
                     "lesson_position":7,"available_at":"2026-10-25T14:00:00Z",
                     "hidden":false,"unknown":{"subject_id":99}}},
            {"id":102,"object":"assignment","data_updated_at":"2026-10-26T01:02:04Z",
             "data":{"subject_id":11,"subject_type":"radical","srs_stage":1,
                     "lesson_position":2,"available_at":null,"hidden":true}},
            {"id":103,"object":"assignment",
             "data":{"subject_type":"vocabulary","srs_stage":0,"lesson_position":0,
                     "available_at":"2026-10-25T15:00:00Z","hidden":false}},
            {"id":104,"object":"assignment","data":null}
        ]})json";
    JsonParser p;
    JsonValue v;
    TEST_ASSERT_TRUE(p.parse(text, &v));
    const std::vector<Assignment> dom = parseAssignments(v);
    std::vector<Assignment> stream;
    TEST_ASSERT_TRUE(parseAssignmentsStream(text, &stream));
    TEST_ASSERT_EQUAL(2, dom.size());
    TEST_ASSERT_EQUAL(static_cast<int>(dom.size()), static_cast<int>(stream.size()));
    for (size_t i = 0; i < dom.size(); ++i) assertAssignmentFieldsEqual(dom[i], stream[i]);
    TEST_ASSERT_TRUE(stream[0].dataUpdatedAt > 0);
    TEST_ASSERT_EQUAL_UINT32(0, stream[1].availableAt);
}

static void test_assignment_stream_edges() {
    std::vector<Assignment> out;
    TEST_ASSERT_TRUE(parseAssignmentsStream(R"json({"data":[]})json", &out));
    TEST_ASSERT_TRUE(out.empty());
    TEST_ASSERT_TRUE(parseAssignmentsStream(R"json({"data":null})json", &out));
    TEST_ASSERT_TRUE(out.empty());
    TEST_ASSERT_TRUE(
        parseAssignmentsStream(R"json({"meta":{"data":[{"id":1,"data":{"subject_id":2}}]}})json",
                               &out));
    TEST_ASSERT_TRUE(out.empty());
    TEST_ASSERT_FALSE(parseAssignmentsStream(R"json({"data":[)json", &out));
    TEST_ASSERT_TRUE(out.empty());
    TEST_ASSERT_FALSE(parseAssignmentsStream(R"json({"data":[]})json", nullptr));
}

static const char* kSubjectStreamFixture = R"json({
    "object":"collection",
    "data":[
        {"id":10,"object":"kanji","data":{
            "level":5,"lesson_position":12,"characters":"\u98df",
            "meanings":[
                {"meaning":"eat","primary":true,"accepted_answer":true},
                {"meaning":"food","primary":false,"accepted_answer":false},
                {"meaning":"","primary":false,"accepted_answer":true}],
            "readings":[
                {"reading":"\u305f.べる","primary":false,"accepted_answer":true,
                 "type":"kunyomi"},
                {"reading":"ショク","primary":true,"accepted_answer":true,"type":"onyomi"},
                {"reading":"","primary":false,"accepted_answer":false,"type":"nanori"}],
            "component_subject_ids":[1,2,3],
            "visually_similar_subject_ids":[7,8],
            "amalgamation_subject_ids":[9],
            "auxiliary_meanings":[
                {"meaning":"food","type":"blacklist"},
                {"meaning":"consume","type":"whitelist"},
                {"meaning":"ignored","type":"other"}],
            "meaning_mnemonic":"A <radical>person</radical> eats.",
            "reading_mnemonic":"<reading>shoku</reading>.",
            "meaning_hint":"Think of <kanji>food</kanji>.",
            "reading_hint":"Long o.",
            "meaning_note":"  keep   this  ",
            "reading_note":"read  me",
            "meaning_correct":8,"meaning_incorrect":2,
            "reading_correct":7,"reading_incorrect":1,
            "meaning_max_streak":5,"meaning_current_streak":3,
            "reading_max_streak":4,"reading_current_streak":2,
            "percentage_correct":150,
            "unknown_object":{"meanings":[{"meaning":"ignored"}],"level":99}
        }},
        {"id":11,"object":"radical","data":{
            "level":1,"lesson_position":3,"characters":null,
            "meanings":[{"meaning":"ground","primary":true,"accepted_answer":true}],
            "component_subject_ids":[]
        }},
        {"id":30,"object":"vocabulary","data":{
            "level":7,"lesson_position":44,"characters":"日本",
            "meanings":[{"meaning":"Japan","primary":true,"accepted_answer":true}],
            "readings":[{"reading":"にほん","primary":true,"accepted_answer":true,
                         "type":"kunyomi"}],
            "parts_of_speech":["noun","proper noun","","five","six","seven","eight","nine",
                               "dropped"],
            "context_sentences":[
                {"en":"This is Japan.","ja":"これは日本です。"},
                {"en":"I like Japan.","ja":"日本が好きです。"},
                {"en":"","ja":""},
                {"en":"","ja":"空腹"},
                {"en":"No room","ja":"満員"}],
            "meaning_mnemonic":"Japan is the <kanji>origin</kanji>.",
            "visually_similar_subject_ids":[31,32]
        }},
        {"id":0,"object":"kanji","data":{"level":1}},
        {"id":40,"object":"kana_vocabulary","data":null}
    ]})json";

static void test_subject_audio_parse() {
    const char* text = R"json({"data":[{"id":30,"object":"vocabulary","data":{
        "level":1,"characters":"日本",
        "pronunciation_audios":[
          {"url":"https://example.test/female.mp3","content_type":"audio/mpeg",
           "metadata":{"gender":"female","voice_actor_name":"Kyoko"}},
          {"url":"https://example.test/male.ogg","content_type":"audio/ogg",
           "metadata":{"gender":"male"}}]
    }}]})json";
    JsonParser p;
    JsonValue v;
    TEST_ASSERT_TRUE(p.parse(text, &v));
    const std::vector<Subject> dom = parseSubjects(v, true);
    std::vector<Subject> stream;
    TEST_ASSERT_TRUE(parseSubjectsStream(text, &stream, true));
    TEST_ASSERT_EQUAL(1, dom.size());
    TEST_ASSERT_EQUAL(1, stream.size());
    TEST_ASSERT_EQUAL(2, dom[0].audios.size());
    TEST_ASSERT_EQUAL(2, stream[0].audios.size());
    TEST_ASSERT_EQUAL_STRING("https://example.test/female.mp3",
                             stream[0].audios[0].url.c_str());
    TEST_ASSERT_EQUAL_STRING("audio/mpeg", stream[0].audios[0].contentType.c_str());
    TEST_ASSERT_EQUAL(0, stream[0].audios[0].voice);
    TEST_ASSERT_EQUAL(1, stream[0].audios[1].voice);
}

static void test_subject_stream_matches_dom() {
    JsonParser p;
    JsonValue v;
    TEST_ASSERT_TRUE(p.parse(kSubjectStreamFixture, &v));

    const std::vector<Subject> domDetails = parseSubjects(v, true);
    std::vector<Subject> streamDetails;
    TEST_ASSERT_TRUE(parseSubjectsStream(kSubjectStreamFixture, &streamDetails, true));
    TEST_ASSERT_EQUAL(3, domDetails.size());
    TEST_ASSERT_EQUAL(static_cast<int>(domDetails.size()),
                      static_cast<int>(streamDetails.size()));
    for (size_t i = 0; i < domDetails.size(); ++i)
        assertSubjectFieldsEqual(domDetails[i], streamDetails[i]);

    TEST_ASSERT_EQUAL(12, streamDetails[0].lessonPosition);
    TEST_ASSERT_EQUAL(3, streamDetails[1].lessonPosition);
    TEST_ASSERT_EQUAL(44, streamDetails[2].lessonPosition);
    TEST_ASSERT_EQUAL_STRING("A person eats.", streamDetails[0].meaningMnemonic.c_str());
    TEST_ASSERT_EQUAL_STRING("keep this", streamDetails[0].meaningNote.c_str());
    TEST_ASSERT_EQUAL_STRING("read me", streamDetails[0].readingNote.c_str());
    TEST_ASSERT_EQUAL(100, streamDetails[0].percentageCorrect);
    TEST_ASSERT_TRUE(streamDetails[0].hasReviewStats);
    TEST_ASSERT_TRUE(streamDetails[1].characters.empty());
    TEST_ASSERT_EQUAL(8, streamDetails[2].partsOfSpeech.size());
    TEST_ASSERT_EQUAL(4, streamDetails[2].contextSentences.size());
    TEST_ASSERT_EQUAL_STRING("No room", streamDetails[2].contextSentences[3].en.c_str());

    const std::vector<Subject> domPlain = parseSubjects(v, false);
    std::vector<Subject> streamPlain;
    TEST_ASSERT_TRUE(parseSubjectsStream(kSubjectStreamFixture, &streamPlain, false));
    TEST_ASSERT_EQUAL(static_cast<int>(domPlain.size()),
                      static_cast<int>(streamPlain.size()));
    for (size_t i = 0; i < domPlain.size(); ++i)
        assertSubjectFieldsEqual(domPlain[i], streamPlain[i]);
    TEST_ASSERT_TRUE(streamPlain[0].meaningMnemonic.empty());
    TEST_ASSERT_TRUE(streamPlain[0].meaningNote.empty());
    TEST_ASSERT_TRUE(streamPlain[2].partsOfSpeech.empty());
    TEST_ASSERT_TRUE(streamPlain[2].contextSentences.empty());
    TEST_ASSERT_TRUE(streamPlain[0].hasReviewStats);
}

static void test_subject_stream_edges_and_caps() {
    std::vector<Subject> out;
    TEST_ASSERT_TRUE(parseSubjectsStream(R"json({"data":[]})json", &out, true));
    TEST_ASSERT_TRUE(out.empty());
    TEST_ASSERT_TRUE(parseSubjectsStream(R"json({"data":null})json", &out, true));
    TEST_ASSERT_TRUE(out.empty());
    TEST_ASSERT_TRUE(parseSubjectsStream(R"json({"meta":{"data":[{"id":1}]}})json", &out, true));
    TEST_ASSERT_TRUE(out.empty());

    const char* nulls = R"json({"data":[
        {"id":5,"object":"radical","data":{"level":1,"characters":null,
         "meanings":[],"readings":[],"meaning_mnemonic":null,
         "unknown":{"level":9,"meanings":[{"meaning":"ignored"}]}}}]})json";
    TEST_ASSERT_TRUE(parseSubjectsStream(nulls, &out, true));
    TEST_ASSERT_EQUAL(1, out.size());
    TEST_ASSERT_EQUAL_UINT32(5, out[0].id);
    TEST_ASSERT_TRUE(out[0].characters.empty());
    TEST_ASSERT_TRUE(out[0].meaningMnemonic.empty());
    TEST_ASSERT_TRUE(out[0].meanings.empty());

    std::string similar;
    std::string amalgamation;
    for (uint32_t i = 0; i < 40; ++i) {
        if (i) {
            similar.push_back(',');
            amalgamation.push_back(',');
        }
        similar += std::to_string(1000 + i);
        amalgamation += std::to_string(2000 + i);
    }
    const std::string capped = R"json({"data":[{"id":1,"object":"kanji","data":{
        "level":1,"characters":"\u706b","visually_similar_subject_ids":[)json" + similar +
                              R"json(],"amalgamation_subject_ids":[)json" + amalgamation +
                              R"json(]}}]})json";
    JsonParser p;
    JsonValue v;
    TEST_ASSERT_TRUE(p.parse(capped, &v));
    const std::vector<Subject> dom = parseSubjects(v, true);
    std::vector<Subject> stream;
    TEST_ASSERT_TRUE(parseSubjectsStream(capped, &stream, true));
    TEST_ASSERT_EQUAL(static_cast<int>(dom.size()), static_cast<int>(stream.size()));
    assertSubjectFieldsEqual(dom[0], stream[0]);
    TEST_ASSERT_EQUAL(32, stream[0].visuallySimilarSubjectIds.size());
    TEST_ASSERT_EQUAL_UINT32(1031, stream[0].visuallySimilarSubjectIds[31]);
    TEST_ASSERT_EQUAL(32, stream[0].amalgamationSubjectIds.size());
    TEST_ASSERT_EQUAL_UINT32(2031, stream[0].amalgamationSubjectIds[31]);

    TEST_ASSERT_FALSE(parseSubjectsStream(R"json({"data":[{"id":1)json", &out, true));
    TEST_ASSERT_TRUE(out.empty());
    TEST_ASSERT_FALSE(parseSubjectsStream(R"json({"data":[]})json", nullptr, true));
}


static void test_summary_subject_ids() {
    JsonParser p;
    JsonValue v;
    const char* text =
        "{\"data\":{"
        "\"lessons\":[{\"available_at\":\"2026-10-25T12:00:00Z\",\"subject_ids\":[1,2,3]}],"
        "\"reviews\":["
        "{\"available_at\":\"2026-10-25T12:00:00Z\",\"subject_ids\":[10,11]},"
        "{\"available_at\":\"2026-10-25T13:00:00Z\",\"subject_ids\":[12]}]}}";
    TEST_ASSERT_TRUE(p.parse(text, &v));
    Summary s;
    TEST_ASSERT_TRUE(parseSummary(v, &s));
    uint32_t now = 0;
    TEST_ASSERT_TRUE(parseIso8601("2026-10-25T12:30:00Z", &now));
    const std::vector<uint32_t> due = s.dueSubjectIds(now, 10);
    TEST_ASSERT_EQUAL(2, due.size());
    TEST_ASSERT_EQUAL_UINT32(10, due[0]);
    TEST_ASSERT_EQUAL_UINT32(11, due[1]);
    const std::vector<uint32_t> lessons = s.lessonSubjectIds(10);
    TEST_ASSERT_EQUAL(3, lessons.size());
}


// ---------------------------------------------------------------- outbox
static void test_crc32_known_vector() {
    const char* s = "123456789";
    TEST_ASSERT_EQUAL_UINT32(0xCBF43926u,
                             crc32(reinterpret_cast<const uint8_t*>(s), 9));
}

static void test_outbox_model() {
    std::vector<OutboxItem> box;
    const uint32_t a = outboxAdd(box, 101, 10, 1, 0, 1700000000);
    const uint32_t b = outboxAdd(box, 102, 11, 0, 2, 1700000001);
    TEST_ASSERT_EQUAL_UINT32(1, a);
    TEST_ASSERT_EQUAL_UINT32(2, b);
    TEST_ASSERT_EQUAL(2, box.size());
    TEST_ASSERT_EQUAL(2, outboxPendingCount(box));
    TEST_ASSERT_NOT_NULL(outboxFind(box, 1));
    TEST_ASSERT_NULL(outboxFind(box, 3));
    box[0].state = OutboxState::Rejected;
    box[0].httpCode = 422;
    TEST_ASSERT_EQUAL(1, outboxRejectedCount(box));
    TEST_ASSERT_EQUAL(1, outboxPendingCount(box));
    TEST_ASSERT_TRUE(outboxRemove(box, 1));
    TEST_ASSERT_FALSE(outboxRemove(box, 1));
    TEST_ASSERT_EQUAL(1, box.size());
    TEST_ASSERT_EQUAL_UINT32(2, box[0].seq);
    TEST_ASSERT_EQUAL_UINT16(0, box[0].httpCode);
    TEST_ASSERT_EQUAL(1, outboxPendingCount(box));
    TEST_ASSERT_EQUAL_UINT32(3, outboxAdd(box, 103, 12, 0, 0, 0));
}

static void test_outbox_http_code_default() {
    OutboxItem item;
    TEST_ASSERT_EQUAL_UINT16(0, item.httpCode);
    std::vector<OutboxItem> box;
    const uint32_t seq = outboxAdd(box, 201, 20, 0, 0, 0);
    TEST_ASSERT_EQUAL_UINT32(1, seq);
    TEST_ASSERT_EQUAL_UINT16(0, box[0].httpCode);
    box[0].state = OutboxState::Rejected;
    box[0].httpCode = 409;
    TEST_ASSERT_EQUAL_UINT16(409, box[0].httpCode);
}

static void test_outbox_attempt_cap() {
    TEST_ASSERT_FALSE(outboxShouldGiveUp(0));
    TEST_ASSERT_FALSE(outboxShouldGiveUp(49));
    TEST_ASSERT_TRUE(outboxShouldGiveUp(50));
    TEST_ASSERT_TRUE(outboxShouldGiveUp(51));
    TEST_ASSERT_TRUE(outboxShouldGiveUp(255));
}

static void test_outbox_capacity_and_next_seq() {
    std::vector<OutboxItem> empty;
    TEST_ASSERT_EQUAL_UINT32(0, outboxAdd(empty, 0, 1, 0, 0, 0));
    TEST_ASSERT_EQUAL(0, empty.size());

    std::vector<OutboxItem> box;
    for (size_t i = 0; i < kOutboxMax; ++i)
        TEST_ASSERT_EQUAL_UINT32(i + 1, outboxAdd(box, i + 1, 1, 0, 0, 0));
    TEST_ASSERT_EQUAL(kOutboxMax, box.size());
    TEST_ASSERT_EQUAL_UINT32(0, outboxAdd(box, 999, 1, 0, 0, 0));
    TEST_ASSERT_EQUAL(kOutboxMax, box.size());
    TEST_ASSERT_TRUE(outboxRemove(box, 1));
    TEST_ASSERT_EQUAL_UINT32(kOutboxMax + 1, outboxAdd(box, 1000, 1, 0, 0, 0));
    TEST_ASSERT_EQUAL(kOutboxMax, box.size());
}

static void test_outbox_stuck_counts() {
    std::vector<OutboxItem> box;
    TEST_ASSERT_EQUAL_UINT32(1, outboxAdd(box, 301, 30, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(2, outboxAdd(box, 302, 31, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(3, outboxAdd(box, 303, 32, 0, 0, 0));
    box[1].state = OutboxState::Stuck;
    box[2].state = OutboxState::Rejected;
    TEST_ASSERT_EQUAL(1, outboxPendingCount(box));
    TEST_ASSERT_EQUAL(1, outboxStuckCount(box));
    TEST_ASSERT_EQUAL(1, outboxRejectedCount(box));
    TEST_ASSERT_EQUAL(2, outboxAttentionCount(box));
    box[0].state = OutboxState::Ambiguous;
    TEST_ASSERT_EQUAL(1, outboxPendingCount(box));
    TEST_ASSERT_EQUAL(2, outboxAttentionCount(box));
}

static void test_outbox_stuck_remove() {
    std::vector<OutboxItem> box;
    TEST_ASSERT_EQUAL_UINT32(1, outboxAdd(box, 401, 40, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(2, outboxAdd(box, 402, 41, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(3, outboxAdd(box, 403, 42, 0, 0, 0));
    box[1].state = OutboxState::Stuck;
    box[1].attempts = kOutboxMaxAttempts;
    TEST_ASSERT_TRUE(outboxRemove(box, 2));
    TEST_ASSERT_EQUAL(2, box.size());
    TEST_ASSERT_NOT_NULL(outboxFind(box, 1));
    TEST_ASSERT_NULL(outboxFind(box, 2));
    TEST_ASSERT_NOT_NULL(outboxFind(box, 3));
    TEST_ASSERT_EQUAL(2, outboxPendingCount(box));
    TEST_ASSERT_EQUAL(0, outboxStuckCount(box));
    TEST_ASSERT_EQUAL(0, outboxAttentionCount(box));
}

// ---------------------------------------------------------------- SPEED-01
static void test_submit_policy_values() {
    TEST_ASSERT_EQUAL(0, static_cast<int>(SubmitPolicy::AtSessionEnd));
    TEST_ASSERT_EQUAL(5, static_cast<int>(SubmitPolicy::EveryFive));
    TEST_ASSERT_EQUAL(1, static_cast<int>(SubmitPolicy::Immediately));

    TEST_ASSERT_TRUE(submitPolicyFromValue(0) == SubmitPolicy::AtSessionEnd);
    TEST_ASSERT_TRUE(submitPolicyFromValue(5) == SubmitPolicy::EveryFive);
    TEST_ASSERT_TRUE(submitPolicyFromValue(1) == SubmitPolicy::Immediately);
    // An invalid stored value shows the default.
    TEST_ASSERT_TRUE(submitPolicyFromValue(-1) == SubmitPolicy::AtSessionEnd);
    TEST_ASSERT_TRUE(submitPolicyFromValue(3) == SubmitPolicy::AtSessionEnd);
    TEST_ASSERT_TRUE(submitPolicyFromValue(99) == SubmitPolicy::AtSessionEnd);
}

static void test_submit_policy_every_five_counts() {
    const SubmitPolicy every = SubmitPolicy::EveryFive;
    TEST_ASSERT_FALSE(submitAfterItem(every, 0));
    for (uint32_t count = 1; count < 5; ++count)
        TEST_ASSERT_FALSE(submitAfterItem(every, count));
    TEST_ASSERT_TRUE(submitAfterItem(every, 5));
    for (uint32_t count = 6; count < 10; ++count)
        TEST_ASSERT_FALSE(submitAfterItem(every, count));
    TEST_ASSERT_TRUE(submitAfterItem(every, 10));
    TEST_ASSERT_TRUE(submitAfterItem(every, 15));
    TEST_ASSERT_TRUE(submitAfterItem(every, 20));
    TEST_ASSERT_FALSE(submitAfterItem(every, 21));

    TEST_ASSERT_FALSE(submitAfterItem(SubmitPolicy::AtSessionEnd, 5));
    TEST_ASSERT_FALSE(submitAfterItem(SubmitPolicy::AtSessionEnd, 50));
    TEST_ASSERT_FALSE(submitAfterItem(SubmitPolicy::Immediately, 0));
    TEST_ASSERT_TRUE(submitAfterItem(SubmitPolicy::Immediately, 1));
    TEST_ASSERT_TRUE(submitAfterItem(SubmitPolicy::Immediately, 7));
}

static void test_submit_policy_session_end() {
    TEST_ASSERT_TRUE(submitAtSessionEnd(SubmitPolicy::AtSessionEnd));
    // The tail of an Every 5 session is sent when the summary opens too.
    TEST_ASSERT_TRUE(submitAtSessionEnd(SubmitPolicy::EveryFive));
    TEST_ASSERT_FALSE(submitAtSessionEnd(SubmitPolicy::Immediately));
}

static void test_stage_tracker_per_assignment() {
    StageTracker tracker;
    tracker.record(11, 1001, 3, 701);
    tracker.record(12, 1002, 5, 702);
    TEST_ASSERT_EQUAL(2, tracker.size());
    TEST_ASSERT_EQUAL_UINT32(11, tracker.seqAt(0));
    TEST_ASSERT_EQUAL_UINT32(1001, tracker.assignmentAt(0));
    TEST_ASSERT_EQUAL_UINT32(12, tracker.seqAt(1));
    TEST_ASSERT_EQUAL_UINT32(1002, tracker.assignmentAt(1));
    TEST_ASSERT_EQUAL(3, tracker.fromStage(1001));
    TEST_ASSERT_EQUAL(5, tracker.fromStage(1002));
    TEST_ASSERT_EQUAL_UINT32(701, tracker.tagFor(1001));
    TEST_ASSERT_EQUAL_UINT32(702, tracker.tagFor(1002));
    TEST_ASSERT_EQUAL_UINT32(0, tracker.tagFor(9999));

    // Re-queuing the same assignment updates its stage/tag and keeps one slot.
    tracker.record(13, 1001, 4, 703);
    TEST_ASSERT_EQUAL(2, tracker.size());
    TEST_ASSERT_EQUAL(4, tracker.fromStage(1001));
    TEST_ASSERT_EQUAL_UINT32(703, tracker.tagFor(1001));
    TEST_ASSERT_EQUAL_UINT32(13, tracker.seqAt(0));

    TEST_ASSERT_TRUE(tracker.forget(1001));
    TEST_ASSERT_EQUAL(0, tracker.fromStage(1001));
    TEST_ASSERT_EQUAL_UINT32(0, tracker.tagFor(1001));
    TEST_ASSERT_EQUAL(1, tracker.size());
    TEST_ASSERT_EQUAL(5, tracker.fromStage(1002));
    TEST_ASSERT_FALSE(tracker.forget(1001));
    TEST_ASSERT_TRUE(tracker.forget(1002));
    TEST_ASSERT_EQUAL(0, tracker.size());
}

static void test_stage_tracker_unknown_is_zero() {
    StageTracker tracker;
    TEST_ASSERT_EQUAL(0, tracker.fromStage(1));
    tracker.record(1, 10, 7);
    TEST_ASSERT_EQUAL(0, tracker.fromStage(11));
    TEST_ASSERT_EQUAL_UINT32(0, tracker.seqAt(1));
    TEST_ASSERT_EQUAL_UINT32(0, tracker.assignmentAt(1));
    tracker.clear();
    TEST_ASSERT_EQUAL(0, tracker.fromStage(10));
    TEST_ASSERT_EQUAL(0, tracker.size());
}

static void test_stage_tracker_bounded() {
    StageTracker tracker;
    for (size_t i = 0; i < StageTracker::kCapacity + 10; ++i)
        tracker.record(static_cast<uint32_t>(i + 1),
                       static_cast<uint32_t>(1000 + i), static_cast<uint8_t>(i % 9));
    TEST_ASSERT_EQUAL(StageTracker::kCapacity, tracker.size());
    TEST_ASSERT_EQUAL(kOutboxMax, StageTracker::kCapacity);
    // The newest record is kept and the oldest ones were evicted.
    const uint32_t newest = 1000 + static_cast<uint32_t>(StageTracker::kCapacity + 9);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>((StageTracker::kCapacity + 9) % 9),
                      tracker.fromStage(newest));
    TEST_ASSERT_EQUAL(0, tracker.fromStage(1000));
    tracker.clear();
    TEST_ASSERT_EQUAL(0, tracker.size());
}

// ---------------------------------------------------------------- http retry
static void test_http_retry_policy() {
    struct RetryCase {
        int code;
        bool truncated;
        bool parsed;
        bool retry;
    };
    const RetryCase cases[] = {
        {200, false, true, false},   // complete, parsed body
        {200, true, true, true},     // truncated body, even if it parsed
        {200, false, false, true},   // parse failure
        {0, false, false, true},     // transport error
        {500, false, false, true},   // server error
        {503, false, false, true},
        {401, false, false, false},  // auth/rate-limit/refused 4xx are final
        {403, false, false, false},
        {404, false, false, false},
        {409, false, false, false},
        {422, false, false, false},
        {429, false, false, false},
        {301, false, false, false},  // redirects are not followed
        {204, false, false, false},  // no body to parse, not a retryable 200
    };
    for (const RetryCase& c : cases) {
        if (c.retry)
            TEST_ASSERT_TRUE(httpRetryable(c.code, c.truncated, c.parsed));
        else
            TEST_ASSERT_FALSE(httpRetryable(c.code, c.truncated, c.parsed));
    }
}


static void test_markup_and_subject_details() {
    TEST_ASSERT_EQUAL_STRING("A ground B", stripMarkup("A <radical>ground</radical> B").c_str());
    JsonParser p;
    JsonValue v;
    const char* text =
        "{\"data\":[{\"id\":10,\"object\":\"kanji\",\"data\":{\"level\":5,"
        "\"characters\":\"休\",\"meanings\":[{\"meaning\":\"rest\",\"primary\":true,"
        "\"accepted_answer\":true}],\"component_subject_ids\":[1,2],"
        "\"visually_similar_subject_ids\":[7,8],\"amalgamation_subject_ids\":[9],"
        "\"meaning_mnemonic\":\"A <radical>person</radical> by a <kanji>tree</kanji>.\","
        "\"reading_mnemonic\":\"<reading>kyuu</reading>.\","
        "\"meaning_hint\":\"A hint.\",\"reading_hint\":\"Read it.\"}}]}";
    TEST_ASSERT_TRUE(p.parse(text, &v));
    const std::vector<Subject> ss = parseSubjects(v, true);
    TEST_ASSERT_EQUAL(1, ss.size());
    TEST_ASSERT_EQUAL(2, ss[0].componentSubjectIds.size());
    TEST_ASSERT_EQUAL(2, ss[0].visuallySimilarSubjectIds.size());
    TEST_ASSERT_EQUAL_UINT32(7, ss[0].visuallySimilarSubjectIds[0]);
    TEST_ASSERT_EQUAL(1, ss[0].amalgamationSubjectIds.size());
    TEST_ASSERT_EQUAL_UINT32(9, ss[0].amalgamationSubjectIds[0]);
    TEST_ASSERT_EQUAL_STRING("A person by a tree.", ss[0].meaningMnemonic.c_str());
    TEST_ASSERT_EQUAL_STRING("kyuu.", ss[0].readingMnemonic.c_str());
    TEST_ASSERT_EQUAL_STRING("A hint.", ss[0].meaningHint.c_str());
    TEST_ASSERT_EQUAL_STRING("Read it.", ss[0].readingHint.c_str());
}

static void test_level_progressions_parse() {
    const char* text =
        "{\"data\":["
        "{\"id\":1,\"object\":\"level_progression\",\"data\":{\"level\":7,"
        "\"unlocked_at\":\"2026-10-01T12:00:00Z\",\"started_at\":\"2026-10-02T12:00:00Z\","
        "\"passed_at\":\"2026-10-10T12:00:00Z\",\"completed_at\":\"2026-10-12T12:00:00Z\"}},"
        "{\"id\":2,\"object\":\"level_progression\",\"data\":{\"level\":8,"
        "\"unlocked_at\":\"2026-10-12T12:00:00Z\",\"started_at\":null,"
        "\"passed_at\":null,\"completed_at\":null}},"
        "{\"id\":3,\"object\":\"level_progression\",\"data\":{\"level\":9,"
        "\"unlocked_at\":null,\"started_at\":null,\"passed_at\":null,"
        "\"completed_at\":null}}]}";
    LevelProgress progress;
    TEST_ASSERT_TRUE(parseLevelProgressionsText(text, 8, &progress));
    TEST_ASSERT_EQUAL(8, progress.level);
    uint32_t unlocked = 0;
    TEST_ASSERT_TRUE(parseIso8601("2026-10-12T12:00:00Z", &unlocked));
    TEST_ASSERT_EQUAL_UINT32(unlocked, progress.unlockedAt);
    TEST_ASSERT_EQUAL_UINT32(0, progress.startedAt);
    TEST_ASSERT_EQUAL_UINT32(0, progress.passedAt);

    LevelProgress highest;
    TEST_ASSERT_TRUE(parseLevelProgressionsText(text, 20, &highest));
    TEST_ASSERT_EQUAL(9, highest.level);
}

static void test_level_kanji_percent() {
    TEST_ASSERT_EQUAL(0, levelKanjiPercent(0, 0));
    TEST_ASSERT_EQUAL(0, levelKanjiPercent(0, 40));
    TEST_ASSERT_EQUAL(43, levelKanjiPercent(17, 40));
    TEST_ASSERT_EQUAL(33, levelKanjiPercent(1, 3));
    TEST_ASSERT_EQUAL(67, levelKanjiPercent(2, 3));
    TEST_ASSERT_EQUAL(100, levelKanjiPercent(40, 40));
    TEST_ASSERT_EQUAL(100, levelKanjiPercent(41, 40));
}

static void test_review_statistics_parse_and_merge() {
    JsonParser p;
    JsonValue v;
    const char* text =
        "{\"data\":["
        "{\"id\":1,\"object\":\"review_statistic\",\"data\":{\"subject_id\":10,"
        "\"meaning_correct\":8,\"meaning_incorrect\":2,\"reading_correct\":7,"
        "\"reading_incorrect\":1,\"meaning_max_streak\":5,\"meaning_current_streak\":3,"
        "\"reading_max_streak\":4,\"reading_current_streak\":2,"
        "\"percentage_correct\":83,\"hidden\":false}},"
        "{\"id\":2,\"object\":\"review_statistic\",\"data\":{\"subject_id\":11,"
        "\"meaning_correct\":1,\"percentage_correct\":50,\"hidden\":true}}]}";
    TEST_ASSERT_TRUE(p.parse(text, &v));
    const std::vector<ReviewStatistic> stats = parseReviewStatistics(v);
    TEST_ASSERT_EQUAL(2, stats.size());
    TEST_ASSERT_EQUAL_UINT32(10, stats[0].subjectId);
    TEST_ASSERT_EQUAL_UINT32(8, stats[0].meaningCorrect);
    TEST_ASSERT_EQUAL_UINT32(2, stats[0].meaningIncorrect);
    TEST_ASSERT_EQUAL_UINT32(7, stats[0].readingCorrect);
    TEST_ASSERT_EQUAL_UINT32(1, stats[0].readingIncorrect);
    TEST_ASSERT_EQUAL(5, stats[0].meaningMaxStreak);
    TEST_ASSERT_EQUAL(3, stats[0].meaningCurrentStreak);
    TEST_ASSERT_EQUAL(4, stats[0].readingMaxStreak);
    TEST_ASSERT_EQUAL(2, stats[0].readingCurrentStreak);
    TEST_ASSERT_EQUAL(83, stats[0].percentageCorrect);
    TEST_ASSERT_TRUE(stats[1].hidden);

    Subject a;
    a.id = 10;
    Subject b;
    b.id = 11;
    std::vector<Subject> subjects = {a, b};
    TEST_ASSERT_EQUAL(1, applyReviewStatistics(stats, &subjects));
    TEST_ASSERT_TRUE(subjects[0].hasReviewStats);
    TEST_ASSERT_EQUAL_UINT32(8, subjects[0].meaningCorrect);
    TEST_ASSERT_EQUAL_UINT32(18, subjects[0].reviewCount());
    TEST_ASSERT_EQUAL(83, subjects[0].percentageCorrect);
    TEST_ASSERT_FALSE(subjects[1].hasReviewStats);
}

static void test_vocabulary_detail_fields() {
    JsonParser p;
    JsonValue v;
    const char* text =
        "{\"data\":[{\"id\":30,\"object\":\"vocabulary\",\"data\":{\"level\":7,"
        "\"characters\":\"日本\",\"meanings\":[{\"meaning\":\"Japan\",\"primary\":true,"
        "\"accepted_answer\":true}],\"readings\":[{\"reading\":\"にほん\",\"primary\":true,"
        "\"accepted_answer\":true}],\"parts_of_speech\":[\"noun\",\"proper noun\"],"
        "\"context_sentences\":[{\"en\":\"This is Japan.\",\"ja\":\"これは日本です。\"},"
        "{\"en\":\"I like Japan.\",\"ja\":\"日本が好きです。\"}]}}]}";
    TEST_ASSERT_TRUE(p.parse(text, &v));
    const std::vector<Subject> subjects = parseSubjects(v, true);
    TEST_ASSERT_EQUAL(1, subjects.size());
    TEST_ASSERT_EQUAL(2, subjects[0].partsOfSpeech.size());
    TEST_ASSERT_EQUAL_STRING("noun", subjects[0].partsOfSpeech[0].c_str());
    TEST_ASSERT_EQUAL(2, subjects[0].contextSentences.size());
    TEST_ASSERT_EQUAL_STRING("This is Japan.", subjects[0].contextSentences[0].en.c_str());
    TEST_ASSERT_EQUAL_STRING("これは日本です。", subjects[0].contextSentences[0].ja.c_str());

    const std::vector<Subject> plain = parseSubjects(v, false);
    TEST_ASSERT_EQUAL(0, plain[0].partsOfSpeech.size());
    TEST_ASSERT_EQUAL(0, plain[0].contextSentences.size());
}

static void test_study_materials_parse_and_merge() {
    JsonParser p;
    JsonValue v;
    const char* text =
        "{\"data\":["
        "{\"id\":900,\"object\":\"study_material\",\"data\":{\"subject_id\":10,"
        "\"meaning_note\":\"  Use this\\nfor eating.  \",\"reading_note\":null,"
        "\"meaning_synonyms\":[\"consume\",\"ingest\"],\"hidden\":false}},"
        "{\"id\":901,\"object\":\"study_material\",\"data\":{\"subject_id\":11,"
        "\"meaning_note\":\"Hidden note\",\"meaning_synonyms\":[\"x\"],\"hidden\":true}}]}";
    TEST_ASSERT_TRUE(p.parse(text, &v));
    const std::vector<StudyMaterial> materials = parseStudyMaterials(v);
    TEST_ASSERT_EQUAL(2, materials.size());
    TEST_ASSERT_EQUAL_UINT32(10, materials[0].subjectId);
    TEST_ASSERT_EQUAL_STRING("Use this for eating.", materials[0].meaningNote.c_str());
    TEST_ASSERT_TRUE(materials[0].readingNote.empty());
    TEST_ASSERT_EQUAL(2, materials[0].meaningSynonyms.size());
    TEST_ASSERT_TRUE(materials[1].hidden);

    Subject a;
    a.id = 10;
    a.type = SubjectType::Kanji;
    a.characters = "食";
    a.meanings = {{"eat", true, true}};
    a.meaningSynonyms = {"consume"};
    Subject b;
    b.id = 11;
    b.type = SubjectType::Kanji;
    std::vector<Subject> subjects = {a, b};
    TEST_ASSERT_EQUAL(1, applyStudyMaterials(materials, &subjects));
    TEST_ASSERT_EQUAL_STRING("Use this for eating.", subjects[0].meaningNote.c_str());
    TEST_ASSERT_EQUAL(2, subjects[0].meaningSynonyms.size());
    TEST_ASSERT_TRUE(subjects[1].meaningNote.empty());
    TEST_ASSERT_TRUE(subjects[1].meaningSynonyms.empty());
    TEST_ASSERT_EQUAL(AnswerStatus::Correct,
                      checkAnswer(subjects[0], AnswerKind::Meaning, "ingest").status);
}


static void test_review_response_parse() {
    JsonParser p;
    JsonValue v;
    const char* text =
        "{\"resources_updated\":{\"assignment\":{\"id\":101,\"object\":\"assignment\","
        "\"data\":{\"srs_stage\":4,\"available_at\":\"2026-10-28T18:00:00Z\"}}}}";
    TEST_ASSERT_TRUE(p.parse(text, &v));
    ReviewUpdate update;
    TEST_ASSERT_TRUE(parseReviewUpdate(v, &update));
    TEST_ASSERT_EQUAL_UINT32(101, update.assignmentId);
    TEST_ASSERT_EQUAL(4, update.srsStage);
    uint32_t available = 0;
    TEST_ASSERT_TRUE(parseIso8601("2026-10-28T18:00:00Z", &available));
    TEST_ASSERT_EQUAL_UINT32(available, update.availableAt);

    JsonValue bad;
    TEST_ASSERT_TRUE(p.parse("{\"resources_updated\":{}}", &bad));
    TEST_ASSERT_FALSE(parseReviewUpdate(bad, &update));
}

static void test_summary_text_parser() {
    const char* text =
        "{\"object\":\"report\",\"url\":\"x\",\"data_updated_at\":\"2026-10-25T12:00:00Z\","
        "\"data\":{\"lessons\":[{\"available_at\":\"2026-10-25T12:00:00Z\","
        "\"subject_ids\":[1,2,3]}],\"next_reviews_at\":\"2026-10-25T13:00:00Z\","
        "\"reviews\":[{\"available_at\":\"2026-10-25T12:00:00Z\","
        "\"subject_ids\":[10,11]},{\"available_at\":\"2026-10-25T13:00:00Z\","
        "\"subject_ids\":[12]}]}}";
    Summary s;
    TEST_ASSERT_TRUE(parseSummaryText(text, &s, 2));
    TEST_ASSERT_EQUAL(3, s.totalReviews());
    TEST_ASSERT_EQUAL(3, s.totalLessons());
    TEST_ASSERT_EQUAL_STRING("2026-10-25T13:00:00Z", s.nextReviewsAt.c_str());
    uint32_t updated = 0;
    TEST_ASSERT_TRUE(parseIso8601("2026-10-25T12:00:00Z", &updated));
    TEST_ASSERT_EQUAL_UINT32(updated, s.dataUpdatedAt);
    TEST_ASSERT_EQUAL(2, s.reviews[0].subjectIds.size());
    TEST_ASSERT_EQUAL(1, s.reviews[1].subjectIds.size());
    uint32_t now = 0;
    TEST_ASSERT_TRUE(parseIso8601("2026-10-25T12:30:00Z", &now));
    TEST_ASSERT_EQUAL(2, s.dueReviews(now));
}

static void test_summary_text_parser_empty() {
    const char* text =
        "{\"object\":\"report\",\"data\":{\"lessons\":[],\"reviews\":[],"
        "\"next_reviews_at\":null}}";
    Summary s;
    TEST_ASSERT_TRUE(parseSummaryText(text, &s));
    TEST_ASSERT_EQUAL(0, s.totalLessons());
    TEST_ASSERT_EQUAL(0, s.totalReviews());
    TEST_ASSERT_TRUE(s.nextReviewsAt.empty());

    const char* invalid[] = {
        "{}",
        "{\"object\":\"report\"}",
        "{\"errors\":[{\"error\":\"unauthorized\"}]}",
        "{\"data\":{}}",
        "{\"data\":{\"lessons\":[]}}",
        "{\"data\":{\"reviews\":[]}}",
        "{\"data\":{\"lessons\":[]}",
    };
    for (const char* json : invalid) {
        TEST_ASSERT_FALSE(parseSummaryText(json, &s));
        TEST_ASSERT_EQUAL(0, s.totalLessons());
        TEST_ASSERT_EQUAL(0, s.totalReviews());
    }
}

static void test_cache_account_key() {
    TEST_ASSERT_EQUAL_UINT32(0x4F9F2CABu, cacheAccountKey("hello"));
}

static Subject cacheSubject() {
    Subject s;
    s.id = 42;
    s.type = SubjectType::Vocabulary;
    s.level = 7;
    s.characters = "大人";
    s.meanings = {{"adult", true, true}, {"grown-up", false, true}};
    s.readings = {{"おとな", true, true, ReadingType::Kunyomi}};
    s.meaningSynonyms = {"grown up"};
    s.whitelist = {"adults"};
    s.blacklist = {"child"};
    s.componentSubjectIds = {1, 2, 3};
    s.visuallySimilarSubjectIds = {4, 5};
    s.amalgamationSubjectIds = {6};
    s.meaningMnemonic = "An adult is a big person.";
    s.readingMnemonic = "Oh, ton of adults.";
    s.meaningHint = "Not a child.";
    s.readingHint = "Long o.";
    s.meaningNote = "Use this when talking about people.";
    s.readingNote = "The second o is long.";
    s.partsOfSpeech = {"noun"};
    s.contextSentences = {{"大人です。", "It is an adult."}};
    s.meaningCorrect = 10;
    s.meaningIncorrect = 2;
    s.readingCorrect = 8;
    s.readingIncorrect = 1;
    s.meaningMaxStreak = 5;
    s.meaningCurrentStreak = 3;
    s.readingMaxStreak = 4;
    s.readingCurrentStreak = 2;
    s.percentageCorrect = 86;
    s.hasReviewStats = true;
    return s;
}

static void test_cache_snapshot_round_trip() {
    CacheSnapshot in;
    in.meta.savedAt = 1760000000u;
    in.meta.accountKey = cacheAccountKey("token");
    in.summary.nextReviewsAt = "2026-10-28T12:00:00Z";
    in.summary.dataUpdatedAt = 1760000123u;
    in.summary.lessons = {{0, 2, {10, 11}}};
    in.summary.reviews = {{1760000000u, 1, {42}}, {1760003600u, 2, {43, 44}}};
    in.user.username = "dabi";
    in.user.level = 12;
    in.user.subscriptionActive = true;
    in.user.maxLevelGranted = 60;
    in.user.lessonsBatchSize = 4;
    in.user.reviewsOrder = ReviewOrder::LowerLevelsFirst;
    in.user.vacationStartedAt = 1759900000u;
    in.levelProgress.level = 8;
    in.levelProgress.unlockedAt = 1759900000u;
    in.levelProgress.startedAt = 1759900100u;
    in.levelProgress.kanjiTotal = 40;
    in.levelProgress.kanjiPassed = 17;
    in.levelProgress.valid = true;
    in.srs = SrsCounts{42, 18, 9, 4, 2, true};
    in.lastFullAt = 1759990000u;
    in.lastQuickAt = 1760000000u;
    in.lastUserAt = 1759980000u;
    Assignment a;
    a.id = 100;
    a.subjectId = 42;
    a.type = SubjectType::Vocabulary;
    a.srsStage = 3;
    a.availableAt = 1759999999u;
    a.dataUpdatedAt = 1759999000u;
    a.hidden = true;
    a.pendingReview = true;
    in.assignments.push_back(a);

    std::vector<uint8_t> bytes;
    TEST_ASSERT_TRUE(encodeCacheSnapshot(in, &bytes));
    TEST_ASSERT_GREATER_OR_EQUAL(kCacheHeaderBytes, bytes.size());
    TEST_ASSERT_EQUAL_UINT8('K', bytes[0]);
    TEST_ASSERT_EQUAL_UINT8('A', bytes[3]);
    TEST_ASSERT_EQUAL_UINT16(kCacheVersion, static_cast<uint16_t>(bytes[4] | (bytes[5] << 8)));

    CacheSnapshot out;
    TEST_ASSERT_TRUE(decodeCacheSnapshot(bytes.data(), bytes.size(), &out));
    TEST_ASSERT_EQUAL_UINT32(in.meta.savedAt, out.meta.savedAt);
    TEST_ASSERT_EQUAL_UINT32(in.meta.accountKey, out.meta.accountKey);
    TEST_ASSERT_EQUAL_STRING(in.summary.nextReviewsAt.c_str(), out.summary.nextReviewsAt.c_str());
    TEST_ASSERT_EQUAL_UINT32(in.summary.dataUpdatedAt, out.summary.dataUpdatedAt);
    TEST_ASSERT_EQUAL(1, out.summary.lessons.size());
    TEST_ASSERT_EQUAL(2, out.summary.reviews.size());
    TEST_ASSERT_EQUAL_UINT32(1760003600u, out.summary.reviews[1].availableAt);
    TEST_ASSERT_EQUAL_STRING("dabi", out.user.username.c_str());
    TEST_ASSERT_EQUAL(12, out.user.level);
    TEST_ASSERT_TRUE(out.user.subscriptionActive);
    TEST_ASSERT_EQUAL(4, out.user.lessonsBatchSize);
    TEST_ASSERT_EQUAL(ReviewOrder::LowerLevelsFirst, out.user.reviewsOrder);
    TEST_ASSERT_EQUAL_UINT32(1759900000u, out.user.vacationStartedAt);
    TEST_ASSERT_EQUAL(8, out.levelProgress.level);
    TEST_ASSERT_EQUAL_UINT32(1759900000u, out.levelProgress.unlockedAt);
    TEST_ASSERT_EQUAL(40, out.levelProgress.kanjiTotal);
    TEST_ASSERT_EQUAL(17, out.levelProgress.kanjiPassed);
    TEST_ASSERT_TRUE(out.levelProgress.valid);
    TEST_ASSERT_TRUE(out.srs.valid);
    TEST_ASSERT_EQUAL_UINT32(42, out.srs.apprentice);
    TEST_ASSERT_EQUAL_UINT32(18, out.srs.guru);
    TEST_ASSERT_EQUAL_UINT32(9, out.srs.master);
    TEST_ASSERT_EQUAL_UINT32(4, out.srs.enlightened);
    TEST_ASSERT_EQUAL_UINT32(2, out.srs.burned);
    TEST_ASSERT_EQUAL_UINT32(75, out.srs.total());
    TEST_ASSERT_EQUAL_UINT32(in.lastFullAt, out.lastFullAt);
    TEST_ASSERT_EQUAL_UINT32(in.lastQuickAt, out.lastQuickAt);
    TEST_ASSERT_EQUAL_UINT32(in.lastUserAt, out.lastUserAt);
    TEST_ASSERT_EQUAL(1, out.assignments.size());
    TEST_ASSERT_EQUAL_UINT32(100, out.assignments[0].id);
    TEST_ASSERT_EQUAL_UINT32(42, out.assignments[0].subjectId);
    TEST_ASSERT_EQUAL(3, out.assignments[0].srsStage);
    TEST_ASSERT_TRUE(out.assignments[0].hidden);
    TEST_ASSERT_TRUE(out.assignments[0].pendingReview);
}

// A v11 snapshot has no last-sync times; all three must come back as savedAt
// so a device that upgrades does not think it never synced.
static void test_cache_v11_snapshot_loads() {
    CacheSnapshot in;
    in.meta.savedAt = 1760000000u;
    in.meta.accountKey = cacheAccountKey("token");
    in.summary.dataUpdatedAt = 1760000123u;
    in.summary.lessons = {{0, 1, {10}}};
    in.user.username = "dabi";
    in.user.level = 7;
    in.levelProgress.level = 7;
    in.levelProgress.valid = true;
    in.srs = SrsCounts{5, 4, 3, 2, 1, true};
    in.lastFullAt = 1759000000u;
    in.lastQuickAt = 1759500000u;
    in.lastUserAt = 1758000000u;

    std::vector<uint8_t> bytes;
    TEST_ASSERT_TRUE(encodeCacheSnapshot(in, &bytes, 11));
    TEST_ASSERT_EQUAL_UINT16(11, static_cast<uint16_t>(bytes[4] | (bytes[5] << 8)));

    CacheSnapshot out;
    TEST_ASSERT_TRUE(decodeCacheSnapshot(bytes.data(), bytes.size(), &out));
    TEST_ASSERT_EQUAL_UINT32(1760000000u, out.meta.savedAt);
    TEST_ASSERT_EQUAL_STRING("dabi", out.user.username.c_str());
    TEST_ASSERT_EQUAL_UINT32(5, out.srs.apprentice);
    TEST_ASSERT_EQUAL_UINT32(out.meta.savedAt, out.lastFullAt);
    TEST_ASSERT_EQUAL_UINT32(out.meta.savedAt, out.lastQuickAt);
    TEST_ASSERT_EQUAL_UINT32(out.meta.savedAt, out.lastUserAt);
}

// ---------------------------------------------------------------- sync plan
static kk::SyncTimes syncTimesAt(uint32_t now) {
    kk::SyncTimes t;
    t.now = now;
    t.lastQuick = now ? now - 600u : 0;
    t.lastFull = now ? now - 600u : 0;
    t.lastUser = now ? now - 600u : 0;
    return t;
}

static void test_sync_plan_manual_is_full() {
    kk::SyncTimes t = syncTimesAt(1760000000u);
    t.lastQuick = t.now - 60u;  // even a sync from a minute ago stays Full
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kk::SyncKind::Full),
                          static_cast<int>(kk::planSync(kk::SyncKind::Full, false, t, 21600)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kk::SyncKind::Skip),
                          static_cast<int>(kk::planSync(kk::SyncKind::Skip, true, t, 21600)));
}

static void test_sync_plan_wake_quick() {
    const kk::SyncTimes t = syncTimesAt(1760000000u);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kk::SyncKind::Quick),
                          static_cast<int>(kk::planSync(kk::SyncKind::Quick, false, t, 21600)));
}

static void test_sync_plan_full_when_due() {
    kk::SyncTimes t = syncTimesAt(1760000000u);
    t.lastFull = t.now - 21600u;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kk::SyncKind::Full),
                          static_cast<int>(kk::planSync(kk::SyncKind::Quick, false, t, 21600)));
    t.lastFull = 0;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kk::SyncKind::Full),
                          static_cast<int>(kk::planSync(kk::SyncKind::Quick, true, t, 21600)));
    t = syncTimesAt(1760000000u);
    t.lastFull = t.now + 60u;  // clock moved backwards
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kk::SyncKind::Full),
                          static_cast<int>(kk::planSync(kk::SyncKind::Quick, false, t, 21600)));
}

static void test_sync_plan_skip_recent() {
    kk::SyncTimes t = syncTimesAt(1760000000u);
    t.lastQuick = t.now - 60u;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kk::SyncKind::Skip),
                          static_cast<int>(kk::planSync(kk::SyncKind::Quick, false, t, 21600)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kk::SyncKind::Skip),
                          static_cast<int>(kk::planSync(kk::SyncKind::Quick, true, t, 21600)));
    t.lastQuick = t.now - kk::kSyncSkipRecentSeconds;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kk::SyncKind::Quick),
                          static_cast<int>(kk::planSync(kk::SyncKind::Quick, false, t, 21600)));
}

static void test_sync_plan_untrusted_clock_is_full() {
    kk::SyncTimes t;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kk::SyncKind::Full),
                          static_cast<int>(kk::planSync(kk::SyncKind::Quick, true, t, 21600)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kk::SyncKind::Quick),
                          static_cast<int>(kk::planSync(kk::SyncKind::Quick, false, t, 21600)));
}

static void test_sync_plan_user_due_cold_and_six_hours() {
    kk::SyncTimes t = syncTimesAt(1760000000u);
    TEST_ASSERT_TRUE(kk::quickNeedsUser(t, true));
    t.lastUser = t.now - kk::kUserRefreshSeconds;
    TEST_ASSERT_TRUE(kk::quickNeedsUser(t, false));
    t.lastUser = t.now - kk::kUserRefreshSeconds + 1u;
    TEST_ASSERT_FALSE(kk::quickNeedsUser(t, false));
    t.lastUser = 0;
    TEST_ASSERT_TRUE(kk::quickNeedsUser(t, false));
    kk::SyncTimes untrusted;
    TEST_ASSERT_TRUE(kk::quickNeedsUser(untrusted, false));
}

// ---------------------------------------------------------------- prefetch
static void test_prefetch_when_empty() {
    TEST_ASSERT_TRUE(kk::shouldPrefetch(5, 0, 20, false));
}

static void test_prefetch_when_smaller() {
    TEST_ASSERT_TRUE(kk::shouldPrefetch(30, 19, 20, false));
}

static void test_prefetch_skips_full_batch() {
    TEST_ASSERT_FALSE(kk::shouldPrefetch(5, 20, 20, false));
    TEST_ASSERT_FALSE(kk::shouldPrefetch(5, 25, 20, false));
    TEST_ASSERT_FALSE(kk::shouldPrefetch(5, 0, 0, false));
}

static void test_prefetch_skips_low_battery() {
    TEST_ASSERT_FALSE(kk::shouldPrefetch(5, 0, 20, true));
}

static void test_prefetch_skips_no_due() {
    TEST_ASSERT_FALSE(kk::shouldPrefetch(0, 0, 20, false));
}

// ---------------------------------------------------------------- sync steps
static void test_sync_steps_quick_count() {
    TEST_ASSERT_EQUAL_UINT8(4, kk::stepCount(kk::SyncKind::Quick, false));
    TEST_ASSERT_EQUAL_UINT8(5, kk::stepCount(kk::SyncKind::Quick, true));
    TEST_ASSERT_EQUAL_UINT8(0, kk::stepCount(kk::SyncKind::Skip, true));
}

static void test_sync_steps_full_count() {
    TEST_ASSERT_EQUAL_UINT8(8, kk::stepCount(kk::SyncKind::Full, false));
    TEST_ASSERT_EQUAL_UINT8(9, kk::stepCount(kk::SyncKind::Full, true));
}

static void test_sync_steps_optional_flags() {
    TEST_ASSERT_TRUE(kk::stepIsOptional(kk::SyncStep::Account));
    TEST_ASSERT_TRUE(kk::stepIsOptional(kk::SyncStep::Level));
    TEST_ASSERT_TRUE(kk::stepIsOptional(kk::SyncStep::Srs));
    TEST_ASSERT_TRUE(kk::stepIsOptional(kk::SyncStep::Critical));
    TEST_ASSERT_TRUE(kk::stepIsOptional(kk::SyncStep::Prefetch));
    TEST_ASSERT_FALSE(kk::stepIsOptional(kk::SyncStep::Wifi));
    TEST_ASSERT_FALSE(kk::stepIsOptional(kk::SyncStep::Time));
    TEST_ASSERT_FALSE(kk::stepIsOptional(kk::SyncStep::Outbox));
    TEST_ASSERT_FALSE(kk::stepIsOptional(kk::SyncStep::Summary));
}

static void test_sync_steps_interrupt_skips_optional() {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kk::SyncStep::Done),
                          static_cast<int>(kk::nextStep(kk::SyncKind::Full, kk::SyncStep::Summary, true)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kk::SyncStep::Done),
                          static_cast<int>(kk::nextStep(kk::SyncKind::Quick, kk::SyncStep::Outbox, true)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kk::SyncStep::Level),
                          static_cast<int>(kk::nextStep(kk::SyncKind::Full, kk::SyncStep::Summary, false)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kk::SyncStep::Done),
                          static_cast<int>(kk::nextStep(kk::SyncKind::Quick, kk::SyncStep::Summary, false)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kk::SyncStep::Prefetch),
                          static_cast<int>(kk::nextStep(kk::SyncKind::Full, kk::SyncStep::Critical, false)));
    TEST_ASSERT_EQUAL_UINT8(4, kk::stepNumber(kk::SyncStep::Summary, kk::SyncKind::Quick, false));
    TEST_ASSERT_EQUAL_UINT8(5, kk::stepNumber(kk::SyncStep::Summary, kk::SyncKind::Quick, true));
    TEST_ASSERT_EQUAL_UINT8(0, kk::stepNumber(kk::SyncStep::Level, kk::SyncKind::Quick, false));
}

static void test_sync_steps_error_is_terminal() {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kk::SyncStep::Failed),
                          static_cast<int>(kk::nextStep(kk::SyncKind::Full, kk::SyncStep::Failed, false)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kk::SyncStep::Done),
                          static_cast<int>(kk::nextStep(kk::SyncKind::Quick, kk::SyncStep::Done, false)));
}

// The Time step must always leave Time: a trusted clock immediately, an
// untrusted one after one bounded 5 s wait.
static void test_sync_time_ready_trusted_clock() {
    TEST_ASSERT_TRUE(kk::syncTimeReady(1760000000u, 0, 5000));
    TEST_ASSERT_TRUE(kk::syncTimeReady(1760000000u, 0, 0));
}

static void test_sync_time_ready_waits_untrusted() {
    TEST_ASSERT_FALSE(kk::syncTimeReady(0, 0, 5000));
    TEST_ASSERT_FALSE(kk::syncTimeReady(0, 4999, 5000));
}

static void test_sync_time_ready_gives_up() {
    TEST_ASSERT_TRUE(kk::syncTimeReady(0, 5000, 5000));
    TEST_ASSERT_TRUE(kk::syncTimeReady(0, 5001, 5000));
    TEST_ASSERT_TRUE(kk::syncTimeReady(0, 0, 0));
}

// A blanked panel must not stop the background sync; the modal screen is only
// driven while it is on.
static void test_sync_runs_while_screen_off_background() {
    TEST_ASSERT_TRUE(kk::syncRunsWhileScreenOff(true, false));
    TEST_ASSERT_FALSE(kk::syncRunsWhileScreenOff(true, true));
    TEST_ASSERT_FALSE(kk::syncRunsWhileScreenOff(false, false));
}

static void test_cache_corruption_is_rejected() {
    CacheSnapshot in;
    in.meta.savedAt = 1234;
    in.meta.accountKey = 99;
    in.summary.nextReviewsAt = "x";
    in.summary.reviews = {{0, 1, {1}}};
    std::vector<uint8_t> bytes;
    TEST_ASSERT_TRUE(encodeCacheSnapshot(in, &bytes));

    CacheSnapshot out;
    std::vector<uint8_t> bad = bytes;
    bad[kCacheHeaderBytes + 2] ^= 0x40;
    TEST_ASSERT_FALSE(decodeCacheSnapshot(bad.data(), bad.size(), &out));
    TEST_ASSERT_FALSE(decodeCacheSnapshot(bytes.data(), bytes.size() - 1, &out));
    TEST_ASSERT_FALSE(decodeCacheSnapshot(bytes.data(), kCacheHeaderBytes - 1, &out));
    bad = bytes;
    bad[4] = static_cast<uint8_t>(kCacheVersion + 1);
    TEST_ASSERT_FALSE(decodeCacheSnapshot(bad.data(), bad.size(), &out));
    bad[4] = static_cast<uint8_t>(kCacheVersion - 1);
    TEST_ASSERT_FALSE(decodeCacheSnapshot(bad.data(), bad.size(), &out));
    TEST_ASSERT_TRUE(decodeCacheSnapshot(bytes.data(), bytes.size(), &out));
}

static void test_cache_subjects_round_trip() {
    SubjectCache in;
    in.meta.savedAt = 1760000000u;
    in.meta.accountKey = 7;
    in.subjects.push_back(cacheSubject());
    Subject radical;
    radical.id = 1;
    radical.type = SubjectType::Radical;
    radical.level = 1;
    radical.meanings = {{"ground", true, true}};
    in.subjects.push_back(radical);

    std::vector<uint8_t> bytes;
    TEST_ASSERT_TRUE(encodeSubjectCache(in, &bytes));
    TEST_ASSERT_EQUAL_UINT8('S', bytes[3]);
    SubjectCache out;
    TEST_ASSERT_TRUE(decodeSubjectCache(bytes.data(), bytes.size(), &out));
    TEST_ASSERT_EQUAL(2, out.subjects.size());
    const Subject& s = out.subjects[0];
    TEST_ASSERT_EQUAL_UINT32(42, s.id);
    TEST_ASSERT_EQUAL(SubjectType::Vocabulary, s.type);
    TEST_ASSERT_EQUAL_STRING("大人", s.characters.c_str());
    TEST_ASSERT_EQUAL(2, s.meanings.size());
    TEST_ASSERT_EQUAL_STRING("adult", s.meanings[0].text.c_str());
    TEST_ASSERT_TRUE(s.meanings[0].primary);
    TEST_ASSERT_EQUAL_STRING("おとな", s.readings[0].text.c_str());
    TEST_ASSERT_EQUAL(ReadingType::Kunyomi, s.readings[0].type);
    TEST_ASSERT_EQUAL_STRING("grown up", s.meaningSynonyms[0].c_str());
    TEST_ASSERT_EQUAL(3, s.componentSubjectIds.size());
    TEST_ASSERT_EQUAL(2, s.visuallySimilarSubjectIds.size());
    TEST_ASSERT_EQUAL(1, s.amalgamationSubjectIds.size());
    TEST_ASSERT_EQUAL_STRING("An adult is a big person.", s.meaningMnemonic.c_str());
    TEST_ASSERT_EQUAL_STRING("Long o.", s.readingHint.c_str());
    TEST_ASSERT_EQUAL_STRING("Use this when talking about people.", s.meaningNote.c_str());
    TEST_ASSERT_EQUAL_STRING("The second o is long.", s.readingNote.c_str());
    TEST_ASSERT_EQUAL(1, s.partsOfSpeech.size());
    TEST_ASSERT_EQUAL_STRING("noun", s.partsOfSpeech[0].c_str());
    TEST_ASSERT_EQUAL(1, s.contextSentences.size());
    TEST_ASSERT_EQUAL_STRING("大人です。", s.contextSentences[0].ja.c_str());
    TEST_ASSERT_EQUAL_STRING("It is an adult.", s.contextSentences[0].en.c_str());
    TEST_ASSERT_EQUAL_UINT32(10, s.meaningCorrect);
    TEST_ASSERT_EQUAL_UINT32(2, s.meaningIncorrect);
    TEST_ASSERT_EQUAL_UINT32(8, s.readingCorrect);
    TEST_ASSERT_EQUAL_UINT32(1, s.readingIncorrect);
    TEST_ASSERT_EQUAL(5, s.meaningMaxStreak);
    TEST_ASSERT_EQUAL(3, s.meaningCurrentStreak);
    TEST_ASSERT_EQUAL(4, s.readingMaxStreak);
    TEST_ASSERT_EQUAL(2, s.readingCurrentStreak);
    TEST_ASSERT_EQUAL(86, s.percentageCorrect);
    TEST_ASSERT_TRUE(s.hasReviewStats);
    TEST_ASSERT_EQUAL(SubjectType::Radical, out.subjects[1].type);
    TEST_ASSERT_TRUE(out.subjects[1].characters.empty());

    out.subjects.clear();
    bytes[bytes.size() - 1] ^= 0x01;
    TEST_ASSERT_FALSE(decodeSubjectCache(bytes.data(), bytes.size(), &out));
}

// ---------------------------------------------------------------- OTA
static const char* kOtaSha =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

static std::string manifestText(const char* version = "v0.2.0", const char* env = "cardputer-adv",
                                uint32_t bytes = 1423021) {
    char line[256];
    snprintf(line, sizeof line,
             "#kanikey-firmware\t1\nversion\t%s\napp\t%s\tkanikey-%s.bin\t%u\t%s\n", version,
             env, env, static_cast<unsigned>(bytes), kOtaSha);
    return line;
}

static void test_ota_manifest_parse() {
    const char* url =
        "https://github.com/dcarnicer/kanikey/releases/download/v0.2.0/kanikey-firmware.txt";
    ManifestParseResult r = parseManifest(manifestText(), url);
    TEST_ASSERT_EQUAL(ManifestError::Ok, r.error);
    TEST_ASSERT_EQUAL_UINT32(1, r.manifest.format);
    TEST_ASSERT_EQUAL_STRING("v0.2.0", r.manifest.version.c_str());
    TEST_ASSERT_EQUAL(1, r.manifest.apps.size());
    const AppImage& app = r.manifest.apps[0];
    TEST_ASSERT_EQUAL_STRING("cardputer-adv", app.env.c_str());
    TEST_ASSERT_EQUAL_UINT32(1423021, app.bytes);
    TEST_ASSERT_EQUAL_STRING(kOtaSha, app.sha256.c_str());
    TEST_ASSERT_EQUAL_STRING(
        "https://github.com/dcarnicer/kanikey/releases/download/v0.2.0/kanikey-cardputer-adv.bin",
        app.url.c_str());
    TEST_ASSERT_TRUE(r.manifest.find("cardputer-adv") == &app);
    TEST_ASSERT_TRUE(r.manifest.find("m5stack") == nullptr);
}

static void test_ota_manifest_edges() {
    ManifestParseResult r = parseManifest("", "https://x/y");
    TEST_ASSERT_EQUAL(ManifestError::Empty, r.error);

    std::string huge(kManifestMaxBytes + 1, 'x');
    r = parseManifest(huge, "https://x/y");
    TEST_ASSERT_EQUAL(ManifestError::TooLarge, r.error);

    r = parseManifest("version\tv0.2.0\n", "https://x/y");
    TEST_ASSERT_EQUAL(ManifestError::BadHeader, r.error);
    TEST_ASSERT_EQUAL_UINT(1, r.line);

    r = parseManifest("#kanikey-firmware\t2\nversion\tv0.2.0\n", "https://x/y");
    TEST_ASSERT_EQUAL(ManifestError::UnsupportedVersion, r.error);

    r = parseManifest("#kanikey-firmware\t1\nversion\t0.2\n", "https://x/y");
    TEST_ASSERT_EQUAL(ManifestError::BadVersion, r.error);

    // A version after the first app record is invalid.
    r = parseManifest(
        "#kanikey-firmware\t1\nversion\tv0.2.0\napp\tcardputer-adv\ta.bin\t10\t" + std::string(kOtaSha) +
            "\nversion\tv0.2.1\n",
        "https://x/y");
    TEST_ASSERT_EQUAL(ManifestError::BadVersion, r.error);

    // Duplicate environments are refused.
    const std::string appLine =
        std::string("app\tcardputer-adv\ta.bin\t10\t") + kOtaSha + "\n";
    r = parseManifest("#kanikey-firmware\t1\nversion\tv0.2.0\n" + appLine + appLine,
                      "https://x/y");
    TEST_ASSERT_EQUAL(ManifestError::DuplicateApp, r.error);

    // A bad hash or zero size rejects the whole manifest.
    r = parseManifest("#kanikey-firmware\t1\nversion\tv0.2.0\n"
                      "app\tcardputer-adv\ta.bin\t10\tnothex\n",
                      "https://x/y");
    TEST_ASSERT_EQUAL(ManifestError::BadApp, r.error);
    r = parseManifest("#kanikey-firmware\t1\nversion\tv0.2.0\napp\tcardputer-adv\ta.bin\t0\t" +
                          std::string(kOtaSha) + "\n",
                      "https://x/y");
    TEST_ASSERT_EQUAL(ManifestError::BadApp, r.error);

    // Unknown records are ignored for forward compatibility.
    r = parseManifest("#kanikey-firmware\t1\nnotes\tfuture\nversion\tv0.2.0\n"
                      "app\tcardputer-adv\ta.bin\t10\t" +
                          std::string(kOtaSha) + "\n",
                      "https://x/y");
    TEST_ASSERT_EQUAL(ManifestError::Ok, r.error);
    TEST_ASSERT_EQUAL_STRING("https://x/a.bin", r.manifest.apps[0].url.c_str());
}

static void test_ota_versions() {
    FirmwareVersion a, b;
    TEST_ASSERT_TRUE(parseFirmwareVersion("0.1.0", &a));
    TEST_ASSERT_TRUE(parseFirmwareVersion("v0.2.0", &b));
    TEST_ASSERT_EQUAL(-1, compareFirmwareVersions(a, b));
    TEST_ASSERT_EQUAL(1, compareFirmwareVersions(b, a));
    TEST_ASSERT_EQUAL(0, compareFirmwareVersions(b, b));
    TEST_ASSERT_TRUE(parseFirmwareVersion("10.20.30", &a));
    TEST_ASSERT_EQUAL_UINT32(10, a.major);
    TEST_ASSERT_EQUAL_UINT32(20, a.minor);
    TEST_ASSERT_EQUAL_UINT32(30, a.patch);
    TEST_ASSERT_FALSE(parseFirmwareVersion("0.1", &a));
    TEST_ASSERT_FALSE(parseFirmwareVersion("0.1.0-dev", &a));
    TEST_ASSERT_FALSE(parseFirmwareVersion("", &a));
    TEST_ASSERT_FALSE(parseFirmwareVersion("1.2.3.4", &a));
    TEST_ASSERT_FALSE(parseFirmwareVersion("v1.2.x", &a));
}

static void test_ota_offer() {
    ManifestParseResult r = parseManifest(manifestText(), "https://x/y/manifest.txt");
    TEST_ASSERT_EQUAL(ManifestError::Ok, r.error);

    OfferInput in;
    in.running = "0.1.0";
    in.env = "cardputer-adv";
    in.slotBytes = 1835008;
    Offer o = offerFor(r.manifest, in);
    TEST_ASSERT_EQUAL(OfferVerdict::Offer, o.verdict);
    TEST_ASSERT_TRUE(o.app == &r.manifest.apps[0]);

    in.running = "0.2.0";
    o = offerFor(r.manifest, in);
    TEST_ASSERT_EQUAL(OfferVerdict::UpToDate, o.verdict);

    in.running = "0.1.0";
    in.env = "m5stack";
    o = offerFor(r.manifest, in);
    TEST_ASSERT_EQUAL(OfferVerdict::NoImage, o.verdict);

    in.env = "cardputer-adv";
    in.slotBytes = 1000;
    o = offerFor(r.manifest, in);
    TEST_ASSERT_EQUAL(OfferVerdict::TooLarge, o.verdict);

    in.slotBytes = 1835008;
    in.rolledBack = "v0.2.0";
    o = offerFor(r.manifest, in);
    TEST_ASSERT_EQUAL(OfferVerdict::RolledBack, o.verdict);

    in.rolledBack = "0.2.0";
    o = offerFor(r.manifest, in);
    TEST_ASSERT_EQUAL(OfferVerdict::RolledBack, o.verdict);

    in.rolledBack = "v0.1.9";
    o = offerFor(r.manifest, in);
    TEST_ASSERT_EQUAL(OfferVerdict::Offer, o.verdict);

    in.rolledBack = "";
    in.running = "dev";
    o = offerFor(r.manifest, in);
    TEST_ASSERT_EQUAL(OfferVerdict::DevBuild, o.verdict);
    in.allowAnyDevVersion = true;
    o = offerFor(r.manifest, in);
    TEST_ASSERT_EQUAL(OfferVerdict::Offer, o.verdict);

    in.running = "0.1.0";
    in.slotBytes = 0;
    o = offerFor(r.manifest, in);
    TEST_ASSERT_EQUAL(OfferVerdict::NoSlot, o.verdict);
}

static void test_ota_url_resolution() {
    TEST_ASSERT_EQUAL_STRING("https://x/y/a.bin",
                             resolveUrl("https://x/y/manifest.txt", "a.bin").c_str());
    TEST_ASSERT_EQUAL_STRING("https://x/a.bin",
                             resolveUrl("https://x/y/manifest.txt", "/a.bin").c_str());
    TEST_ASSERT_EQUAL_STRING("https://cdn/a.bin",
                             resolveUrl("https://x/y/manifest.txt", "https://cdn/a.bin").c_str());
    const ManifestParseResult r = parseManifest(
        "#kanikey-firmware\t1\nversion\tv0.2.0\napp\tcardputer-adv\t../a.bin\t10\t" +
            std::string(kOtaSha) + "\n",
        "https://x/y/manifest.txt");
    TEST_ASSERT_EQUAL(ManifestError::BadApp, r.error);
}

// --------------------------------------------------------------- settings
static int gSettingsLoadCalls = 0;

static int settingsTestReader(void* context, const char* nvsKey, int fallback) {
    (void)context;
    ++gSettingsLoadCalls;
    if (std::strcmp(nvsKey, "bright") == 0) return 192;
    if (std::strcmp(nvsKey, "sleep") == 0) return 9999;  // not a choice
    return fallback;
}

static bool isSettingSection(const char* section) {
    static const char* const kSections[] = {
        "Reviews", "Lessons", "Study", "Sync & data",
        "Display & power", "Reminders & sound", "Account", "System",
    };
    for (const char* name : kSections)
        if (std::strcmp(section, name) == 0) return true;
    return false;
}

static int settingsKeyedCount() {
    size_t count = 0;
    const SettingDef* all = settingsAll(&count);
    int keyed = 0;
    for (size_t i = 0; i < count; ++i)
        if (all[i].kind == SettingKind::Choice || all[i].kind == SettingKind::Toggle) ++keyed;
    return keyed;
}

static void test_settings_registry_ids() {
    size_t count = 0;
    const SettingDef* all = settingsAll(&count);
    TEST_ASSERT_NOT_NULL(all);
    TEST_ASSERT_EQUAL(48, static_cast<int>(count));
    for (size_t i = 0; i < count; ++i) {
        const SettingDef& def = all[i];
        TEST_ASSERT_NOT_NULL(def.id);
        TEST_ASSERT_NOT_NULL(def.section);
        TEST_ASSERT_NOT_NULL(def.label);
        TEST_ASSERT_NOT_NULL(def.help);
        TEST_ASSERT_GREATER_THAN(0, static_cast<int>(std::strlen(def.id)));
        TEST_ASSERT_GREATER_THAN(0, static_cast<int>(std::strlen(def.section)));
        TEST_ASSERT_GREATER_THAN(0, static_cast<int>(std::strlen(def.label)));
        TEST_ASSERT_EQUAL_PTR(&all[i], settingById(def.id));
        const bool keyed = def.kind == SettingKind::Choice || def.kind == SettingKind::Toggle;
        if (keyed) {
            TEST_ASSERT_NOT_NULL(def.nvsKey);
            TEST_ASSERT_GREATER_THAN(0, static_cast<int>(std::strlen(def.nvsKey)));
            TEST_ASSERT_LESS_OR_EQUAL(15, static_cast<int>(std::strlen(def.nvsKey)));
            for (const char* c = def.nvsKey; *c; ++c)
                TEST_ASSERT_TRUE((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') ||
                                 *c == '.' || *c == '_' || *c == '-');
            TEST_ASSERT_NOT_NULL(def.choices);
            TEST_ASSERT_GREATER_OR_EQUAL(2, def.choiceCount);
            TEST_ASSERT_EQUAL_PTR(&all[i], settingByNvsKey(def.nvsKey));
            TEST_ASSERT_NULL(def.valueText);
        } else {
            TEST_ASSERT_NULL(def.nvsKey);
            TEST_ASSERT_NULL(def.choices);
            TEST_ASSERT_EQUAL(0, def.choiceCount);
            TEST_ASSERT_NOT_NULL(def.valueText);
            TEST_ASSERT_GREATER_THAN(0, static_cast<int>(std::strlen(def.valueText)));
        }
        for (size_t j = i + 1; j < count; ++j)
            TEST_ASSERT_TRUE(std::strcmp(def.id, all[j].id) != 0);
    }
    TEST_ASSERT_NULL(settingById("nope"));
    TEST_ASSERT_NULL(settingByNvsKey("nope"));
}

static void test_settings_registry_defaults() {
    struct Expected {
        const char* id;
        const char* nvsKey;
        int defaultValue;
    };
    static const Expected kExpected[] = {
        {"P1", "bright", 128},  {"P2", "dim", 30},      {"P3", "blank", 60},
        {"P4", "sleep", 120},   {"R1", "revsize", 20},   {"R4", "rv.first", 1},
        {"L1", "lessonsz", 5},
    };
    for (const Expected& expected : kExpected) {
        const SettingDef* def = settingById(expected.id);
        TEST_ASSERT_NOT_NULL(def);
        TEST_ASSERT_NOT_NULL(def->nvsKey);
        TEST_ASSERT_EQUAL_STRING(expected.nvsKey, def->nvsKey);
        TEST_ASSERT_EQUAL_INT(expected.defaultValue, def->defaultValue);
        TEST_ASSERT_EQUAL_INT(expected.defaultValue,
                             settingValidate(*def, def->defaultValue));
    }
}

static void test_settings_cycle_wraps() {
    const SettingDef* brightness = settingById("P1");
    TEST_ASSERT_NOT_NULL(brightness);
    TEST_ASSERT_EQUAL_INT(64, settingNextValue(*brightness, 255));
    TEST_ASSERT_EQUAL_INT(192, settingNextValue(*brightness, 128));
    TEST_ASSERT_EQUAL_INT(255, settingPrevValue(*brightness, 64));
    TEST_ASSERT_EQUAL_INT(64, settingPrevValue(*brightness, 128));
    TEST_ASSERT_EQUAL_INT(128, settingNextValue(*brightness, 200));
    TEST_ASSERT_EQUAL_INT(128, settingPrevValue(*brightness, 200));

    const SettingDef* dim = settingById("P2");
    TEST_ASSERT_NOT_NULL(dim);
    TEST_ASSERT_EQUAL_INT(0, settingNextValue(*dim, 60));
    TEST_ASSERT_EQUAL_INT(60, settingPrevValue(*dim, 0));

    const SettingDef* order = settingById("R4");
    TEST_ASSERT_NOT_NULL(order);
    TEST_ASSERT_EQUAL_INT(0, settingNextValue(*order, 1));
    TEST_ASSERT_EQUAL_INT(1, settingPrevValue(*order, 0));
    TEST_ASSERT_EQUAL_INT(1, settingNextValue(*order, 7));
}

static void test_settings_validate() {
    const SettingDef* brightness = settingById("P1");
    const SettingDef* dim = settingById("P2");
    const SettingDef* order = settingById("R4");
    TEST_ASSERT_NOT_NULL(brightness);
    TEST_ASSERT_NOT_NULL(dim);
    TEST_ASSERT_NOT_NULL(order);
    TEST_ASSERT_EQUAL_INT(128, settingValidate(*brightness, 200));
    TEST_ASSERT_EQUAL_INT(192, settingValidate(*brightness, 192));
    TEST_ASSERT_EQUAL_INT(128, settingValidate(*brightness, -1));
    TEST_ASSERT_EQUAL_INT(15, settingValidate(*dim, 15));
    TEST_ASSERT_EQUAL_INT(30, settingValidate(*dim, 45));
    TEST_ASSERT_EQUAL_INT(1, settingValidate(*order, 5));
}

static void test_settings_help_width() {
    size_t count = 0;
    const SettingDef* all = settingsAll(&count);
    TEST_ASSERT_NOT_NULL(all);
    for (size_t i = 0; i < count; ++i) {
        TEST_ASSERT_GREATER_THAN(0, static_cast<int>(std::strlen(all[i].help)));
        TEST_ASSERT_LESS_OR_EQUAL(38, static_cast<int>(std::strlen(all[i].help)));
        if (all[i].choices)
            for (uint8_t c = 0; c < all[i].choiceCount; ++c)
                TEST_ASSERT_GREATER_THAN(0, static_cast<int>(std::strlen(all[i].choices[c].label)));
    }
}

static void test_settings_sections() {
    size_t count = 0;
    const SettingDef* all = settingsAll(&count);
    TEST_ASSERT_NOT_NULL(all);
    for (size_t i = 0; i < count; ++i)
        TEST_ASSERT_TRUE(isSettingSection(all[i].section));

    const SettingDef* out[16];
    const struct {
        const char* name;
        int expected;
    } kSections[] = {
        {"Reviews", 12},          {"Lessons", 6},
        {"Study", 5},             {"Sync & data", 6},
        {"Display & power", 6},   {"Reminders & sound", 7},
        {"Account", 3},           {"System", 3},
    };
    size_t total = 0;
    for (const auto& section : kSections) {
        const size_t found = settingsInSection(section.name, out, 16);
        TEST_ASSERT_EQUAL_INT(section.expected, static_cast<int>(found));
        total += found;
    }
    TEST_ASSERT_EQUAL(48, static_cast<int>(total));
    TEST_ASSERT_EQUAL(2, static_cast<int>(settingsInSection("Display & power", out, 2)));
    TEST_ASSERT_EQUAL_STRING("P1", out[0]->id);
    TEST_ASSERT_EQUAL_STRING("P2", out[1]->id);
}

static void test_settings_cache() {
    gSettingsLoadCalls = 0;
    SettingsCache cache;
    cache.load(settingsTestReader, nullptr);
    TEST_ASSERT_TRUE(cache.loaded());
    TEST_ASSERT_EQUAL(39, static_cast<int>(cache.size()));
    TEST_ASSERT_EQUAL(39, gSettingsLoadCalls);

    int value = 0;
    TEST_ASSERT_TRUE(cache.get("bright", &value));
    TEST_ASSERT_EQUAL_INT(192, value);
    TEST_ASSERT_TRUE(cache.get("rv.first", &value));
    TEST_ASSERT_EQUAL_INT(1, value);  // reader leaves the default
    TEST_ASSERT_TRUE(cache.get("sleep", &value));
    TEST_ASSERT_EQUAL_INT(120, value);  // invalid stored 9999 -> default
    TEST_ASSERT_FALSE(cache.get("missing", &value));
    TEST_ASSERT_FALSE(cache.set("missing", 1));

    TEST_ASSERT_TRUE(cache.set("bright", 64));
    TEST_ASSERT_TRUE(cache.get("bright", &value));
    TEST_ASSERT_EQUAL_INT(64, value);
    TEST_ASSERT_FALSE(cache.set("bright", 64));
    TEST_ASSERT_EQUAL(39, gSettingsLoadCalls);  // get/set never reload

    SettingsCache defaults;
    defaults.load();
    TEST_ASSERT_TRUE(defaults.loaded());
    TEST_ASSERT_TRUE(defaults.get("dim", &value));
    TEST_ASSERT_EQUAL_INT(30, value);
}

static void test_settings_value_labels() {
    char buf[24];
    const SettingDef* brightness = settingById("P1");
    const SettingDef* dim = settingById("P2");
    const SettingDef* sleep = settingById("P4");
    const SettingDef* portal = settingById("setup.portal");
    const SettingDef* types = settingById("R3");
    const SettingDef* advance = settingById("R8");
    const SettingDef* reminder = settingById("M3");
    TEST_ASSERT_NOT_NULL(brightness);
    TEST_ASSERT_NOT_NULL(dim);
    TEST_ASSERT_NOT_NULL(sleep);
    TEST_ASSERT_NOT_NULL(portal);
    TEST_ASSERT_NOT_NULL(types);
    TEST_ASSERT_NOT_NULL(advance);
    TEST_ASSERT_NOT_NULL(reminder);
    settingValueLabel(*brightness, 128, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("128", buf);
    settingValueLabel(*dim, 0, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("off", buf);
    settingValueLabel(*dim, 15, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("15s", buf);
    settingValueLabel(*sleep, 120, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("2m", buf);
    settingValueLabel(*portal, 0, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("open", buf);
    settingValueLabel(*types, 1, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("Radical>Kanji>Vocab", buf);
    settingValueLabel(*advance, 500, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("0.5 s", buf);
    settingValueLabel(*reminder, 480, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("08:00", buf);
}

static void test_settings_keys_unique_and_short() {
    size_t count = 0;
    const SettingDef* all = settingsAll(&count);
    TEST_ASSERT_NOT_NULL(all);
    int keyed = 0;
    for (size_t i = 0; i < count; ++i) {
        const SettingDef& def = all[i];
        if (!def.nvsKey) continue;
        ++keyed;
        const size_t length = std::strlen(def.nvsKey);
        TEST_ASSERT_GREATER_THAN(0, static_cast<int>(length));
        TEST_ASSERT_LESS_OR_EQUAL(15, static_cast<int>(length));
        for (const char* c = def.nvsKey; *c; ++c)
            TEST_ASSERT_TRUE((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') ||
                             *c == '.' || *c == '_' || *c == '-');
        for (size_t j = i + 1; j < count; ++j)
            if (all[j].nvsKey)
                TEST_ASSERT_TRUE(std::strcmp(def.nvsKey, all[j].nvsKey) != 0);
    }
    TEST_ASSERT_EQUAL(39, keyed);
    TEST_ASSERT_EQUAL(keyed, settingsKeyedCount());
}

static void test_settings_registry_keys_and_defaults() {
    struct Expected {
        const char* id;
        const char* nvsKey;
        int defaultValue;
    };
    // Every keyed row of the CFG-02 tables: key and default exactly.
    static const Expected kExpected[] = {
        {"R1", "revsize", 20},          {"R2", "rv.order", 0},
        {"R3", "rv.types", 0},          {"R4", "rv.first", 1},
        {"R5", "rv.b2b", 1},            {"R6", "rv.typos", 0},
        {"R7", "rv.wrong", 0},          {"R8", "rv.advance", 0},
        {"R9", "rv.details", 0},        {"R10", "rv.header", 0},
        {"R11", "rv.critical", 0},      {"R12", "rv.submit", 0},
        {"L1", "lessonsz", 5},          {"L2", "ls.order", 0},
        {"L3", "ls.quiz", 1},           {"L4", "ls.appr", 0},
        {"L5", "ls.due", 0},            {"L6", "ls.daily", 0},
        {"S1", "st.critpct", 75},       {"S2", "st.critmin", 5},
        {"S3", "st.recent", 0},         {"S4", "st.mistakes", 86400},
        {"S5", "st.practice", 10},      {"Y1", "sy.wake", 1},
        {"Y2", "sy.full", 21600},       {"Y3", "sy.prefetch", 1},
        {"P1", "bright", 128},          {"P2", "dim", 30},
        {"P3", "blank", 60},            {"P4", "sleep", 120},
        {"P5", "dp.lightsleep", 1},     {"P6", "dp.revsleep", 0},
        {"M1", "rm.remind", 0},         {"M2", "rm.quiet", 1},
        {"M3", "rm.time", 0},           {"M4", "rm.sound", 0},
        {"M5", "rm.volume", 1},         {"M6", "rm.autoplay", 0},
        {"M7", "rm.voice", 0},
    };
    const int expectedCount = static_cast<int>(sizeof kExpected / sizeof kExpected[0]);
    TEST_ASSERT_EQUAL(39, expectedCount);
    TEST_ASSERT_EQUAL(expectedCount, settingsKeyedCount());
    for (const Expected& expected : kExpected) {
        const SettingDef* def = settingById(expected.id);
        TEST_ASSERT_NOT_NULL(def);
        TEST_ASSERT_NOT_NULL(def->nvsKey);
        TEST_ASSERT_EQUAL_STRING(expected.nvsKey, def->nvsKey);
        TEST_ASSERT_EQUAL_INT(expected.defaultValue, def->defaultValue);
    }
}

static void test_settings_defaults_are_in_choices() {
    size_t count = 0;
    const SettingDef* all = settingsAll(&count);
    TEST_ASSERT_NOT_NULL(all);
    for (size_t i = 0; i < count; ++i) {
        const SettingDef& def = all[i];
        if (!def.choices || def.choiceCount == 0) continue;
        bool found = false;
        for (uint8_t c = 0; c < def.choiceCount; ++c)
            if (def.choices[c].value == def.defaultValue) found = true;
        TEST_ASSERT_TRUE(found);
    }
}

static void test_settings_sections_cover_every_entry() {
    TEST_ASSERT_EQUAL(8, static_cast<int>(settingsSectionCount()));
    static const char* const kNames[] = {
        "Reviews", "Lessons", "Study", "Sync & data",
        "Display & power", "Reminders & sound", "Account", "System",
    };
    for (size_t i = 0; i < 8; ++i)
        TEST_ASSERT_EQUAL_STRING(kNames[i], settingsSectionName(i));
    TEST_ASSERT_EQUAL_STRING("", settingsSectionName(8));

    const SettingDef* out[16];
    size_t total = 0;
    for (size_t i = 0; i < 8; ++i) {
        const size_t found = settingsInSection(settingsSectionName(i), out, 16);
        TEST_ASSERT_GREATER_THAN(0, static_cast<int>(found));
        total += found;
    }
    TEST_ASSERT_EQUAL(48, static_cast<int>(total));
    TEST_ASSERT_EQUAL(12, static_cast<int>(settingsInSection("Reviews", out, 16)));
}

static void test_settings_review_defaults_match_wanikani() {
    struct Expected {
        const char* id;
        const char* label;
    };
    static const Expected kExpected[] = {
        {"R1", "20"},            {"R2", "Account preference"},
        {"R3", "Off"},           {"R4", "Meaning"},
        {"R5", "On"},            {"R6", "WaniKani"},
        {"R7", "Off"},           {"R8", "Off"},
        {"R9", "Off"},           {"R10", "SRS+accuracy"},
        {"R11", "Off"},          {"R12", "At session end"},
    };
    char value[24];
    for (const Expected& expected : kExpected) {
        const SettingDef* def = settingById(expected.id);
        TEST_ASSERT_NOT_NULL(def);
        settingValueLabel(*def, def->defaultValue, value, sizeof value);
        TEST_ASSERT_EQUAL_STRING(expected.label, value);
    }
}

static void test_settings_lesson_defaults() {
    struct Expected {
        const char* id;
        const char* label;
    };
    static const Expected kExpected[] = {
        {"L1", "5"},  {"L2", "WaniKani"}, {"L3", "Required"},
        {"L4", "Off"}, {"L5", "Off"},      {"L6", "Off"},
    };
    char value[24];
    for (const Expected& expected : kExpected) {
        const SettingDef* def = settingById(expected.id);
        TEST_ASSERT_NOT_NULL(def);
        settingValueLabel(*def, def->defaultValue, value, sizeof value);
        TEST_ASSERT_EQUAL_STRING(expected.label, value);
    }
}

static void test_settings_cycle_next_prev_wraps() {
    char value[24];
    const SettingDef* review = settingById("R1");
    TEST_ASSERT_NOT_NULL(review);
    TEST_ASSERT_EQUAL_INT(10, settingNextValue(*review, 50));
    TEST_ASSERT_EQUAL_INT(50, settingPrevValue(*review, 10));

    const SettingDef* brightness = settingById("P1");
    TEST_ASSERT_NOT_NULL(brightness);
    TEST_ASSERT_EQUAL_INT(64, settingNextValue(*brightness, 255));
    TEST_ASSERT_EQUAL_INT(192, settingNextValue(*brightness, 128));
    TEST_ASSERT_EQUAL_INT(255, settingPrevValue(*brightness, 64));
    TEST_ASSERT_EQUAL_INT(64, settingPrevValue(*brightness, 128));
    TEST_ASSERT_EQUAL_INT(128, settingNextValue(*brightness, 200));
    TEST_ASSERT_EQUAL_INT(128, settingPrevValue(*brightness, 200));

    const SettingDef* sync = settingById("Y1");
    TEST_ASSERT_NOT_NULL(sync);
    TEST_ASSERT_EQUAL_INT(1, settingNextValue(*sync, 0));
    settingValueLabel(*sync, 1, value, sizeof value);
    TEST_ASSERT_EQUAL_STRING("Quick", value);

    const SettingDef* quiz = settingById("L3");
    TEST_ASSERT_NOT_NULL(quiz);
    TEST_ASSERT_EQUAL_INT(1, settingNextValue(*quiz, 0));
    TEST_ASSERT_EQUAL_INT(0, settingPrevValue(*quiz, 1));

    const SettingDef* order = settingById("R2");
    TEST_ASSERT_NOT_NULL(order);
    TEST_ASSERT_EQUAL_INT(0, settingNextValue(*order, 6));
    TEST_ASSERT_EQUAL_INT(6, settingPrevValue(*order, 0));
}

static void test_settings_meaning_first_migration() {
    const SettingDef* first = settingById("R4");
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL_STRING("rv.first", first->nvsKey);
    // No rv.first yet: a stored order reads meaning for 1 and reading for 0.
    TEST_ASSERT_EQUAL_INT(1, settingFirstQuestionValue(false, 0, true, 1));
    TEST_ASSERT_EQUAL_INT(0, settingFirstQuestionValue(false, 0, true, 0));
    // Neither key present: WaniKani's default, meaning first.
    TEST_ASSERT_EQUAL_INT(1, settingFirstQuestionValue(false, 0, false, 0));
    // rv.first wins once written, including the new Random choice.
    TEST_ASSERT_EQUAL_INT(2, settingFirstQuestionValue(true, 2, true, 0));
    TEST_ASSERT_EQUAL_INT(1,
                          settingValidate(*first, settingFirstQuestionValue(false, 0, true, 1)));
    char value[16];
    settingValueLabel(*first, 1, value, sizeof value);
    TEST_ASSERT_EQUAL_STRING("Meaning", value);
    settingValueLabel(*first, 0, value, sizeof value);
    TEST_ASSERT_EQUAL_STRING("Reading", value);
    settingValueLabel(*first, 2, value, sizeof value);
    TEST_ASSERT_EQUAL_STRING("Random", value);
}

static void test_settings_help_fits_one_line() {
    size_t count = 0;
    const SettingDef* all = settingsAll(&count);
    TEST_ASSERT_NOT_NULL(all);
    for (size_t i = 0; i < count; ++i) {
        const SettingDef& def = all[i];
        TEST_ASSERT_NOT_NULL(def.help);
        const size_t length = std::strlen(def.help);
        TEST_ASSERT_GREATER_THAN(0, static_cast<int>(length));
        TEST_ASSERT_LESS_OR_EQUAL(38, static_cast<int>(length));
        TEST_ASSERT_NULL(std::strchr(def.help, '\n'));
    }
}

static void test_settings_actions_have_no_key() {
    static const char* const kActions[] = {
        "setup.portal", "account.wifi",  "account.signOut", "system.firmware",
        "system.about", "system.reset",  "sync.now",        "sync.outbox",
        "sync.clearCache",
    };
    for (const char* id : kActions) {
        const SettingDef* def = settingById(id);
        TEST_ASSERT_NOT_NULL(def);
        TEST_ASSERT_TRUE(def->kind == SettingKind::Action ||
                         def->kind == SettingKind::Submenu ||
                         def->kind == SettingKind::Info);
        TEST_ASSERT_NULL(def->nvsKey);
        TEST_ASSERT_NULL(def->choices);
        TEST_ASSERT_EQUAL(0, def->choiceCount);
        TEST_ASSERT_NOT_NULL(def->valueText);
    }
    const SettingDef* wifi = settingById("account.wifi");
    TEST_ASSERT_NOT_NULL(wifi);
    TEST_ASSERT_EQUAL_STRING("Account", wifi->section);
    TEST_ASSERT_EQUAL_STRING("Wi-Fi", wifi->label);
}

// ------------------------------------------------------------------ wifi
static kk::WifiNetwork wifiEntry(const char* ssid, int rssi, int channel,
                                 kk::WifiSecurity security) {
    kk::WifiNetwork network{};
    std::strncpy(network.ssid, ssid, kk::kWifiSsidMax);
    network.ssid[kk::kWifiSsidMax] = '\0';
    network.rssi = static_cast<int16_t>(rssi);
    network.channel = static_cast<uint8_t>(channel);
    network.security = security;
    return network;
}

static void test_wifi_scan_list_filters_24ghz_and_dedupes() {
    using namespace kk;
    const WifiNetwork raw[] = {
        wifiEntry("Home", -60, 6, WifiSecurity::Wpa2),
        wifiEntry("Home", -45, 1, WifiSecurity::Wpa2),
        wifiEntry("Cafe", -72, 11, WifiSecurity::Open),
        wifiEntry("Five", -40, 36, WifiSecurity::Wpa2),
        wifiEntry("", -50, 1, WifiSecurity::Wpa2),
        wifiEntry("Unknown", -55, 0, WifiSecurity::Wpa2),
    };
    WifiNetwork out[8];
    const size_t count = wifiBuildScanList(raw, sizeof raw / sizeof raw[0], out, 8);
    TEST_ASSERT_EQUAL(2, static_cast<int>(count));
    TEST_ASSERT_EQUAL_STRING("Home", out[0].ssid);
    TEST_ASSERT_EQUAL_INT(-45, out[0].rssi);
    TEST_ASSERT_EQUAL_STRING("Cafe", out[1].ssid);
}

static void test_wifi_scan_list_sorts_and_caps() {
    using namespace kk;
    const WifiNetwork raw[] = {
        wifiEntry("Bravo", -70, 6, WifiSecurity::Wpa2),
        wifiEntry("Charlie", -50, 11, WifiSecurity::Wpa2),
        wifiEntry("Alpha", -50, 1, WifiSecurity::Wpa2),
    };
    WifiNetwork out[8];
    size_t count = wifiBuildScanList(raw, 3, out, 8);
    TEST_ASSERT_EQUAL(3, static_cast<int>(count));
    TEST_ASSERT_EQUAL_STRING("Alpha", out[0].ssid);
    TEST_ASSERT_EQUAL_STRING("Charlie", out[1].ssid);
    TEST_ASSERT_EQUAL_STRING("Bravo", out[2].ssid);

    count = wifiBuildScanList(raw, 3, out, 2);
    TEST_ASSERT_EQUAL(2, static_cast<int>(count));
    TEST_ASSERT_EQUAL_STRING("Alpha", out[0].ssid);
    TEST_ASSERT_EQUAL_STRING("Charlie", out[1].ssid);

    // In-place compaction is what the controller does on its scan buffer.
    WifiNetwork inPlace[] = {
        wifiEntry("Five", -40, 36, WifiSecurity::Wpa2),
        wifiEntry("Home", -55, 6, WifiSecurity::Wpa2),
        wifiEntry("Home", -48, 1, WifiSecurity::Wpa2),
    };
    count = wifiBuildScanList(inPlace, 3, inPlace, 3);
    TEST_ASSERT_EQUAL(1, static_cast<int>(count));
    TEST_ASSERT_EQUAL_STRING("Home", inPlace[0].ssid);
    TEST_ASSERT_EQUAL_INT(-48, inPlace[0].rssi);
}

static void test_wifi_labels_and_security_rules() {
    using namespace kk;
    TEST_ASSERT_TRUE(wifiChannelIs24(1));
    TEST_ASSERT_TRUE(wifiChannelIs24(6));
    TEST_ASSERT_TRUE(wifiChannelIs24(11));
    TEST_ASSERT_TRUE(wifiChannelIs24(14));
    TEST_ASSERT_FALSE(wifiChannelIs24(0));
    TEST_ASSERT_FALSE(wifiChannelIs24(32));
    TEST_ASSERT_FALSE(wifiChannelIs24(36));
    TEST_ASSERT_FALSE(wifiChannelIs24(149));

    TEST_ASSERT_EQUAL_STRING("open", wifiSecurityLabel(WifiSecurity::Open));
    TEST_ASSERT_EQUAL_STRING("WEP", wifiSecurityLabel(WifiSecurity::Wep));
    TEST_ASSERT_EQUAL_STRING("WPA", wifiSecurityLabel(WifiSecurity::Wpa));
    TEST_ASSERT_EQUAL_STRING("WPA2", wifiSecurityLabel(WifiSecurity::Wpa2));
    TEST_ASSERT_EQUAL_STRING("WPA2/3", wifiSecurityLabel(WifiSecurity::Wpa2Wpa3));
    TEST_ASSERT_EQUAL_STRING("WPA3", wifiSecurityLabel(WifiSecurity::Wpa3));
    TEST_ASSERT_EQUAL_STRING("802.1X", wifiSecurityLabel(WifiSecurity::Enterprise));
    TEST_ASSERT_EQUAL_STRING("?", wifiSecurityLabel(WifiSecurity::Unknown));

    TEST_ASSERT_TRUE(wifiSecuritySupported(WifiSecurity::Open));
    TEST_ASSERT_TRUE(wifiSecuritySupported(WifiSecurity::Wpa2));
    TEST_ASSERT_TRUE(wifiSecuritySupported(WifiSecurity::Wpa2Wpa3));
    TEST_ASSERT_TRUE(wifiSecuritySupported(WifiSecurity::Wpa3));
    TEST_ASSERT_TRUE(wifiSecuritySupported(WifiSecurity::Unknown));
    TEST_ASSERT_FALSE(wifiSecuritySupported(WifiSecurity::Wep));
    TEST_ASSERT_FALSE(wifiSecuritySupported(WifiSecurity::Enterprise));
    TEST_ASSERT_FALSE(wifiSecuritySupported(WifiSecurity::Wapi));

    TEST_ASSERT_FALSE(wifiSecurityNeedsPassword(WifiSecurity::Open));
    TEST_ASSERT_TRUE(wifiSecurityNeedsPassword(WifiSecurity::Wpa2));
    TEST_ASSERT_TRUE(wifiSecurityNeedsPassword(WifiSecurity::Unknown));

    TEST_ASSERT_EQUAL_STRING("||||", wifiSignalLabel(-50));
    TEST_ASSERT_EQUAL_STRING("||||", wifiSignalLabel(-55));
    TEST_ASSERT_EQUAL_STRING("|||", wifiSignalLabel(-56));
    TEST_ASSERT_EQUAL_STRING("|||", wifiSignalLabel(-67));
    TEST_ASSERT_EQUAL_STRING("||", wifiSignalLabel(-68));
    TEST_ASSERT_EQUAL_STRING("||", wifiSignalLabel(-78));
    TEST_ASSERT_EQUAL_STRING("|", wifiSignalLabel(-79));
}

static void test_wifi_ssid_and_password_validation() {
    using namespace kk;
    TEST_ASSERT_TRUE(wifiSsidValid("Home"));
    char ssid32[33];
    std::memset(ssid32, 'a', 32);
    ssid32[32] = '\0';
    TEST_ASSERT_TRUE(wifiSsidValid(ssid32));
    char ssid33[34];
    std::memset(ssid33, 'a', 33);
    ssid33[33] = '\0';
    TEST_ASSERT_FALSE(wifiSsidValid(ssid33));
    TEST_ASSERT_FALSE(wifiSsidValid(""));
    TEST_ASSERT_FALSE(wifiSsidValid("bad\nname"));
    TEST_ASSERT_FALSE(wifiSsidValid("bad\x7fname"));

    TEST_ASSERT_TRUE(wifiCheckPassword("", true) == WifiPasswordStatus::Ok);
    TEST_ASSERT_TRUE(wifiCheckPassword("", false) == WifiPasswordStatus::Empty);
    TEST_ASSERT_TRUE(wifiCheckPassword("short", false) == WifiPasswordStatus::TooShort);
    TEST_ASSERT_TRUE(wifiCheckPassword("12345678", false) == WifiPasswordStatus::Ok);
    char pass63[64];
    std::memset(pass63, 'p', 63);
    pass63[63] = '\0';
    TEST_ASSERT_TRUE(wifiCheckPassword(pass63, false) == WifiPasswordStatus::Ok);
    char pass64[65];
    std::memset(pass64, 'p', 64);
    pass64[64] = '\0';
    TEST_ASSERT_TRUE(wifiCheckPassword(pass64, false) == WifiPasswordStatus::TooLong);
    TEST_ASSERT_TRUE(wifiCheckPassword("bad\npassword", false) ==
                     WifiPasswordStatus::BadChar);
    TEST_ASSERT_EQUAL_STRING("At least 8 characters",
                             wifiPasswordError(WifiPasswordStatus::TooShort));
    TEST_ASSERT_EQUAL_STRING("At most 63 characters",
                             wifiPasswordError(WifiPasswordStatus::TooLong));
}

static void test_wifi_failure_reasons() {
    using namespace kk;
    TEST_ASSERT_TRUE(wifiClassifyFailure(WifiLinkStatus::NoSsid, true,
                                         WifiSecurity::Wpa2) ==
                     WifiJoinFailure::NotFound);
    TEST_ASSERT_TRUE(wifiClassifyFailure(WifiLinkStatus::ConnectFailed, true,
                                         WifiSecurity::Wpa2) ==
                     WifiJoinFailure::WrongPassword);
    TEST_ASSERT_TRUE(wifiClassifyFailure(WifiLinkStatus::Disconnected, true,
                                         WifiSecurity::Wpa2) ==
                     WifiJoinFailure::WrongPassword);
    TEST_ASSERT_TRUE(wifiClassifyFailure(WifiLinkStatus::Idle, true,
                                         WifiSecurity::Wpa2) ==
                     WifiJoinFailure::Timeout);
    TEST_ASSERT_TRUE(wifiClassifyFailure(WifiLinkStatus::Other, true,
                                         WifiSecurity::Unknown) ==
                     WifiJoinFailure::Timeout);
    TEST_ASSERT_TRUE(wifiClassifyFailure(WifiLinkStatus::NoSsid, true,
                                         WifiSecurity::Enterprise) ==
                     WifiJoinFailure::Unsupported);
    TEST_ASSERT_TRUE(wifiClassifyFailure(WifiLinkStatus::NoSsid, true,
                                         WifiSecurity::Wep) ==
                     WifiJoinFailure::Unsupported);

    TEST_ASSERT_EQUAL_STRING("not found", wifiFailureLabel(WifiJoinFailure::NotFound));
    TEST_ASSERT_EQUAL_STRING("wrong password",
                             wifiFailureLabel(WifiJoinFailure::WrongPassword));
    TEST_ASSERT_EQUAL_STRING("timeout", wifiFailureLabel(WifiJoinFailure::Timeout));
    TEST_ASSERT_EQUAL_STRING("unsupported",
                             wifiFailureLabel(WifiJoinFailure::Unsupported));
    TEST_ASSERT_EQUAL_STRING("save failed",
                             wifiFailureLabel(WifiJoinFailure::SaveFailed));
}

static void test_wifi_mask_text() {
    using namespace kk;
    char out[16];
    TEST_ASSERT_EQUAL(6, static_cast<int>(wifiMaskText("secret", false, out, sizeof out)));
    TEST_ASSERT_EQUAL_STRING("******", out);
    TEST_ASSERT_EQUAL(6, static_cast<int>(wifiMaskText("secret", true, out, sizeof out)));
    TEST_ASSERT_EQUAL_STRING("secret", out);
    TEST_ASSERT_EQUAL(3, static_cast<int>(wifiMaskText("secret", false, out, 4)));
    TEST_ASSERT_EQUAL_STRING("***", out);
    TEST_ASSERT_EQUAL(0, static_cast<int>(wifiMaskText("", false, out, sizeof out)));
    TEST_ASSERT_EQUAL_STRING("", out);
}

void run_queue_tests();
void run_les_tests();
void run_ota_tests();
void run_answer_tests();
void run_store_tests();
void run_portal_tests();
void run_storage_tests();
void run_net_tests();
void run_app_tests();
void run_cache_tests();
void run_rev_tests();
void run_rev10_tests();
void run_rev11_tests();
void run_ui_tests();
void run_pack_tests();
void run_http_tests();
void run_power_tests();
void run_screen_tests();
void run_hid_tests();
void run_epd_tests();

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_romaji_common_words);
    RUN_TEST(test_romaji_n_and_small_tsu);
    RUN_TEST(test_romaji_ime_editing);
    RUN_TEST(test_meaning_accepts_meanings_synonyms_and_whitelist);
    RUN_TEST(test_meaning_blacklist_wins);
    RUN_TEST(test_meaning_typo_is_a_warning_in_strict_mode);
    RUN_TEST(test_meaning_and_reading_confusion);
    RUN_TEST(test_reading_accepts_romaji_kana_and_katakana);
    RUN_TEST(test_reading_primary_and_valid_both_correct);
    RUN_TEST(test_reading_okurigana_and_n_conversion);
    RUN_TEST(test_vocabulary_and_radical_rules);
    RUN_TEST(test_empty_and_non_kana_readings);
    RUN_TEST(test_lesson_quiz_meaning_then_reading);
    RUN_TEST(test_lesson_quiz_retries_wrong_and_warning);
    RUN_TEST(test_lesson_quiz_radical_only);
    RUN_TEST(test_lesson_order_wanikani_level_then_position);
    RUN_TEST(test_lesson_order_ties_break_by_assignment_id);
    RUN_TEST(test_lesson_order_keeps_unknown_level_last);
    RUN_TEST(test_lesson_order_sorts_the_whole_candidate_pool);
    RUN_TEST(test_session_meaning_then_reading);
    RUN_TEST(test_session_reading_first);
    RUN_TEST(test_session_wrong_parts_requeue_and_count);
    RUN_TEST(test_session_warning_does_not_advance);
    RUN_TEST(test_session_radical_only_meaning);
    RUN_TEST(test_session_filters_and_limit);
    RUN_TEST(test_queue_critical_items_first);
    RUN_TEST(test_queue_critical_limit_keeps_only_critical);
    RUN_TEST(test_queue_critical_preserves_normal_order);
    RUN_TEST(test_queue_without_critical_matches_previous_order);
    RUN_TEST(test_session_skip);
    RUN_TEST(test_session_lower_levels_first);
    RUN_TEST(test_session_stats_accuracy);
    RUN_TEST(test_session_duration_text);
    RUN_TEST(test_cardputer_characters);
    RUN_TEST(test_cardputer_modifiers);
    RUN_TEST(test_cardputer_event_fields);
    RUN_TEST(test_help_question_mark_is_shift_slash);
    RUN_TEST(test_help_tables_are_short);
    RUN_TEST(test_json_values_and_escapes);
    RUN_TEST(test_json_rejects_bad_input);
    RUN_TEST(test_json_stream_events);
    RUN_TEST(test_json_stream_escapes);
    RUN_TEST(test_json_stream_numbers);
    RUN_TEST(test_json_stream_truncation_and_depth);
    RUN_TEST(test_json_stream_sink_rejection);
    RUN_TEST(test_iso8601_round_trip);
    RUN_TEST(test_rate_limit_reset_in_42s);
    RUN_TEST(test_rate_limit_past_or_zero_falls_back);
    RUN_TEST(test_rate_limit_untrusted_clock_falls_back);
    RUN_TEST(test_rate_limit_far_future_is_capped);
    RUN_TEST(test_rate_limit_exactly_now_falls_back);
    RUN_TEST(test_user_parse);
    RUN_TEST(test_summary_parse_and_counts);
    RUN_TEST(test_forecast_line_due_with_next);
    RUN_TEST(test_forecast_line_idle_with_next);
    RUN_TEST(test_forecast_line_due_without_next);
    RUN_TEST(test_forecast_line_idle_without_next);
    RUN_TEST(test_forecast_count_single_and_zero);
    RUN_TEST(test_summary_counts_advance_with_time);
    RUN_TEST(test_summary_next_review_after_last_bucket);
    RUN_TEST(test_forecast_24_slots_fill_gaps);
    RUN_TEST(test_forecast_includes_current_hour);
    RUN_TEST(test_forecast_ignores_past_buckets);
    RUN_TEST(test_forecast_max_and_empty);
    RUN_TEST(test_kanji_needed_rounds_up);
    RUN_TEST(test_kanji_needed_zero_when_reached);
    RUN_TEST(test_kanji_needed_unknown_total);
    RUN_TEST(test_kanji_needed_small_levels);
    RUN_TEST(test_journal_record_roundtrip);
    RUN_TEST(test_journal_block_crc);
    RUN_TEST(test_journal_ring_wraps);
    RUN_TEST(test_journal_patch_srs_after);
    RUN_TEST(test_journal_block_rejects_zero_crc);
    RUN_TEST(test_journal_seven_day_counts);
    RUN_TEST(test_journal_today_accuracy);
    RUN_TEST(test_journal_current_streak_today_empty);
    RUN_TEST(test_journal_best_streak_with_gaps);
    RUN_TEST(test_journal_day_key_time_zone_boundaries);
    RUN_TEST(test_journal_empty);
    RUN_TEST(test_subject_codec_roundtrip);
    RUN_TEST(test_subject_codec_matches_cache_subject);
    RUN_TEST(test_catalog_meta_account_mismatch);
    RUN_TEST(test_catalog_lru_victim);
    RUN_TEST(test_catalog_cap_math);
    RUN_TEST(test_review_statistics_parses_id_and_total_answers);
    RUN_TEST(test_critical_strictly_below_threshold);
    RUN_TEST(test_critical_minimum_answers);
    RUN_TEST(test_critical_custom_thresholds);
    RUN_TEST(test_critical_excludes_hidden_and_burned);
    RUN_TEST(test_critical_missing_assignment_skipped);
    RUN_TEST(test_critical_sorted_worst_first);
    RUN_TEST(test_critical_list_round_trip);
    RUN_TEST(test_practice_recent_lessons_stage_or_window);
    RUN_TEST(test_practice_current_level_kanji_below_guru);
    RUN_TEST(test_practice_limit_and_empty);
    RUN_TEST(test_practice_pool_round_trip);
    RUN_TEST(test_summary_text_parser);
    RUN_TEST(test_summary_text_parser_empty);
    RUN_TEST(test_review_response_parse);
    RUN_TEST(test_level_progressions_parse);
    RUN_TEST(test_level_kanji_percent);
    RUN_TEST(test_srs_total_count_and_groups);
    RUN_TEST(test_assignment_parse);
    RUN_TEST(test_subject_parse);
    RUN_TEST(test_subject_audio_parse);
    RUN_TEST(test_audio_index_round_trip);
    RUN_TEST(test_audio_choose_voice_fallback);
    RUN_TEST(test_audio_file_name_and_evict);
    RUN_TEST(test_assignment_stream_matches_dom);
    RUN_TEST(test_assignment_stream_edges);
    RUN_TEST(test_subject_stream_matches_dom);
    RUN_TEST(test_subject_stream_edges_and_caps);
    RUN_TEST(test_summary_subject_ids);
    RUN_TEST(test_markup_and_subject_details);
    RUN_TEST(test_vocabulary_detail_fields);
    RUN_TEST(test_study_materials_parse_and_merge);
    RUN_TEST(test_review_statistics_parse_and_merge);
    RUN_TEST(test_crc32_known_vector);
    RUN_TEST(test_outbox_model);
    RUN_TEST(test_outbox_http_code_default);
    RUN_TEST(test_outbox_attempt_cap);
    RUN_TEST(test_outbox_capacity_and_next_seq);
    RUN_TEST(test_outbox_stuck_counts);
    RUN_TEST(test_outbox_stuck_remove);
    RUN_TEST(test_submit_policy_values);
    RUN_TEST(test_submit_policy_every_five_counts);
    RUN_TEST(test_submit_policy_session_end);
    RUN_TEST(test_stage_tracker_per_assignment);
    RUN_TEST(test_stage_tracker_unknown_is_zero);
    RUN_TEST(test_stage_tracker_bounded);
    RUN_TEST(test_http_retry_policy);
    RUN_TEST(test_cache_account_key);
    RUN_TEST(test_cache_snapshot_round_trip);
    RUN_TEST(test_cache_v11_snapshot_loads);
    RUN_TEST(test_cache_corruption_is_rejected);
    RUN_TEST(test_cache_subjects_round_trip);
    RUN_TEST(test_sync_plan_manual_is_full);
    RUN_TEST(test_sync_plan_wake_quick);
    RUN_TEST(test_sync_plan_full_when_due);
    RUN_TEST(test_sync_plan_skip_recent);
    RUN_TEST(test_sync_plan_untrusted_clock_is_full);
    RUN_TEST(test_sync_plan_user_due_cold_and_six_hours);
    RUN_TEST(test_prefetch_when_empty);
    RUN_TEST(test_prefetch_when_smaller);
    RUN_TEST(test_prefetch_skips_full_batch);
    RUN_TEST(test_prefetch_skips_low_battery);
    RUN_TEST(test_prefetch_skips_no_due);
    RUN_TEST(test_sync_steps_quick_count);
    RUN_TEST(test_sync_steps_full_count);
    RUN_TEST(test_sync_steps_optional_flags);
    RUN_TEST(test_sync_steps_interrupt_skips_optional);
    RUN_TEST(test_sync_steps_error_is_terminal);
    RUN_TEST(test_sync_time_ready_trusted_clock);
    RUN_TEST(test_sync_time_ready_waits_untrusted);
    RUN_TEST(test_sync_time_ready_gives_up);
    RUN_TEST(test_sync_runs_while_screen_off_background);
    RUN_TEST(test_ota_manifest_parse);
    RUN_TEST(test_ota_manifest_edges);
    RUN_TEST(test_ota_versions);
    RUN_TEST(test_ota_offer);
    RUN_TEST(test_ota_url_resolution);
    RUN_TEST(test_settings_registry_ids);
    RUN_TEST(test_settings_registry_defaults);
    RUN_TEST(test_settings_cycle_wraps);
    RUN_TEST(test_settings_validate);
    RUN_TEST(test_settings_help_width);
    RUN_TEST(test_settings_sections);
    RUN_TEST(test_settings_cache);
    RUN_TEST(test_settings_value_labels);
    RUN_TEST(test_settings_keys_unique_and_short);
    RUN_TEST(test_settings_registry_keys_and_defaults);
    RUN_TEST(test_settings_defaults_are_in_choices);
    RUN_TEST(test_settings_sections_cover_every_entry);
    RUN_TEST(test_settings_review_defaults_match_wanikani);
    RUN_TEST(test_settings_lesson_defaults);
    RUN_TEST(test_settings_cycle_next_prev_wraps);
    RUN_TEST(test_settings_meaning_first_migration);
    RUN_TEST(test_settings_help_fits_one_line);
    RUN_TEST(test_settings_actions_have_no_key);
    RUN_TEST(test_wifi_scan_list_filters_24ghz_and_dedupes);
    RUN_TEST(test_wifi_scan_list_sorts_and_caps);
    RUN_TEST(test_wifi_labels_and_security_rules);
    RUN_TEST(test_wifi_ssid_and_password_validation);
    RUN_TEST(test_wifi_failure_reasons);
    RUN_TEST(test_wifi_mask_text);
    run_queue_tests();
    run_les_tests();
    run_ota_tests();
    run_answer_tests();
    run_store_tests();
    run_portal_tests();
    run_storage_tests();
    run_net_tests();
    run_app_tests();
    run_cache_tests();
    run_rev_tests();
    run_rev10_tests();
    run_rev11_tests();
    run_ui_tests();
    run_pack_tests();
    run_http_tests();
    run_power_tests();
    run_screen_tests();
    run_hid_tests();
    run_epd_tests();
    return UNITY_END();
}

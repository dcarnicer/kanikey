#include <unity.h>

#include <string>

#include "wk/AnswerChecker.h"
#include "wk/ReviewQueue.h"
#include "wk/RomajiIme.h"
#include "wk/Text.h"

using namespace kk;

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
    TEST_ASSERT_EQUAL_STRING("たb", ime.text().c_str());
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

static void test_reading_primary_vs_valid() {
    const Subject s = kanji();
    const AnswerCheck strict = checkAnswer(s, AnswerKind::Reading, "taberu");
    TEST_ASSERT_EQUAL(AnswerStatus::Warning, strict.status);
    TEST_ASSERT_TRUE(strict.close);
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
    TEST_ASSERT_TRUE(n.message.find("nn") != std::string::npos);
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

static void test_session_skip() {
    const Subject s = kanji();
    ReviewQueue q;
    q.build({assignment(20, s.id, 3, 100)}, {s}, 1000);
    q.skip();
    TEST_ASSERT_TRUE(q.empty());
    TEST_ASSERT_EQUAL(0, q.completedCount());
}

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
    RUN_TEST(test_reading_primary_vs_valid);
    RUN_TEST(test_reading_okurigana_and_n_conversion);
    RUN_TEST(test_vocabulary_and_radical_rules);
    RUN_TEST(test_empty_and_non_kana_readings);
    RUN_TEST(test_session_meaning_then_reading);
    RUN_TEST(test_session_wrong_parts_requeue_and_count);
    RUN_TEST(test_session_warning_does_not_advance);
    RUN_TEST(test_session_radical_only_meaning);
    RUN_TEST(test_session_filters_and_limit);
    RUN_TEST(test_session_skip);
    return UNITY_END();
}

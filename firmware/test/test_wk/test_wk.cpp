#include <unity.h>

#include <string>

#include "ui/KeyInput.h"
#include "wk/AnswerChecker.h"
#include "wk/Assignment.h"
#include "wk/Cache.h"
#include "wk/Crc32.h"
#include "wk/Outbox.h"
#include "wk/Json.h"
#include "wk/LessonQuiz.h"
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

    q.submit("taberu");
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
    TEST_ASSERT_TRUE(tgc::cardputerAdvEvent(static_cast<uint8_t>(codeAt(11, 2)), &e));
    TEST_ASSERT_FALSE(e.down);
    TEST_ASSERT_EQUAL(tgc::Key::Up, e.key);
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

static void test_iso8601_round_trip() {
    uint32_t e = 0;
    TEST_ASSERT_TRUE(parseIso8601("2026-10-25T12:38:37.375268Z", &e));
    TEST_ASSERT_EQUAL_STRING("2026-10-25T12:38:37Z", formatIso8601(e).c_str());
    TEST_ASSERT_TRUE(parseIso8601("1970-01-01T00:00:00Z", &e));
    TEST_ASSERT_EQUAL_UINT32(0, e);
    TEST_ASSERT_FALSE(parseIso8601("2026-10-25 12:38:37", &e));
    TEST_ASSERT_FALSE(parseIso8601("2026-10-25T12:38:37+02:00", &e));
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


// ---------------------------------------------------------------- API records
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
    TEST_ASSERT_EQUAL(1, outboxRejectedCount(box));
    TEST_ASSERT_EQUAL(1, outboxPendingCount(box));
    TEST_ASSERT_TRUE(outboxRemove(box, 1));
    TEST_ASSERT_FALSE(outboxRemove(box, 1));
    TEST_ASSERT_EQUAL(1, box.size());
    TEST_ASSERT_EQUAL_UINT32(3, outboxAdd(box, 103, 12, 0, 0, 0));
}


static void test_markup_and_subject_details() {
    TEST_ASSERT_EQUAL_STRING("A ground B", stripMarkup("A <radical>ground</radical> B").c_str());
    JsonParser p;
    JsonValue v;
    const char* text =
        "{\"data\":[{\"id\":10,\"object\":\"kanji\",\"data\":{\"level\":5,"
        "\"characters\":\"休\",\"meanings\":[{\"meaning\":\"rest\",\"primary\":true,"
        "\"accepted_answer\":true}],\"component_subject_ids\":[1,2],"
        "\"meaning_mnemonic\":\"A <radical>person</radical> by a <kanji>tree</kanji>.\","
        "\"reading_mnemonic\":\"<reading>kyuu</reading>.\"}}]}";
    TEST_ASSERT_TRUE(p.parse(text, &v));
    const std::vector<Subject> ss = parseSubjects(v, true);
    TEST_ASSERT_EQUAL(1, ss.size());
    TEST_ASSERT_EQUAL(2, ss[0].componentSubjectIds.size());
    TEST_ASSERT_EQUAL_STRING("A person by a tree.", ss[0].meaningMnemonic.c_str());
    TEST_ASSERT_EQUAL_STRING("kyuu.", ss[0].readingMnemonic.c_str());
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
    s.meaningMnemonic = "An adult is a big person.";
    s.readingMnemonic = "Oh, ton of adults.";
    s.meaningHint = "Not a child.";
    s.readingHint = "Long o.";
    s.meaningNote = "Use this when talking about people.";
    s.readingNote = "The second o is long.";
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
    TEST_ASSERT_EQUAL(1, out.assignments.size());
    TEST_ASSERT_EQUAL_UINT32(100, out.assignments[0].id);
    TEST_ASSERT_EQUAL_UINT32(42, out.assignments[0].subjectId);
    TEST_ASSERT_EQUAL(3, out.assignments[0].srsStage);
    TEST_ASSERT_TRUE(out.assignments[0].hidden);
    TEST_ASSERT_TRUE(out.assignments[0].pendingReview);
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
    TEST_ASSERT_EQUAL_STRING("An adult is a big person.", s.meaningMnemonic.c_str());
    TEST_ASSERT_EQUAL_STRING("Long o.", s.readingHint.c_str());
    TEST_ASSERT_EQUAL_STRING("Use this when talking about people.", s.meaningNote.c_str());
    TEST_ASSERT_EQUAL_STRING("The second o is long.", s.readingNote.c_str());
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
    RUN_TEST(test_lesson_quiz_meaning_then_reading);
    RUN_TEST(test_lesson_quiz_retries_wrong_and_warning);
    RUN_TEST(test_lesson_quiz_radical_only);
    RUN_TEST(test_session_meaning_then_reading);
    RUN_TEST(test_session_reading_first);
    RUN_TEST(test_session_wrong_parts_requeue_and_count);
    RUN_TEST(test_session_warning_does_not_advance);
    RUN_TEST(test_session_radical_only_meaning);
    RUN_TEST(test_session_filters_and_limit);
    RUN_TEST(test_session_skip);
    RUN_TEST(test_session_lower_levels_first);
    RUN_TEST(test_cardputer_characters);
    RUN_TEST(test_cardputer_modifiers);
    RUN_TEST(test_cardputer_event_fields);
    RUN_TEST(test_json_values_and_escapes);
    RUN_TEST(test_json_rejects_bad_input);
    RUN_TEST(test_iso8601_round_trip);
    RUN_TEST(test_user_parse);
    RUN_TEST(test_summary_parse_and_counts);
    RUN_TEST(test_summary_text_parser);
    RUN_TEST(test_review_response_parse);
    RUN_TEST(test_assignment_parse);
    RUN_TEST(test_subject_parse);
    RUN_TEST(test_summary_subject_ids);
    RUN_TEST(test_markup_and_subject_details);
    RUN_TEST(test_study_materials_parse_and_merge);
    RUN_TEST(test_review_statistics_parse_and_merge);
    RUN_TEST(test_crc32_known_vector);
    RUN_TEST(test_outbox_model);
    RUN_TEST(test_cache_account_key);
    RUN_TEST(test_cache_snapshot_round_trip);
    RUN_TEST(test_cache_corruption_is_rejected);
    RUN_TEST(test_cache_subjects_round_trip);
    return UNITY_END();
}

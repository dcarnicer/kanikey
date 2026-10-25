#include <unity.h>

#include <string>

#include "ui/KeyInput.h"
#include "wk/AnswerChecker.h"
#include "wk/Assignment.h"
#include "wk/Crc32.h"
#include "wk/Outbox.h"
#include "wk/Json.h"
#include "wk/SubjectParser.h"
#include "wk/Summary.h"
#include "wk/Time.h"
#include "wk/User.h"
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
        "\"subscription\":{\"active\":true,\"max_level_granted\":60},"
        "\"preferences\":{\"lessons_batch_size\":10}}}";
    TEST_ASSERT_TRUE(p.parse(text, &v));
    User u;
    TEST_ASSERT_TRUE(parseUser(v, &u));
    TEST_ASSERT_EQUAL_STRING("tester", u.username.c_str());
    TEST_ASSERT_EQUAL(8, u.level);
    TEST_ASSERT_TRUE(u.subscriptionActive);
    TEST_ASSERT_EQUAL(60, u.maxLevelGranted);
    TEST_ASSERT_EQUAL(10, u.lessonsBatchSize);
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
        "\"subject_type\":\"kanji\",\"srs_stage\":3,"
        "\"available_at\":\"2026-10-25T14:00:00Z\",\"hidden\":false}},"
        "{\"id\":102,\"object\":\"assignment\",\"data\":{\"subject_id\":11,"
        "\"subject_type\":\"radical\",\"srs_stage\":1,"
        "\"available_at\":\"2026-10-25T15:00:00Z\",\"hidden\":true}}]}";
    TEST_ASSERT_TRUE(p.parse(text, &v));
    const std::vector<Assignment> as = parseAssignments(v);
    TEST_ASSERT_EQUAL(2, as.size());
    TEST_ASSERT_EQUAL_UINT32(101, as[0].id);
    TEST_ASSERT_EQUAL_UINT32(10, as[0].subjectId);
    TEST_ASSERT_EQUAL(SubjectType::Kanji, as[0].type);
    TEST_ASSERT_EQUAL(3, as[0].srsStage);
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
    RUN_TEST(test_cardputer_characters);
    RUN_TEST(test_cardputer_modifiers);
    RUN_TEST(test_cardputer_event_fields);
    RUN_TEST(test_json_values_and_escapes);
    RUN_TEST(test_json_rejects_bad_input);
    RUN_TEST(test_iso8601_round_trip);
    RUN_TEST(test_user_parse);
    RUN_TEST(test_summary_parse_and_counts);
    RUN_TEST(test_assignment_parse);
    RUN_TEST(test_subject_parse);
    RUN_TEST(test_summary_subject_ids);
    RUN_TEST(test_crc32_known_vector);
    RUN_TEST(test_outbox_model);
    return UNITY_END();
}

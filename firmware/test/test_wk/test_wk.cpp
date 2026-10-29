#include <unity.h>

#include <cmath>
#include <string>
#include <vector>

#include "ui/KeyInput.h"
#include "wk/AnswerChecker.h"
#include "wk/Assignment.h"
#include "wk/Cache.h"
#include "wk/Crc32.h"
#include "wk/HttpRetry.h"
#include "wk/Outbox.h"
#include "wk/Json.h"
#include "wk/JsonStream.h"
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
#include "wk/SrsCounts.h"
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
            "level":5,"characters":"\u98df",
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
            "level":1,"characters":null,
            "meanings":[{"meaning":"ground","primary":true,"accepted_answer":true}],
            "component_subject_ids":[]
        }},
        {"id":30,"object":"vocabulary","data":{
            "level":7,"characters":"日本",
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
    JsonParser p;
    JsonValue v;
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
    TEST_ASSERT_TRUE(p.parse(text, &v));
    LevelProgress progress;
    TEST_ASSERT_TRUE(parseLevelProgressions(v, 8, &progress));
    TEST_ASSERT_EQUAL(8, progress.level);
    uint32_t unlocked = 0;
    TEST_ASSERT_TRUE(parseIso8601("2026-10-12T12:00:00Z", &unlocked));
    TEST_ASSERT_EQUAL_UINT32(unlocked, progress.unlockedAt);
    TEST_ASSERT_EQUAL_UINT32(0, progress.startedAt);
    TEST_ASSERT_EQUAL_UINT32(0, progress.passedAt);

    LevelProgress highest;
    TEST_ASSERT_TRUE(parseLevelProgressions(v, 20, &highest));
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
    RUN_TEST(test_session_stats_accuracy);
    RUN_TEST(test_session_duration_text);
    RUN_TEST(test_cardputer_characters);
    RUN_TEST(test_cardputer_modifiers);
    RUN_TEST(test_cardputer_event_fields);
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
    RUN_TEST(test_summary_text_parser);
    RUN_TEST(test_summary_text_parser_empty);
    RUN_TEST(test_review_response_parse);
    RUN_TEST(test_level_progressions_parse);
    RUN_TEST(test_level_kanji_percent);
    RUN_TEST(test_srs_total_count_and_groups);
    RUN_TEST(test_assignment_parse);
    RUN_TEST(test_subject_parse);
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
    RUN_TEST(test_http_retry_policy);
    RUN_TEST(test_cache_account_key);
    RUN_TEST(test_cache_snapshot_round_trip);
    RUN_TEST(test_cache_corruption_is_rejected);
    RUN_TEST(test_cache_subjects_round_trip);
    return UNITY_END();
}

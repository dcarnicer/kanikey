#include <unity.h>

#include <string>

#include "wk/AnswerChecker.h"

using namespace kk;

namespace {

Subject typoSubject() {
    Subject s;
    s.id = 201;
    s.type = SubjectType::Vocabulary;
    s.level = 1;
    s.characters = "作文";
    s.meanings = {{"Composition", true, true}};
    s.readings = {{"さくぶん", true, true, ReadingType::Kunyomi}};
    return s;
}

Subject readingAsMeaningSubject() {
    Subject s;
    s.id = 202;
    s.type = SubjectType::Kanji;
    s.level = 1;
    s.characters = "一";
    s.meanings = {{"one", true, true}};
    s.readings = {{"いち", true, true, ReadingType::Onyomi}};
    return s;
}

Subject nHintSubject() {
    Subject s;
    s.id = 203;
    s.type = SubjectType::Vocabulary;
    s.level = 1;
    s.characters = "今夜";
    s.meanings = {{"tonight", true, true}};
    s.readings = {{"こんや", true, true, ReadingType::Kunyomi}};
    return s;
}

}  // namespace

// ------------------------------------------------------- FIX-03 warnings

static void test_strict_typo_warning_does_not_name_answer() {
    const Subject s = typoSubject();
    const AnswerCheck c = checkAnswer(s, AnswerKind::Meaning, "compositon", true);
    TEST_ASSERT_EQUAL(AnswerStatus::Warning, c.status);
    TEST_ASSERT_TRUE(c.close);
    TEST_ASSERT_TRUE(c.message.find("Close") != std::string::npos);
    // The warning must not spell the expected answer (or the misspelling).
    TEST_ASSERT_TRUE(c.message.find("Composition") == std::string::npos);
    TEST_ASSERT_TRUE(c.message.find("composition") == std::string::npos);
    TEST_ASSERT_TRUE(c.message.find("compositon") == std::string::npos);
}

static void test_warning_status_unchanged() {
    const Subject one = readingAsMeaningSubject();
    const AnswerCheck reading = checkAnswer(one, AnswerKind::Meaning, "ichi");
    TEST_ASSERT_EQUAL(AnswerStatus::Warning, reading.status);
    TEST_ASSERT_EQUAL_STRING("That is the reading. Type the meaning",
                             reading.message.c_str());

    const Subject kon = nHintSubject();
    const AnswerCheck n = checkAnswer(kon, AnswerKind::Reading, "konya");
    TEST_ASSERT_EQUAL(AnswerStatus::Warning, n.status);
    TEST_ASSERT_TRUE(n.message.find("n'") != std::string::npos);
}

void run_app_tests() {
    RUN_TEST(test_strict_typo_warning_does_not_name_answer);
    RUN_TEST(test_warning_status_unchanged);
}

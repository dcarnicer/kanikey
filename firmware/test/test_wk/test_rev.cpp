// REV-01/REV-03: accepted-answer display tokens and the first-sentence hint.
#include <unity.h>

#include <string>
#include <vector>

#include "wk/AnswerDisplay.h"
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
}

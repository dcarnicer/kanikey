#include <unity.h>

#include <string>

#include "wk/AnswerChecker.h"
#include "wk/RomajiIme.h"
#include "wk/Text.h"

using namespace kk;

namespace {

Subject personKanji() {
    Subject s;
    s.id = 100;
    s.type = SubjectType::Kanji;
    s.level = 1;
    s.characters = "人";
    s.meanings = {{"person", true, true}};
    s.readings = {
        {"じん", true, true, ReadingType::Onyomi},
        {"にん", false, true, ReadingType::Onyomi},
        {"ひと", false, false, ReadingType::Kunyomi},
    };
    return s;
}

Subject kunyomiKanji() {
    Subject s;
    s.id = 101;
    s.type = SubjectType::Kanji;
    s.level = 1;
    s.characters = "見";
    s.meanings = {{"see", true, true}};
    s.readings = {
        {"み.る", true, true, ReadingType::Kunyomi},
        {"けん", false, false, ReadingType::Onyomi},
    };
    return s;
}

Subject nanoriKanji() {
    Subject s;
    s.id = 102;
    s.type = SubjectType::Kanji;
    s.level = 1;
    s.characters = "名";
    s.meanings = {{"name", true, true}};
    s.readings = {
        {"のり", true, true, ReadingType::Nanori},
        {"のる", false, false, ReadingType::Kunyomi},
    };
    return s;
}

Subject mixedTypeKanji() {
    Subject s;
    s.id = 103;
    s.type = SubjectType::Kanji;
    s.level = 1;
    s.characters = "人";
    s.meanings = {{"person", true, true}};
    s.readings = {
        {"じん", true, true, ReadingType::Onyomi},
        {"ひと", false, true, ReadingType::Kunyomi},
        {"にん", false, false, ReadingType::Nanori},
    };
    return s;
}

Subject readingKanji(const char* characters, const char* reading) {
    Subject s;
    s.id = 104;
    s.type = SubjectType::Kanji;
    s.level = 1;
    s.characters = characters;
    s.meanings = {{"item", true, true}};
    s.readings = {{reading, true, true, ReadingType::Kunyomi}};
    return s;
}

Subject meaningVocab(const char* characters, const char* meaning) {
    Subject s;
    s.id = 105;
    s.type = SubjectType::Vocabulary;
    s.level = 1;
    s.characters = characters;
    s.meanings = {{meaning, true, true}};
    s.readings = {{"よみ", true, true, ReadingType::Kunyomi}};
    return s;
}

}  // namespace

// ------------------------------------------------------- FIX-04 readings

static void test_kanji_rejected_kun_yomi_is_warning() {
    const Subject s = personKanji();
    const AnswerCheck c = checkAnswer(s, AnswerKind::Reading, "hito");
    TEST_ASSERT_EQUAL(AnswerStatus::Warning, c.status);
    TEST_ASSERT_TRUE(c.message.find("on'yomi") != std::string::npos);
}

static void test_kanji_accepted_non_primary_is_correct() {
    const Subject s = personKanji();
    TEST_ASSERT_EQUAL(AnswerStatus::Correct,
                      checkAnswer(s, AnswerKind::Reading, "nin").status);
}

static void test_kanji_primary_is_correct() {
    const Subject s = personKanji();
    TEST_ASSERT_EQUAL(AnswerStatus::Correct,
                      checkAnswer(s, AnswerKind::Reading, "jin").status);
}

static void test_kanji_kunyomi_primary_rejects_onyomi_with_warning() {
    const Subject s = kunyomiKanji();
    TEST_ASSERT_EQUAL(AnswerStatus::Correct,
                      checkAnswer(s, AnswerKind::Reading, "miru").status);
    const AnswerCheck c = checkAnswer(s, AnswerKind::Reading, "ken");
    TEST_ASSERT_EQUAL(AnswerStatus::Warning, c.status);
    TEST_ASSERT_TRUE(c.message.find("kun'yomi") != std::string::npos);
}

static void test_kanji_rejected_reading_names_the_type() {
    const AnswerCheck nanori =
        checkAnswer(nanoriKanji(), AnswerKind::Reading, "noru");
    TEST_ASSERT_EQUAL(AnswerStatus::Warning, nanori.status);
    TEST_ASSERT_TRUE(nanori.message.find("nanori") != std::string::npos);

    // Accepted readings of several types: no single type to name.
    const AnswerCheck mixed =
        checkAnswer(mixedTypeKanji(), AnswerKind::Reading, "nin");
    TEST_ASSERT_EQUAL(AnswerStatus::Warning, mixed.status);
    TEST_ASSERT_TRUE(mixed.message.find("not accepted") != std::string::npos);
}

// ------------------------------------------------------- FIX-05 n' hint

static void test_n_hint_konya() {
    const Subject s = readingKanji("今", "こんや");
    const AnswerCheck c = checkAnswer(s, AnswerKind::Reading, "konya");
    TEST_ASSERT_EQUAL(AnswerStatus::Warning, c.status);
    TEST_ASSERT_TRUE(c.close);
}

static void test_n_hint_kinyoubi() {
    const Subject s = readingKanji("金曜日", "きんようび");
    const AnswerCheck c = checkAnswer(s, AnswerKind::Reading, "kinyoubi");
    TEST_ASSERT_EQUAL(AnswerStatus::Warning, c.status);
}

static void test_n_hint_tani() {
    const Subject s = readingKanji("単位", "たんい");
    const AnswerCheck c = checkAnswer(s, AnswerKind::Reading, "tani");
    TEST_ASSERT_EQUAL(AnswerStatus::Warning, c.status);
}

static void test_n_hint_kinen() {
    const Subject s = readingKanji("禁煙", "きんえん");
    const AnswerCheck c = checkAnswer(s, AnswerKind::Reading, "kinen");
    TEST_ASSERT_EQUAL(AnswerStatus::Warning, c.status);
}

static void test_n_hint_genan() {
    const Subject s = readingKanji("原案", "げんあん");
    const AnswerCheck c = checkAnswer(s, AnswerKind::Reading, "genan");
    TEST_ASSERT_EQUAL(AnswerStatus::Warning, c.status);
}

static void test_n_apostrophe_is_correct() {
    const Subject s = readingKanji("今", "こんや");
    TEST_ASSERT_EQUAL(AnswerStatus::Correct,
                      checkAnswer(s, AnswerKind::Reading, "kon'ya").status);
}

static void test_n_hint_message_text() {
    const Subject s = readingKanji("今", "こんや");
    const AnswerCheck c = checkAnswer(s, AnswerKind::Reading, "konya");
    TEST_ASSERT_EQUAL_STRING("Use n' before a vowel or y", c.message.c_str());
}

// ------------------------------------------------------- FIX-15 typo policy

static void test_typo_tolerance_boundaries() {
    TEST_ASSERT_EQUAL(0, typoTolerance(0));
    TEST_ASSERT_EQUAL(0, typoTolerance(3));
    TEST_ASSERT_EQUAL(1, typoTolerance(4));
    TEST_ASSERT_EQUAL(1, typoTolerance(5));
    TEST_ASSERT_EQUAL(2, typoTolerance(6));
    TEST_ASSERT_EQUAL(2, typoTolerance(7));
    TEST_ASSERT_EQUAL(2, typoTolerance(8));
    TEST_ASSERT_EQUAL(3, typoTolerance(9));
    TEST_ASSERT_EQUAL(3, typoTolerance(20));
}

static void test_short_answer_long_input_not_close() {
    const Subject one = meaningVocab("一", "one");
    TEST_ASSERT_EQUAL(AnswerStatus::Incorrect,
                      checkAnswer(one, AnswerKind::Meaning, "none").status);

    const Subject hand = meaningVocab("手", "hand");
    TEST_ASSERT_EQUAL(AnswerStatus::Incorrect,
                      checkAnswer(hand, AnswerKind::Meaning, "handle").status);
}

static void test_long_answer_small_typo_close() {
    const Subject s = meaningVocab("作文", "Composition");
    const AnswerCheck strict = checkAnswer(s, AnswerKind::Meaning, "compositon");
    TEST_ASSERT_EQUAL(AnswerStatus::Warning, strict.status);
    TEST_ASSERT_TRUE(strict.close);
    TEST_ASSERT_TRUE(strict.message.find("Close") != std::string::npos);
    TEST_ASSERT_EQUAL(AnswerStatus::Correct,
                      checkAnswer(s, AnswerKind::Meaning, "compositon", false).status);
}

static void test_two_typos_in_three_letters_not_close() {
    const Subject s = meaningVocab("一", "one");
    TEST_ASSERT_EQUAL(AnswerStatus::Incorrect,
                      checkAnswer(s, AnswerKind::Meaning, "eno").status);
}

static void test_exact_match_unaffected() {
    const Subject s = meaningVocab("作文", "Composition");
    TEST_ASSERT_EQUAL(AnswerStatus::Correct,
                      checkAnswer(s, AnswerKind::Meaning, "composition").status);
    TEST_ASSERT_EQUAL(AnswerStatus::Correct,
                      checkAnswer(s, AnswerKind::Meaning, "Composition").status);
}

// ------------------------------------------------------- FIX-16 lowercase

static void test_meaning_field_capitalised_romaji_is_reading_warning() {
    Subject s = meaningVocab("一", "one");
    s.readings = {{"いち", true, true, ReadingType::Onyomi}};
    const AnswerCheck c = checkAnswer(s, AnswerKind::Meaning, "Ichi");
    TEST_ASSERT_EQUAL(AnswerStatus::Warning, c.status);
    TEST_ASSERT_TRUE(c.message.find("reading") != std::string::npos);
}

static void test_reading_field_capitalised_romaji_is_correct() {
    const Subject s = readingKanji("一", "いち");
    TEST_ASSERT_EQUAL(AnswerStatus::Correct,
                      checkAnswer(s, AnswerKind::Reading, "Ichi").status);
    TEST_ASSERT_EQUAL(AnswerStatus::Correct,
                      checkAnswer(s, AnswerKind::Reading, "ICHI").status);
}

static void test_mixed_case_kana_untouched() {
    const Subject s = readingKanji("一", "いち");
    TEST_ASSERT_EQUAL(AnswerStatus::Correct,
                      checkAnswer(s, AnswerKind::Reading, "IcHi").status);
    TEST_ASSERT_EQUAL(AnswerStatus::Correct,
                      checkAnswer(s, AnswerKind::Reading, "いち").status);
    // Lowercasing ASCII must not touch the multi-byte kana bytes.
    TEST_ASSERT_EQUAL_STRING("いちa", lowerAscii("いちA").c_str());
}

static void test_uppercase_ascii_meaning_unchanged() {
    const Subject s = meaningVocab("食", "eat");
    TEST_ASSERT_EQUAL(AnswerStatus::Correct,
                      checkAnswer(s, AnswerKind::Meaning, "EAT").status);
}

// ------------------------------------------------------- FIX-17 IME aliases

static void test_ime_alias_sya_row() {
    TEST_ASSERT_EQUAL_STRING("しゃ", romajiToHiragana("sya").c_str());
    TEST_ASSERT_EQUAL_STRING("しゅ", romajiToHiragana("syu").c_str());
    TEST_ASSERT_EQUAL_STRING("しょ", romajiToHiragana("syo").c_str());
}

static void test_ime_alias_zya_row() {
    TEST_ASSERT_EQUAL_STRING("じゃ", romajiToHiragana("zya").c_str());
    TEST_ASSERT_EQUAL_STRING("じゅ", romajiToHiragana("zyu").c_str());
    TEST_ASSERT_EQUAL_STRING("じょ", romajiToHiragana("zyo").c_str());
}

static void test_ime_alias_jya_row() {
    TEST_ASSERT_EQUAL_STRING("じゃ", romajiToHiragana("jya").c_str());
    TEST_ASSERT_EQUAL_STRING("じゅ", romajiToHiragana("jyu").c_str());
    TEST_ASSERT_EQUAL_STRING("じょ", romajiToHiragana("jyo").c_str());
}

static void test_ime_alias_cya_row() {
    TEST_ASSERT_EQUAL_STRING("ちゃ", romajiToHiragana("cya").c_str());
    TEST_ASSERT_EQUAL_STRING("ちゅ", romajiToHiragana("cyu").c_str());
    TEST_ASSERT_EQUAL_STRING("ちょ", romajiToHiragana("cyo").c_str());
}

static void test_ime_alias_she_che_je() {
    TEST_ASSERT_EQUAL_STRING("しぇ", romajiToHiragana("she").c_str());
    TEST_ASSERT_EQUAL_STRING("ちぇ", romajiToHiragana("che").c_str());
    TEST_ASSERT_EQUAL_STRING("じぇ", romajiToHiragana("je").c_str());
}

static void test_ime_alias_xtsu_ltsu() {
    TEST_ASSERT_EQUAL_STRING("っ", romajiToHiragana("xtsu").c_str());
    TEST_ASSERT_EQUAL_STRING("っ", romajiToHiragana("ltsu").c_str());
    TEST_ASSERT_EQUAL_STRING("まっちゃ", romajiToHiragana("matcha").c_str());
}

static void test_ime_syasin_is_shashin() {
    TEST_ASSERT_EQUAL_STRING("しゃしん", romajiToHiragana("syasin").c_str());
}

static void test_ime_hepburn_forms_unchanged() {
    TEST_ASSERT_EQUAL_STRING(romajiToHiragana("sha").c_str(),
                             romajiToHiragana("sya").c_str());
    TEST_ASSERT_EQUAL_STRING(romajiToHiragana("ja").c_str(),
                             romajiToHiragana("zya").c_str());
    TEST_ASSERT_EQUAL_STRING(romajiToHiragana("ja").c_str(),
                             romajiToHiragana("jya").c_str());
    TEST_ASSERT_EQUAL_STRING(romajiToHiragana("cha").c_str(),
                             romajiToHiragana("cya").c_str());
    TEST_ASSERT_EQUAL_STRING("まっちゃ", romajiToHiragana("matcha").c_str());
    TEST_ASSERT_EQUAL_STRING("こんにちは", romajiToHiragana("konnichiha").c_str());
}

void run_answer_tests() {
    RUN_TEST(test_kanji_rejected_kun_yomi_is_warning);
    RUN_TEST(test_kanji_accepted_non_primary_is_correct);
    RUN_TEST(test_kanji_primary_is_correct);
    RUN_TEST(test_kanji_kunyomi_primary_rejects_onyomi_with_warning);
    RUN_TEST(test_kanji_rejected_reading_names_the_type);

    RUN_TEST(test_n_hint_konya);
    RUN_TEST(test_n_hint_kinyoubi);
    RUN_TEST(test_n_hint_tani);
    RUN_TEST(test_n_hint_kinen);
    RUN_TEST(test_n_hint_genan);
    RUN_TEST(test_n_apostrophe_is_correct);
    RUN_TEST(test_n_hint_message_text);

    RUN_TEST(test_typo_tolerance_boundaries);
    RUN_TEST(test_short_answer_long_input_not_close);
    RUN_TEST(test_long_answer_small_typo_close);
    RUN_TEST(test_two_typos_in_three_letters_not_close);
    RUN_TEST(test_exact_match_unaffected);

    RUN_TEST(test_meaning_field_capitalised_romaji_is_reading_warning);
    RUN_TEST(test_reading_field_capitalised_romaji_is_correct);
    RUN_TEST(test_mixed_case_kana_untouched);
    RUN_TEST(test_uppercase_ascii_meaning_unchanged);

    RUN_TEST(test_ime_alias_sya_row);
    RUN_TEST(test_ime_alias_zya_row);
    RUN_TEST(test_ime_alias_jya_row);
    RUN_TEST(test_ime_alias_cya_row);
    RUN_TEST(test_ime_alias_she_che_je);
    RUN_TEST(test_ime_alias_xtsu_ltsu);
    RUN_TEST(test_ime_syasin_is_shashin);
    RUN_TEST(test_ime_hepburn_forms_unchanged);
}

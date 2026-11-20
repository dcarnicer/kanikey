// REV-10: kana-aware Backspace, answer caps and the Backspace repeat timer.
#include <unity.h>

#include <string>

#include "ui/KeyInput.h"
#include "wk/RomajiIme.h"
#include "wk/Text.h"

using namespace kk;

namespace {

RomajiIme typed(const char* romaji) {
    RomajiIme ime;
    for (const char* p = romaji; *p; ++p) ime.feed(*p);
    return ime;
}

// ------------------------------------------------------------- kana units

static void test_romaji_backspace_ta() {
    RomajiIme ime = typed("ta");
    TEST_ASSERT_EQUAL_STRING("た", ime.text().c_str());
    ime.backspace();
    TEST_ASSERT_TRUE(ime.empty());
    TEST_ASSERT_EQUAL_STRING("", ime.text().c_str());
}

static void test_romaji_backspace_tabe() {
    RomajiIme ime = typed("tabe");
    TEST_ASSERT_EQUAL_STRING("たべ", ime.text().c_str());
    ime.backspace();
    TEST_ASSERT_EQUAL_STRING("た", ime.text().c_str());
    TEST_ASSERT_EQUAL_STRING("ta", ime.raw().c_str());
    ime.backspace();
    TEST_ASSERT_TRUE(ime.empty());
}

static void test_romaji_backspace_n() {
    RomajiIme ime = typed("n");
    TEST_ASSERT_EQUAL_STRING("ん", ime.text().c_str());
    ime.backspace();
    TEST_ASSERT_TRUE(ime.empty());
}

static void test_romaji_backspace_nn() {
    RomajiIme ime = typed("nn");
    TEST_ASSERT_EQUAL_STRING("ん", ime.text().c_str());
    ime.backspace();
    TEST_ASSERT_TRUE(ime.empty());
}

static void test_romaji_backspace_nna() {
    RomajiIme ime = typed("nna");
    TEST_ASSERT_EQUAL_STRING("んな", ime.text().c_str());
    ime.backspace();
    TEST_ASSERT_EQUAL_STRING("ん", ime.text().c_str());
    TEST_ASSERT_EQUAL_STRING("nn", ime.raw().c_str());
    ime.backspace();
    TEST_ASSERT_TRUE(ime.empty());
}

static void test_romaji_backspace_n_apostrophe() {
    RomajiIme ime = typed("n'");
    TEST_ASSERT_EQUAL_STRING("ん", ime.text().c_str());
    ime.backspace();
    TEST_ASSERT_TRUE(ime.empty());
}

static void test_romaji_backspace_kan() {
    RomajiIme ime = typed("kan");
    TEST_ASSERT_EQUAL_STRING("かん", ime.text().c_str());
    ime.backspace();
    TEST_ASSERT_EQUAL_STRING("か", ime.text().c_str());
    TEST_ASSERT_EQUAL_STRING("ka", ime.raw().c_str());
}

static void test_romaji_backspace_kann() {
    RomajiIme ime = typed("kann");
    TEST_ASSERT_EQUAL_STRING("かん", ime.text().c_str());
    ime.backspace();
    TEST_ASSERT_EQUAL_STRING("か", ime.text().c_str());
    TEST_ASSERT_EQUAL_STRING("ka", ime.raw().c_str());
}

static void test_romaji_backspace_n_apostrophe_ya() {
    RomajiIme ime = typed("n'ya");
    TEST_ASSERT_EQUAL_STRING("んや", ime.text().c_str());
    ime.backspace();
    TEST_ASSERT_EQUAL_STRING("ん", ime.text().c_str());
    TEST_ASSERT_EQUAL_STRING("n'", ime.raw().c_str());
}

static void test_romaji_backspace_sokuon_tta() {
    // A trailing doubled consonant is the whole pending っ, so the raw prefix
    // for っ is stable and Backspace can remove just the た.
    TEST_ASSERT_EQUAL_STRING("っ", romajiToHiragana("tt").c_str());
    RomajiIme ime = typed("tta");
    TEST_ASSERT_EQUAL_STRING("った", ime.text().c_str());
    ime.backspace();
    TEST_ASSERT_EQUAL_STRING("っ", ime.text().c_str());
    TEST_ASSERT_EQUAL_STRING("tt", ime.raw().c_str());
    ime.backspace();
    TEST_ASSERT_TRUE(ime.empty());
}

static void test_romaji_backspace_ascii_only() {
    RomajiIme ime = typed("xyz");
    TEST_ASSERT_EQUAL_STRING("xyz", ime.text().c_str());
    ime.backspace();
    TEST_ASSERT_EQUAL_STRING("xy", ime.text().c_str());
    ime.backspace();
    TEST_ASSERT_EQUAL_STRING("x", ime.text().c_str());
    ime.backspace();
    TEST_ASSERT_TRUE(ime.empty());
}

static void test_romaji_backspace_empty_noop() {
    RomajiIme ime;
    ime.backspace();
    TEST_ASSERT_TRUE(ime.empty());
    TEST_ASSERT_EQUAL_STRING("", ime.raw().c_str());
    TEST_ASSERT_EQUAL_STRING("", ime.text().c_str());
}

// ------------------------------------------------------------- answer cap

static void test_answer_cap_64_bytes() {
    TEST_ASSERT_EQUAL_UINT(64, static_cast<unsigned>(kMaxAnswerBytes));
    std::string answer;
    for (int i = 0; i < 100; ++i) appendCapped(answer, 'a', kMaxAnswerBytes);
    TEST_ASSERT_EQUAL_UINT(64, static_cast<unsigned>(answer.size()));
    appendCapped(answer, 'b', kMaxAnswerBytes);
    TEST_ASSERT_EQUAL_UINT(64, static_cast<unsigned>(answer.size()));
    TEST_ASSERT_EQUAL_CHAR('a', answer.back());

    // The reading IME caps its committed kana the same way. A character that
    // would cross the cap is refused, and kana are never split.
    RomajiIme ime;
    for (int i = 0; i < 100; ++i) ime.feedCapped('1', kMaxAnswerBytes);
    TEST_ASSERT_EQUAL_UINT(64, static_cast<unsigned>(ime.text().size()));
    TEST_ASSERT_FALSE(ime.feedCapped('1', kMaxAnswerBytes));
    TEST_ASSERT_EQUAL_UINT(64, static_cast<unsigned>(ime.text().size()));

    RomajiIme kana;
    for (int i = 0; i < 100; ++i) kana.feedCapped('a', kMaxAnswerBytes);
    TEST_ASSERT_TRUE(kana.text().size() <= kMaxAnswerBytes);
    TEST_ASSERT_EQUAL_UINT(63, static_cast<unsigned>(kana.text().size()));
}

static void test_append_capped_stops_at_limit() {
    std::string s = "abc";
    appendCapped(s, 'd', 3);
    TEST_ASSERT_EQUAL_STRING("abc", s.c_str());
    appendCapped(s, 'd', 4);
    TEST_ASSERT_EQUAL_STRING("abcd", s.c_str());
    appendCapped(s, 'e', 4);
    TEST_ASSERT_EQUAL_STRING("abcd", s.c_str());

    std::string over = "abcdef";
    appendCapped(over, 'g', 4);
    TEST_ASSERT_EQUAL_STRING("abcdef", over.c_str());

    std::string none;
    appendCapped(none, 'a', 0);
    TEST_ASSERT_TRUE(none.empty());
}

// ------------------------------------------------------------- repeat

static void test_key_repeat_backspace() {
    TEST_ASSERT_TRUE(tgc::keyRepeats(tgc::Key::Backspace));
    TEST_ASSERT_FALSE(tgc::keyRepeats(tgc::Key::Enter));
    TEST_ASSERT_FALSE(tgc::keyRepeats(tgc::Key::Esc));

    tgc::KeyRepeat repeat;
    repeat.down(tgc::Key::Backspace, 1000);
    TEST_ASSERT_TRUE(repeat.poll(1399) == tgc::Key::None);
    TEST_ASSERT_TRUE(repeat.poll(1400) == tgc::Key::Backspace);
    TEST_ASSERT_TRUE(repeat.poll(1519) == tgc::Key::None);
    TEST_ASSERT_TRUE(repeat.poll(1520) == tgc::Key::Backspace);
    repeat.up(tgc::Key::Backspace);
    TEST_ASSERT_TRUE(repeat.poll(2000) == tgc::Key::None);

    repeat.down(tgc::Key::Backspace, 3000);
    repeat.cancel();
    TEST_ASSERT_TRUE(repeat.poll(4000) == tgc::Key::None);
}

// REV-10 display split: the kana is drawn at 20 px from the pack and the
// unconverted romaji tail in Latin, so `ak` never reaches the pack as `あk`.
void test_pending_romaji_split() {
    PendingRomaji ak = typed("ak").split();
    TEST_ASSERT_EQUAL_STRING("あ", ak.kana.c_str());
    TEST_ASSERT_EQUAL_STRING("k", ak.pending.c_str());

    PendingRomaji aka = typed("aka").split();
    TEST_ASSERT_EQUAL_STRING("あか", aka.kana.c_str());
    TEST_ASSERT_TRUE(aka.pending.empty());

    PendingRomaji ky = typed("ky").split();
    TEST_ASSERT_TRUE(ky.kana.empty());
    TEST_ASSERT_EQUAL_STRING("ky", ky.pending.c_str());

    PendingRomaji sh = typed("sh").split();
    TEST_ASSERT_TRUE(sh.kana.empty());
    TEST_ASSERT_EQUAL_STRING("sh", sh.pending.c_str());

    // Finished syllables leave nothing pending, including n and the sokuon.
    PendingRomaji kan = typed("kan").split();
    TEST_ASSERT_EQUAL_STRING("かん", kan.kana.c_str());
    TEST_ASSERT_TRUE(kan.pending.empty());
    TEST_ASSERT_EQUAL_STRING("ん", typed("n").split().kana.c_str());
    TEST_ASSERT_EQUAL_STRING("っ", typed("tt").split().kana.c_str());
    TEST_ASSERT_TRUE(typed("shi").split().pending.empty());
    TEST_ASSERT_TRUE(typed("").split().kana.empty());
    TEST_ASSERT_TRUE(typed("").split().pending.empty());

    // The helper takes text that is already converted, as the feedback does.
    PendingRomaji direct = splitPendingRomaji("あk");
    TEST_ASSERT_EQUAL_STRING("あ", direct.kana.c_str());
    TEST_ASSERT_EQUAL_STRING("k", direct.pending.c_str());

    // A stray letter in the middle is not a pending tail; it stays with the kana.
    PendingRomaji stray = splitPendingRomaji("kあ");
    TEST_ASSERT_EQUAL_STRING("kあ", stray.kana.c_str());
    TEST_ASSERT_TRUE(stray.pending.empty());
}

}  // namespace

void run_rev10_tests() {
    RUN_TEST(test_pending_romaji_split);
    RUN_TEST(test_romaji_backspace_ta);
    RUN_TEST(test_romaji_backspace_tabe);
    RUN_TEST(test_romaji_backspace_n);
    RUN_TEST(test_romaji_backspace_nn);
    RUN_TEST(test_romaji_backspace_nna);
    RUN_TEST(test_romaji_backspace_n_apostrophe);
    RUN_TEST(test_romaji_backspace_kan);
    RUN_TEST(test_romaji_backspace_kann);
    RUN_TEST(test_romaji_backspace_n_apostrophe_ya);
    RUN_TEST(test_romaji_backspace_sokuon_tta);
    RUN_TEST(test_romaji_backspace_ascii_only);
    RUN_TEST(test_romaji_backspace_empty_noop);
    RUN_TEST(test_answer_cap_64_bytes);
    RUN_TEST(test_append_capped_stops_at_limit);
    RUN_TEST(test_key_repeat_backspace);
}

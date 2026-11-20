// PD-03: the PaperDIY two-button mapping (wk/ButtonNav.h).
#include <unity.h>

#include <initializer_list>

#include "wk/ButtonNav.h"

using namespace tgc;

namespace {

// Drives one button through a list of (level, time) steps and collects every key
// the nav sends, in order. Times are absolute ms.
struct Step {
    NavButton b;
    bool down;
    uint32_t at;
};

void drive(ButtonNav& nav, std::initializer_list<Step> steps, Key* out, int* count) {
    *count = 0;
    for (const Step& s : steps) {
        const Key k = nav.update(s.b, s.down, s.at);
        if (k != Key::None) out[(*count)++] = k;
    }
}

}  // namespace

void test_button_tap_a_is_next() {
    ButtonNav nav;
    Key out[4];
    int n = 0;
    drive(nav, {{NavButton::A, true, 1000}, {NavButton::A, false, 1100}}, out, &n);
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL(static_cast<int>(Key::Down), static_cast<int>(out[0]));
}

void test_button_tap_b_is_back() {
    ButtonNav nav;
    Key out[4];
    int n = 0;
    drive(nav, {{NavButton::B, true, 0}, {NavButton::B, false, 120}}, out, &n);
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL(static_cast<int>(Key::Esc), static_cast<int>(out[0]));
}

void test_button_hold_a_is_confirm_at_threshold() {
    ButtonNav nav;
    // Still down at 599 ms: nothing yet; at 600 ms the hold sends Enter once.
    TEST_ASSERT_EQUAL(static_cast<int>(Key::None),
                      static_cast<int>(nav.update(NavButton::A, true, 0)));
    TEST_ASSERT_EQUAL(static_cast<int>(Key::None),
                      static_cast<int>(nav.update(NavButton::A, true, 599)));
    TEST_ASSERT_EQUAL(static_cast<int>(Key::Enter),
                      static_cast<int>(nav.update(NavButton::A, true, 600)));
    // Release after a hold sends nothing more (no short Down).
    TEST_ASSERT_EQUAL(static_cast<int>(Key::None),
                      static_cast<int>(nav.update(NavButton::A, false, 700)));
}

void test_button_hold_b_is_prev_at_threshold() {
    ButtonNav nav;
    nav.update(NavButton::B, true, 50);
    TEST_ASSERT_EQUAL(static_cast<int>(Key::Up),
                      static_cast<int>(nav.update(NavButton::B, true, 50 + kButtonHoldMs)));
    TEST_ASSERT_EQUAL(static_cast<int>(Key::None),
                      static_cast<int>(nav.update(NavButton::B, false, 2000)));
}

void test_button_just_under_threshold_is_a_tap() {
    ButtonNav nav;
    Key out[4];
    int n = 0;
    drive(nav, {{NavButton::A, true, 0}, {NavButton::A, true, 599}, {NavButton::A, false, 599}},
          out, &n);
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL(static_cast<int>(Key::Down), static_cast<int>(out[0]));
}

void test_button_hold_does_not_repeat() {
    ButtonNav nav;
    int enters = 0;
    for (uint32_t t = 0; t <= 5000; t += 10) {
        if (nav.update(NavButton::A, true, t) == Key::Enter) ++enters;
    }
    TEST_ASSERT_EQUAL(1, enters);
}

void test_button_each_hold_sends_once_per_press() {
    ButtonNav nav;
    Key out[8];
    int n = 0;
    drive(nav,
          {{NavButton::A, true, 0},
           {NavButton::A, true, 700},
           {NavButton::A, false, 800},
           {NavButton::A, true, 900},
           {NavButton::A, true, 1600},
           {NavButton::A, false, 1700}},
          out, &n);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL(static_cast<int>(Key::Enter), static_cast<int>(out[0]));
    TEST_ASSERT_EQUAL(static_cast<int>(Key::Enter), static_cast<int>(out[1]));
}

void test_button_chord_sends_nothing() {
    ButtonNav nav;
    Key out[8];
    int n = 0;
    drive(nav,
          {{NavButton::A, true, 0},
           {NavButton::B, true, 100},
           {NavButton::A, false, 200},
           {NavButton::B, false, 250}},
          out, &n);
    TEST_ASSERT_EQUAL(0, n);
    TEST_ASSERT_FALSE(nav.chord());
}

void test_button_chord_holds_then_resumes() {
    ButtonNav nav;
    Key out[8];
    int n = 0;
    // A chord, then a clean tap of A once both are up.
    drive(nav,
          {{NavButton::A, true, 0},
           {NavButton::B, true, 50},
           {NavButton::A, false, 100},
           {NavButton::B, false, 150},
           {NavButton::A, true, 300},
           {NavButton::A, false, 400}},
          out, &n);
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL(static_cast<int>(Key::Down), static_cast<int>(out[0]));
}

void test_button_chord_blocks_long_hold() {
    ButtonNav nav;
    nav.update(NavButton::A, true, 0);
    nav.update(NavButton::B, true, 10);
    // Still held well past the threshold, but the chord keeps Enter back.
    TEST_ASSERT_EQUAL(static_cast<int>(Key::None),
                      static_cast<int>(nav.update(NavButton::A, true, 2000)));
}

void test_button_mapping_table() {
    TEST_ASSERT_EQUAL(static_cast<int>(Key::Down), static_cast<int>(navShortKey(NavButton::A)));
    TEST_ASSERT_EQUAL(static_cast<int>(Key::Enter), static_cast<int>(navLongKey(NavButton::A)));
    TEST_ASSERT_EQUAL(static_cast<int>(Key::Esc), static_cast<int>(navShortKey(NavButton::B)));
    TEST_ASSERT_EQUAL(static_cast<int>(Key::Up), static_cast<int>(navLongKey(NavButton::B)));
    TEST_ASSERT_EQUAL(600u, kButtonHoldMs);
}

void run_button_tests() {
    RUN_TEST(test_button_tap_a_is_next);
    RUN_TEST(test_button_tap_b_is_back);
    RUN_TEST(test_button_hold_a_is_confirm_at_threshold);
    RUN_TEST(test_button_hold_b_is_prev_at_threshold);
    RUN_TEST(test_button_just_under_threshold_is_a_tap);
    RUN_TEST(test_button_hold_does_not_repeat);
    RUN_TEST(test_button_each_hold_sends_once_per_press);
    RUN_TEST(test_button_chord_sends_nothing);
    RUN_TEST(test_button_chord_holds_then_resumes);
    RUN_TEST(test_button_chord_blocks_long_hold);
    RUN_TEST(test_button_mapping_table);
}

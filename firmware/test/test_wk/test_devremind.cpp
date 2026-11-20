#include <unity.h>

#include <cstdint>
#include <string_view>

#include "wk/DevRemind.h"

using namespace kk;

// `remind <s>` takes whole seconds 1-86400 and nothing else.
static void test_remind_seconds_accepts_range() {
    uint32_t s = 0;
    TEST_ASSERT_TRUE(parseRemindSeconds("1", &s));
    TEST_ASSERT_EQUAL_UINT32(1u, s);
    TEST_ASSERT_TRUE(parseRemindSeconds("30", &s));
    TEST_ASSERT_EQUAL_UINT32(30u, s);
    TEST_ASSERT_TRUE(parseRemindSeconds("86400", &s));
    TEST_ASSERT_EQUAL_UINT32(86400u, s);
}

static void test_remind_seconds_refuses_bad_text() {
    uint32_t s = 7;
    TEST_ASSERT_FALSE(parseRemindSeconds("", &s));
    TEST_ASSERT_FALSE(parseRemindSeconds("0", &s));
    TEST_ASSERT_FALSE(parseRemindSeconds("86401", &s));
    TEST_ASSERT_FALSE(parseRemindSeconds("99999999", &s));
    TEST_ASSERT_FALSE(parseRemindSeconds("-5", &s));
    TEST_ASSERT_FALSE(parseRemindSeconds("12s", &s));
    TEST_ASSERT_FALSE(parseRemindSeconds("1 2", &s));
    TEST_ASSERT_EQUAL_UINT32(7u, s);  // a refused parse leaves the value alone
}

// The four cue names map to the four REM-03 cues; anything else is refused.
static void test_cue_names_map_to_cues() {
    SoundCue cue = SoundCue::AnswerWrong;
    TEST_ASSERT_TRUE(parseCueName("chime", &cue));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SoundCue::ReminderChime),
                            static_cast<uint8_t>(cue));
    TEST_ASSERT_TRUE(parseCueName("correct", &cue));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SoundCue::AnswerCorrect),
                            static_cast<uint8_t>(cue));
    TEST_ASSERT_TRUE(parseCueName("wrong", &cue));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SoundCue::AnswerWrong),
                            static_cast<uint8_t>(cue));
    TEST_ASSERT_TRUE(parseCueName("end", &cue));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SoundCue::SessionEnd),
                            static_cast<uint8_t>(cue));
    TEST_ASSERT_FALSE(parseCueName("Chime", &cue));
    TEST_ASSERT_FALSE(parseCueName("", &cue));
    TEST_ASSERT_FALSE(parseCueName("vol", &cue));
}

// `cue vol` takes the M5 choice values: Low 0, Medium 1, High 2.
static void test_cue_volume_maps_to_choices() {
    int choice = 9;
    TEST_ASSERT_TRUE(parseCueVolume("low", &choice));
    TEST_ASSERT_EQUAL_INT(0, choice);
    TEST_ASSERT_TRUE(parseCueVolume("med", &choice));
    TEST_ASSERT_EQUAL_INT(1, choice);
    TEST_ASSERT_TRUE(parseCueVolume("medium", &choice));
    TEST_ASSERT_EQUAL_INT(1, choice);
    TEST_ASSERT_TRUE(parseCueVolume("high", &choice));
    TEST_ASSERT_EQUAL_INT(2, choice);
    TEST_ASSERT_FALSE(parseCueVolume("loud", &choice));
    TEST_ASSERT_EQUAL_INT(2, choice);
}

// The drift is rebuilt minus expected: positive when the RTC clock ran ahead.
static void test_remind_drift_is_signed() {
    TEST_ASSERT_EQUAL_INT64(0, remindDriftSeconds(1800000030u, 1800000030u));
    TEST_ASSERT_EQUAL_INT64(3, remindDriftSeconds(1800000033u, 1800000030u));
    TEST_ASSERT_EQUAL_INT64(-4, remindDriftSeconds(1800000026u, 1800000030u));
}

// M1 Off becomes 1 for a remind run; a set M1 is kept as it is.
static void test_remind_min_due_off_is_one() {
    TEST_ASSERT_EQUAL_UINT32(1u, devRemindMinDue(0));
    TEST_ASSERT_EQUAL_UINT32(1u, devRemindMinDue(1));
    TEST_ASSERT_EQUAL_UINT32(5u, devRemindMinDue(5));
}

void run_devremind_tests() {
    RUN_TEST(test_remind_seconds_accepts_range);
    RUN_TEST(test_remind_seconds_refuses_bad_text);
    RUN_TEST(test_cue_names_map_to_cues);
    RUN_TEST(test_cue_volume_maps_to_choices);
    RUN_TEST(test_remind_drift_is_signed);
    RUN_TEST(test_remind_min_due_off_is_one);
}

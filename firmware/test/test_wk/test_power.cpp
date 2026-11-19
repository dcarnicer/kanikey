// Idle timer: a sync step may refresh gLastInputAt after App.cpp captured
// `now`, so the age must clamp to zero instead of underflowing into a blank.
#include <unity.h>

#include <cstdint>

#include "wk/IdleTimer.h"

using namespace kk;

static void test_idle_age_clamps_future_timestamp() {
    TEST_ASSERT_EQUAL_UINT32(100, idleAgeMs(1100, 1000));
    TEST_ASSERT_EQUAL_UINT32(0, idleAgeMs(1000, 1000));
    TEST_ASSERT_EQUAL_UINT32(0, idleAgeMs(1000, 1100));
}

static void test_idle_age_wraps_and_respects_limits() {
    TEST_ASSERT_EQUAL_UINT32(21, idleAgeMs(5, 0xFFFFFFF0u));
    TEST_ASSERT_TRUE(idleDue(2000, 1000, 500));
    TEST_ASSERT_FALSE(idleDue(1200, 1000, 500));
    TEST_ASSERT_FALSE(idleDue(1000, 1200, 500));
    TEST_ASSERT_FALSE(idleDue(100000, 0, 0));  // timer disabled
}

void run_power_tests() {
    RUN_TEST(test_idle_age_clamps_future_timestamp);
    RUN_TEST(test_idle_age_wraps_and_respects_limits);
}

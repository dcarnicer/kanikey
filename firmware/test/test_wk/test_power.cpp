// Idle timer: a sync step may refresh gLastInputAt after App.cpp captured
// `now`, so the age must clamp to zero instead of underflowing into a blank.
#include <unity.h>

#include <cstdint>

#include "wk/IdleTimer.h"
#include "wk/SleepPolicy.h"

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

// SPEED-06: P6 Never keeps the old no-deep-sleep rule during a review.
static void test_sleep_policy_never_during_review() {
    TEST_ASSERT_FALSE(reviewMayDeepSleep(SleepDuringReview::Never, 0));
    TEST_ASSERT_FALSE(reviewMayDeepSleep(SleepDuringReview::Never, 3600000));
    TEST_ASSERT_FALSE(deepSleepAllowed(true, SleepDuringReview::Never, 3600000));
}

// SPEED-06: P6 After 10 min starts a clean end at exactly ten idle minutes.
static void test_sleep_policy_after_ten_minutes() {
    TEST_ASSERT_FALSE(reviewMayDeepSleep(SleepDuringReview::After10Min, 0));
    TEST_ASSERT_FALSE(reviewMayDeepSleep(SleepDuringReview::After10Min, 599999));
    TEST_ASSERT_TRUE(reviewMayDeepSleep(SleepDuringReview::After10Min, 600000));
    TEST_ASSERT_TRUE(deepSleepAllowed(true, SleepDuringReview::After10Min, 600000));
    TEST_ASSERT_FALSE(deepSleepAllowed(true, SleepDuringReview::After10Min, 599999));
    TEST_ASSERT_EQUAL(SleepDuringReview::Never, sleepDuringReviewFromValue(0));
    TEST_ASSERT_EQUAL(SleepDuringReview::After10Min, sleepDuringReviewFromValue(600));
    TEST_ASSERT_EQUAL(SleepDuringReview::Never, sleepDuringReviewFromValue(42));
}

// SPEED-06: at home P6 does not matter; only P4 gates the deadline.
static void test_sleep_policy_idle_at_home() {
    TEST_ASSERT_TRUE(deepSleepAllowed(false, SleepDuringReview::Never, 0));
    TEST_ASSERT_TRUE(deepSleepAllowed(false, SleepDuringReview::After10Min, 0));
    TEST_ASSERT_TRUE(deepSleepAllowed(false, SleepDuringReview::Never, 3600000));
}

// SPEED-06: blank 60 s and sleep 120 s leaves 60 s of light sleep at the blank;
// a zero "Sleep after" has no timer (wait for a key).
static void test_light_sleep_slice_to_deep_sleep() {
    TEST_ASSERT_EQUAL_UINT32(60000, lightSleepSliceMs(60000, 60000, 120000));
    TEST_ASSERT_EQUAL_UINT32(0, lightSleepSliceMs(60000, 60000, 0));
    TEST_ASSERT_EQUAL_UINT32(0, lightSleepSliceMs(60000, 0, 120000));
    TEST_ASSERT_EQUAL_UINT32(0, lightSleepSliceMs(120000, 60000, 120000));
    TEST_ASSERT_EQUAL_UINT32(30000, lightSleepSliceMs(90000, 60000, 120000));
    // A timer that wakes exactly on the deadline must not sleep again with no
    // timer: >= is the due test, and 0 disables it.
    TEST_ASSERT_TRUE(sleepDeadlineReached(120000, 120000));
    TEST_ASSERT_FALSE(sleepDeadlineReached(119999, 120000));
    TEST_ASSERT_FALSE(sleepDeadlineReached(120000, 0));
}

// SPEED-06: in a review the timer is P6's deadline, not P4's shorter one; Never
// has no timer at all.
static void test_light_sleep_slice_review_deadline() {
    TEST_ASSERT_EQUAL_UINT32(0, reviewSleepSliceMs(SleepDuringReview::Never, 0));
    TEST_ASSERT_EQUAL_UINT32(540000, reviewSleepSliceMs(SleepDuringReview::After10Min, 60000));
    TEST_ASSERT_EQUAL_UINT32(0, reviewSleepSliceMs(SleepDuringReview::After10Min, 600000));
    TEST_ASSERT_EQUAL_UINT32(540000,
                             lightSleepTimerMs(60000, 60000, 0, true,
                                               SleepDuringReview::After10Min));
    TEST_ASSERT_EQUAL_UINT32(540000,
                             lightSleepTimerMs(60000, 60000, 120000, true,
                                               SleepDuringReview::After10Min));
    TEST_ASSERT_EQUAL_UINT32(0, lightSleepTimerMs(60000, 60000, 120000, true,
                                                  SleepDuringReview::Never));
    TEST_ASSERT_EQUAL_UINT32(60000,
                             lightSleepTimerMs(60000, 60000, 120000, false,
                                               SleepDuringReview::Never));
}

void run_power_tests() {
    RUN_TEST(test_idle_age_clamps_future_timestamp);
    RUN_TEST(test_idle_age_wraps_and_respects_limits);
    RUN_TEST(test_sleep_policy_never_during_review);
    RUN_TEST(test_sleep_policy_after_ten_minutes);
    RUN_TEST(test_sleep_policy_idle_at_home);
    RUN_TEST(test_light_sleep_slice_to_deep_sleep);
    RUN_TEST(test_light_sleep_slice_review_deadline);
}

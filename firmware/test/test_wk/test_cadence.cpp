// PD-04: the PaperDIY sync cadence and the one timer it shares with the REM-02
// reminder. Pure host tests on a fake clock (UTC here, so the local clock equals
// UTC). The intervals and the back-off are proposals (docs/spec/10 §2.5).
#include <unity.h>

#include <cstdint>

#include "wk/PaperCadence.h"
#include "wk/Reminder.h"
#include "wk/Time.h"

using namespace kk;

namespace {

int32_t cdZero(uint32_t) { return 0; }

uint32_t cdUtc(int y, unsigned mo, unsigned d, unsigned h, unsigned mi) {
    return static_cast<uint32_t>(daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60);
}

ReminderRules cdQuietOff() { return reminderRulesFromChoices(0, 0, 0); }
ReminderRules cdQuiet2208() { return reminderRulesFromChoices(0, 1, 0); }  // M2 22-08

CadenceInputs cdIn(uint32_t now, int battery) {
    CadenceInputs in;
    in.nowUtc = now;
    in.batteryPercent = battery;
    return in;
}

CadenceWake cdWake(const CadenceInputs& in) {
    return cadenceNextWake(in, CadenceConfig{}, cdQuietOff(), cdZero);
}

const uint32_t kNow = cdUtc(2026, 11, 20, 9, 0);

}  // namespace

// Battery at or above 50 %: the 30 min interval, and nothing else to wake for.
static void test_cadence_periodic_when_battery_is_high() {
    const CadenceWake w = cdWake(cdIn(kNow, 80));
    TEST_ASSERT_EQUAL_UINT32(kNow + 1800, w.at);
    TEST_ASSERT_TRUE(w.reason == CadenceReason::Periodic);
}

// 20-49 % and an unknown level get the 60 min interval; 50 % is the boundary.
static void test_cadence_interval_by_battery_band() {
    TEST_ASSERT_EQUAL_UINT32(kNow + 1800, cdWake(cdIn(kNow, 50)).at);
    TEST_ASSERT_EQUAL_UINT32(kNow + 3600, cdWake(cdIn(kNow, 49)).at);
    TEST_ASSERT_EQUAL_UINT32(kNow + 3600, cdWake(cdIn(kNow, 20)).at);
    TEST_ASSERT_EQUAL_UINT32(kNow + 3600, cdWake(cdIn(kNow, -1)).at);
}

// Below 20 % there is no sync wake at all; a reminder still wakes (planWake).
static void test_cadence_low_battery_has_no_sync_wake() {
    const CadenceWake w = cdWake(cdIn(kNow, 19));
    TEST_ASSERT_EQUAL_UINT32(0, w.at);
    TEST_ASSERT_TRUE(w.reason == CadenceReason::LowBattery);
    TEST_ASSERT_EQUAL_UINT32(0, cdWake(cdIn(kNow, 0)).at);
}

// Without a trusted clock there is no wall time to schedule against.
static void test_cadence_no_clock_no_wake() {
    const CadenceWake w = cdWake(cdIn(0, 80));
    TEST_ASSERT_EQUAL_UINT32(0, w.at);
    TEST_ASSERT_TRUE(w.reason == CadenceReason::None);
}

// A review bucket that opens before the periodic wake wakes the sync 60 s after
// it opens; a later one does not change the periodic wake.
static void test_cadence_bucket_sooner_than_periodic() {
    CadenceInputs in = cdIn(kNow, 80);
    in.nextReviewUtc = kNow + 1200;
    const CadenceWake w = cdWake(in);
    TEST_ASSERT_EQUAL_UINT32(kNow + 1260, w.at);
    TEST_ASSERT_TRUE(w.reason == CadenceReason::Bucket);

    in.nextReviewUtc = kNow + 7200;
    const CadenceWake later = cdWake(in);
    TEST_ASSERT_EQUAL_UINT32(kNow + 1800, later.at);
    TEST_ASSERT_TRUE(later.reason == CadenceReason::Periodic);
}

// A bucket that has already passed its grace is not a wake target.
static void test_cadence_past_bucket_is_ignored() {
    CadenceInputs in = cdIn(kNow, 80);
    in.nextReviewUtc = kNow - 120;
    TEST_ASSERT_EQUAL_UINT32(kNow + 1800, cdWake(in).at);
}

// The minimum gap: a bucket five minutes away wakes at 15 min, never sooner.
static void test_cadence_min_gap_floors_an_early_bucket() {
    CadenceInputs in = cdIn(kNow, 80);
    in.nextReviewUtc = kNow + 300;
    const CadenceWake w = cdWake(in);
    TEST_ASSERT_EQUAL_UINT32(kNow + 900, w.at);
    TEST_ASSERT_TRUE(w.reason == CadenceReason::Bucket);
}

// Back-off after failed syncs: 15, 30, then 60 min, and 60 min after that.
static void test_cadence_backoff_steps() {
    const CadenceConfig cfg;
    TEST_ASSERT_EQUAL_UINT32(0, cadenceBackoffS(0, cfg));
    TEST_ASSERT_EQUAL_UINT32(900, cadenceBackoffS(1, cfg));
    TEST_ASSERT_EQUAL_UINT32(1800, cadenceBackoffS(2, cfg));
    TEST_ASSERT_EQUAL_UINT32(3600, cadenceBackoffS(3, cfg));
    TEST_ASSERT_EQUAL_UINT32(3600, cadenceBackoffS(7, cfg));
    TEST_ASSERT_EQUAL_UINT32(3600, cadenceBackoffS(0xFFFFFFFFu, cfg));
}

// The back-off holds an early bucket back: after two failures the next wake is
// 30 min out, not the 5 min the bucket asked for.
static void test_cadence_backoff_holds_an_early_bucket() {
    CadenceInputs in = cdIn(kNow, 80);
    in.nextReviewUtc = kNow + 300;
    in.failures = 2;
    TEST_ASSERT_EQUAL_UINT32(kNow + 1800, cdWake(in).at);
    in.failures = 0;
    TEST_ASSERT_EQUAL_UINT32(kNow + 900, cdWake(in).at);
}

// No wake storm: as failures grow, the gap never shrinks, and it never falls
// under the minimum gap.
static void test_cadence_failures_never_storm() {
    uint32_t previous = 0;
    for (uint32_t f = 0; f <= 6; ++f) {
        CadenceInputs in = cdIn(kNow, 80);
        in.nextReviewUtc = kNow + 60;
        in.failures = f;
        const uint32_t gap = cdWake(in).at - kNow;
        TEST_ASSERT_TRUE(gap >= 900);
        TEST_ASSERT_TRUE(gap >= previous);
        previous = gap;
    }
}

// A WaniKani rate-limit wait is a floor too: a longer wait holds the wake, a
// shorter one does not.
static void test_cadence_rate_limit_wait_is_a_floor() {
    CadenceInputs in = cdIn(kNow, 80);
    in.nextReviewUtc = kNow + 300;
    in.retryAfterS = 1200;
    TEST_ASSERT_EQUAL_UINT32(kNow + 1200, cdWake(in).at);
    in.retryAfterS = 600;
    TEST_ASSERT_EQUAL_UINT32(kNow + 900, cdWake(in).at);
}

// Whatever the bucket, the gap to the next sync wake is at least the minimum.
static void test_cadence_min_gap_holds_for_every_bucket_offset() {
    for (uint32_t off = 0; off <= 3600; off += 60) {
        CadenceInputs in = cdIn(kNow, 80);
        in.nextReviewUtc = kNow + off;
        const CadenceWake w = cdWake(in);
        TEST_ASSERT_TRUE(w.at - kNow >= 900);
    }
}

// M2 quiet hours: 23:30 plus the 30 min interval lands at midnight, inside
// 22-08, so the sync waits for 08:00. With M2 off it runs at midnight.
static void test_cadence_quiet_hours_skip_a_night_sync() {
    const uint32_t night = cdUtc(2026, 11, 20, 23, 30);
    const CadenceWake quiet =
        cadenceNextWake(cdIn(night, 80), CadenceConfig{}, cdQuiet2208(), cdZero);
    TEST_ASSERT_EQUAL_UINT32(cdUtc(2026, 11, 21, 8, 0), quiet.at);
    TEST_ASSERT_TRUE(quiet.reason == CadenceReason::Periodic);

    const CadenceWake open = cadenceNextWake(cdIn(night, 80), CadenceConfig{}, cdQuietOff(), cdZero);
    TEST_ASSERT_EQUAL_UINT32(cdUtc(2026, 11, 21, 0, 0), open.at);
}

// The shared helper: a wake inside the quiet span moves to its end; outside it
// is unchanged.
static void test_cadence_skip_quiet_hours_helper() {
    const ReminderRules q = cdQuiet2208();
    TEST_ASSERT_EQUAL_UINT32(cdUtc(2026, 11, 21, 8, 0),
                             skipQuietHours(cdUtc(2026, 11, 20, 23, 30), q, cdZero));
    TEST_ASSERT_EQUAL_UINT32(kNow, skipQuietHours(kNow, q, cdZero));
    TEST_ASSERT_EQUAL_UINT32(cdUtc(2026, 11, 20, 23, 30),
                             skipQuietHours(cdUtc(2026, 11, 20, 23, 30), cdQuietOff(), cdZero));
}

// One timer: the earlier target wins, and a tie goes to the reminder.
static void test_plan_wake_reminder_earlier_wins() {
    CadenceWake cad;
    cad.at = kNow + 1800;
    cad.reason = CadenceReason::Periodic;
    const WakePlan p = planWake(kNow + 600, cad);
    TEST_ASSERT_EQUAL_UINT32(kNow + 600, p.at);
    TEST_ASSERT_TRUE(p.reason == WakeReason::Reminder);
}

static void test_plan_wake_cadence_earlier_wins() {
    CadenceWake cad;
    cad.at = kNow + 1800;
    cad.reason = CadenceReason::Periodic;
    const WakePlan p = planWake(kNow + 3000, cad);
    TEST_ASSERT_EQUAL_UINT32(kNow + 1800, p.at);
    TEST_ASSERT_TRUE(p.reason == WakeReason::Periodic);
}

static void test_plan_wake_tie_goes_to_reminder() {
    CadenceWake cad;
    cad.at = kNow + 1800;
    cad.reason = CadenceReason::Bucket;
    const WakePlan p = planWake(kNow + 1800, cad);
    TEST_ASSERT_EQUAL_UINT32(kNow + 1800, p.at);
    TEST_ASSERT_TRUE(p.reason == WakeReason::Reminder);
}

// Without a reminder the sync wake is the timer, with its own reason.
static void test_plan_wake_cadence_alone() {
    CadenceWake cad;
    cad.at = kNow + 1260;
    cad.reason = CadenceReason::Bucket;
    const WakePlan p = planWake(0, cad);
    TEST_ASSERT_EQUAL_UINT32(kNow + 1260, p.at);
    TEST_ASSERT_TRUE(p.reason == WakeReason::Bucket);
}

// No timer: the reason says why (a low battery, or nothing at all). A reminder
// still wakes through a low battery.
static void test_plan_wake_none_and_low_battery() {
    CadenceWake low;
    low.reason = CadenceReason::LowBattery;
    const WakePlan none = planWake(0, low);
    TEST_ASSERT_EQUAL_UINT32(0, none.at);
    TEST_ASSERT_TRUE(none.reason == WakeReason::LowBattery);

    const WakePlan remOnly = planWake(kNow + 300, low);
    TEST_ASSERT_EQUAL_UINT32(kNow + 300, remOnly.at);
    TEST_ASSERT_TRUE(remOnly.reason == WakeReason::Reminder);

    const WakePlan nothing = planWake(0, CadenceWake{});
    TEST_ASSERT_EQUAL_UINT32(0, nothing.at);
    TEST_ASSERT_TRUE(nothing.reason == WakeReason::None);
}

static void test_plan_wake_reason_names() {
    TEST_ASSERT_EQUAL_STRING("bucket", wakeReasonName(WakeReason::Bucket));
    TEST_ASSERT_EQUAL_STRING("periodic", wakeReasonName(WakeReason::Periodic));
    TEST_ASSERT_EQUAL_STRING("reminder", wakeReasonName(WakeReason::Reminder));
    TEST_ASSERT_EQUAL_STRING("lowbatt", wakeReasonName(WakeReason::LowBattery));
    TEST_ASSERT_EQUAL_STRING("none", wakeReasonName(WakeReason::None));
}

// A timer is due when its target is within the slack; a zero target never is.
static void test_timer_due_slack() {
    const uint32_t target = kNow + 1000;
    TEST_ASSERT_TRUE(timerDue(target, target - 60));
    TEST_ASSERT_FALSE(timerDue(target, target - 61));
    TEST_ASSERT_TRUE(timerDue(target, target + 5));
    TEST_ASSERT_TRUE(timerDue(target, target, 0));
    TEST_ASSERT_FALSE(timerDue(target, target - 1, 0));
    TEST_ASSERT_FALSE(timerDue(0, kNow));
}

void run_cadence_tests() {
    RUN_TEST(test_cadence_periodic_when_battery_is_high);
    RUN_TEST(test_cadence_interval_by_battery_band);
    RUN_TEST(test_cadence_low_battery_has_no_sync_wake);
    RUN_TEST(test_cadence_no_clock_no_wake);
    RUN_TEST(test_cadence_bucket_sooner_than_periodic);
    RUN_TEST(test_cadence_past_bucket_is_ignored);
    RUN_TEST(test_cadence_min_gap_floors_an_early_bucket);
    RUN_TEST(test_cadence_backoff_steps);
    RUN_TEST(test_cadence_backoff_holds_an_early_bucket);
    RUN_TEST(test_cadence_failures_never_storm);
    RUN_TEST(test_cadence_rate_limit_wait_is_a_floor);
    RUN_TEST(test_cadence_min_gap_holds_for_every_bucket_offset);
    RUN_TEST(test_cadence_quiet_hours_skip_a_night_sync);
    RUN_TEST(test_cadence_skip_quiet_hours_helper);
    RUN_TEST(test_plan_wake_reminder_earlier_wins);
    RUN_TEST(test_plan_wake_cadence_earlier_wins);
    RUN_TEST(test_plan_wake_tie_goes_to_reminder);
    RUN_TEST(test_plan_wake_cadence_alone);
    RUN_TEST(test_plan_wake_none_and_low_battery);
    RUN_TEST(test_plan_wake_reason_names);
    RUN_TEST(test_timer_due_slack);
}

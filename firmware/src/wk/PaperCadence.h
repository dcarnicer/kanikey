#pragma once
// PD-04: when the PaperDIY wakes from deep sleep to sync, and the one timer that
// carries that cadence together with the REM-02 reminder. Pure C++ (no Arduino):
// the device passes in the clock, the battery level and the M2 quiet hours, and
// the native tests use a fake clock.
//
// The defaults are proposals (docs/spec/10-bluetooth-paperdiy.md §2.5), not
// measurements:
//   - a sync wake every 30 min at >= 50 % battery, every 60 min at 20-49 % or
//     with an unknown level, and no sync wake below 20 % (reminders still wake);
//   - a review bucket wakes the sync 60 s after it opens, when that is sooner
//     than the interval;
//   - never sooner than 15 min after the last wake; after failed syncs the gap
//     doubles (15, 30, then 60 min) and a WaniKani rate-limit wait is a floor;
//   - M2 quiet hours move a wake inside them to the end of the quiet span, so a
//     night sync is skipped.
#include <cstdint>

#include "wk/Reminder.h"

namespace kk {

struct CadenceConfig {
    uint32_t minGapS = 15 * 60;        // never sooner than this after a wake
    uint32_t periodicHighS = 30 * 60;  // battery >= midPercent
    uint32_t periodicLowS = 60 * 60;   // battery in [lowPercent, midPercent), or unknown
    uint32_t backoffCapS = 60 * 60;    // a failed sync's gap never grows past this
    uint32_t bucketGraceS = 60;        // the server publishes a bucket a little late
    int lowPercent = 20;               // below this: no sync wake, reminders only
    int midPercent = 50;
};

struct CadenceInputs {
    uint32_t nowUtc = 0;         // the wake point (a trusted clock)
    uint32_t nextReviewUtc = 0;  // the next review bucket after nowUtc; 0 = none
    int batteryPercent = -1;     // -1 = unknown
    uint32_t failures = 0;       // consecutive failed sync wakes
    uint32_t retryAfterS = 0;    // a WaniKani rate-limit wait still running
};

enum class CadenceReason : uint8_t { None, Bucket, Periodic, LowBattery };

struct CadenceWake {
    uint32_t at = 0;  // UTC; 0 = no sync wake
    CadenceReason reason = CadenceReason::None;
};

// The gap after `failures` failed syncs in a row: 0 for none, then minGapS
// doubling, never more than backoffCapS.
uint32_t cadenceBackoffS(uint32_t failures, const CadenceConfig& cfg);

// The next sync wake after in.nowUtc, or none (at 0) for no clock or a low
// battery. The wake is the earliest of the periodic interval and the review
// bucket, pushed to the minimum gap or the back-off, then moved past quiet hours.
CadenceWake cadenceNextWake(const CadenceInputs& in, const CadenceConfig& cfg,
                            const ReminderRules& quiet, UtcOffsetFn offsetAt);

enum class WakeReason : uint8_t { None, Bucket, Periodic, Reminder, LowBattery };

struct WakePlan {
    uint32_t at = 0;  // UTC; 0 = no timer
    WakeReason reason = WakeReason::None;
};

// The one timer: the earlier of the reminder wake (0 = none) and the sync wake.
// A tie goes to the reminder, which the user asked for by name.
WakePlan planWake(uint32_t reminderAt, const CadenceWake& cadence);

// The log name of a wake reason: "bucket", "periodic", "reminder", "lowbatt" or
// "none".
const char* wakeReasonName(WakeReason reason);

// True when a timer armed for targetUtc has fired by nowUtc. The slack covers
// the RTC's rounding; a target of 0 is never due.
bool timerDue(uint32_t targetUtc, uint32_t nowUtc, uint32_t slackS = 60);

}  // namespace kk

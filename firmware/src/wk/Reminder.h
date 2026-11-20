#pragma once
// REM-02 reminder schedule: when the device next wakes from deep sleep for a
// reminder, and whether a wake shows the banner. Pure C++ (no Arduino). Local
// time comes in through a UTC-offset callback, so the native tests use a fake
// clock with a DST table and never read the host's tzdata.
#include <cstdint>

#include "wk/Summary.h"

namespace kk {

// Seconds east of UTC in force at `utc` (the device reads its POSIX TZ rule).
using UtcOffsetFn = int32_t (*)(uint32_t utc);

// M1-M3 in the form the schedule needs.
struct ReminderRules {
    uint32_t minDue = 0;       // M1: the review count that reminds; 0 = no bucket wakes
    bool quiet = false;        // M2: no reminders inside the quiet hours
    uint8_t quietStartHour = 22;
    uint8_t quietEndHour = 8;  // the quiet span wraps midnight when start > end
    uint16_t dailyMinute = 0;  // M3: minutes after local midnight; 0 = off
};

// From the stored choice values: M1 is the count (0 = off), M2 is 0 Off, 1 22-08,
// 2 23-07 (anything else is off), M3 is minutes after midnight (0 = off).
ReminderRules reminderRulesFromChoices(int minDue, int quietChoice, int dailyMinutes);

enum class ReminderReason : uint8_t { None, Bucket, Daily };

struct ReminderWake {
    uint32_t at = 0;  // UTC epoch of the next wake; 0 = no timer
    ReminderReason reason = ReminderReason::None;
    bool clamped = false;  // the 24 h cap moved the wake earlier than its reason
};

// The next wake: the earlier of the first review bucket after the wake point and
// the next M3 time, each moved past quiet hours, capped at 24 h after nowUtc.
// The wake point is max(nowUtc, lastFiredAt), so a reminder that fired at T
// never schedules a wake at or before T. No rules on, or nothing in the
// forecast and no daily time: { at 0, None }.
ReminderWake reminderNextWakeAt(const Summary& summary, uint32_t nowUtc, uint32_t lastFiredAt,
                                const ReminderRules& rules, UtcOffsetFn offsetAt);

// The banner: M1 (1 when M1 is off, so a set M3 time still reminds about any
// due review) is met by `dueReviews` and nowUtc is outside quiet hours. False
// when both M1 and M3 are off.
bool reminderWanted(uint32_t dueReviews, uint32_t nowUtc, const ReminderRules& rules,
                    UtcOffsetFn offsetAt);

// True when nowUtc's local wall-clock time falls in the quiet span (M2).
bool inQuietHours(uint32_t nowUtc, const ReminderRules& rules, UtcOffsetFn offsetAt);

// PD-04: `at` itself when it is outside the quiet span (or M2 is off), else the
// first moment after it outside the span. Shared by the reminder and the sync
// cadence, so a night sync waits for the same quiet end as a night reminder.
uint32_t skipQuietHours(uint32_t at, const ReminderRules& rules, UtcOffsetFn offsetAt);

}  // namespace kk

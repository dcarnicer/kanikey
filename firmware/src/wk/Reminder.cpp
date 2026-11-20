#include "wk/Reminder.h"

#include <algorithm>

namespace kk {
namespace {

constexpr int64_t kDay = 86400;
constexpr int64_t kMaxSleepS = 86400;  // the device sleeps at most 24 h

int64_t floorDiv(int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

int64_t offsetOf(int64_t utc, UtcOffsetFn offsetAt) {
    if (utc < 0) utc = 0;
    if (utc > 0xFFFFFFFFll) utc = 0xFFFFFFFFll;
    return offsetAt(static_cast<uint32_t>(utc));
}

// Local wall-clock seconds (as if UTC) for a UTC epoch.
int64_t wallOf(int64_t utc, UtcOffsetFn offsetAt) { return utc + offsetOf(utc, offsetAt); }

// UTC epoch of the local wall time `wall`. The two offsets around that day give
// the two candidates; a candidate is kept when its own offset agrees. Both valid
// (autumn overlap): the earlier one, the first occurrence. Neither valid (spring
// gap): the earlier offset, which normalises like mktime (02:30 becomes 03:30).
int64_t utcFromWall(int64_t wall, UtcOffsetFn offsetAt) {
    const int64_t before = offsetOf(wall - kDay, offsetAt);
    const int64_t after = offsetOf(wall + kDay, offsetAt);
    const int64_t uBefore = wall - before;
    const int64_t uAfter = wall - after;
    const bool validBefore = offsetOf(uBefore, offsetAt) == before;
    const bool validAfter = offsetOf(uAfter, offsetAt) == after;
    if (validBefore && validAfter) return std::min(uBefore, uAfter);
    if (validAfter) return uAfter;
    return uBefore;
}

// Minutes since local midnight for a local wall-clock second count.
int64_t minuteOfDay(int64_t wall) { return (wall - floorDiv(wall, kDay) * kDay) / 60; }

bool quietAtWall(int64_t wall, const ReminderRules& rules) {
    if (!rules.quiet) return false;
    const int64_t start = static_cast<int64_t>(rules.quietStartHour) * 60;
    const int64_t end = static_cast<int64_t>(rules.quietEndHour) * 60;
    const int64_t m = minuteOfDay(wall);
    if (start == end) return false;
    if (start < end) return m >= start && m < end;
    return m >= start || m < end;  // wraps midnight, e.g. 22-08
}

// The first moment at or after `at` outside the quiet span. A wake inside quiet
// hours moves to the quiet end, so the device sleeps through the night.
int64_t skipQuiet(int64_t at, const ReminderRules& rules, UtcOffsetFn offsetAt) {
    const int64_t wall = wallOf(at, offsetAt);
    if (!quietAtWall(wall, rules)) return at;
    const int64_t day0 = floorDiv(wall, kDay);
    for (int64_t k = 0; k < 2; ++k) {
        const int64_t endWall = (day0 + k) * kDay + static_cast<int64_t>(rules.quietEndHour) * 3600;
        if (endWall <= wall) continue;
        const int64_t u = utcFromWall(endWall, offsetAt);
        if (u > at) return u;
    }
    return at;
}

// The first M3 time strictly after `after` (today's or a later day's), 0 when
// none is found in the next two local days.
int64_t dailyAfter(int64_t after, const ReminderRules& rules, UtcOffsetFn offsetAt) {
    const int64_t day0 = floorDiv(wallOf(after, offsetAt), kDay);
    for (int64_t k = 0; k < 3; ++k) {
        const int64_t wall = (day0 + k) * kDay + static_cast<int64_t>(rules.dailyMinute) * 60;
        const int64_t u = utcFromWall(wall, offsetAt);
        if (u > after) return u;
    }
    return 0;
}

}  // namespace

ReminderRules reminderRulesFromChoices(int minDue, int quietChoice, int dailyMinutes) {
    ReminderRules r;
    r.minDue = minDue > 0 ? static_cast<uint32_t>(minDue) : 0;
    if (quietChoice == 1) {
        r.quiet = true;
        r.quietStartHour = 22;
        r.quietEndHour = 8;
    } else if (quietChoice == 2) {
        r.quiet = true;
        r.quietStartHour = 23;
        r.quietEndHour = 7;
    }
    r.dailyMinute = dailyMinutes > 0 && dailyMinutes < 24 * 60 ? static_cast<uint16_t>(dailyMinutes) : 0;
    return r;
}

ReminderWake reminderNextWakeAt(const Summary& summary, uint32_t nowUtc, uint32_t lastFiredAt,
                                const ReminderRules& rules, UtcOffsetFn offsetAt) {
    ReminderWake out;
    if (rules.minDue == 0 && rules.dailyMinute == 0) return out;
    const int64_t boundary = std::max<int64_t>(nowUtc, lastFiredAt);
    int64_t best = 0;
    ReminderReason reason = ReminderReason::None;
    // Bucket first: on a tie the bucket is the reason.
    auto consider = [&](int64_t at, ReminderReason r) {
        if (at > boundary && (best == 0 || at < best)) {
            best = at;
            reason = r;
        }
    };
    if (rules.minDue > 0) {
        const uint32_t bucket = summary.nextReviewAt(static_cast<uint32_t>(boundary));
        if (bucket) consider(skipQuiet(bucket, rules, offsetAt), ReminderReason::Bucket);
    }
    if (rules.dailyMinute > 0) {
        const int64_t daily = dailyAfter(boundary, rules, offsetAt);
        if (daily) consider(skipQuiet(daily, rules, offsetAt), ReminderReason::Daily);
    }
    if (best == 0) return out;
    int64_t cap = static_cast<int64_t>(nowUtc) + kMaxSleepS;
    if (cap <= boundary) cap = boundary + 1;  // only for a fired time in the far future
    if (best > cap) {
        best = cap;
        out.clamped = true;
    }
    out.at = static_cast<uint32_t>(best);
    out.reason = reason;
    return out;
}

bool inQuietHours(uint32_t nowUtc, const ReminderRules& rules, UtcOffsetFn offsetAt) {
    return quietAtWall(wallOf(nowUtc, offsetAt), rules);
}

uint32_t skipQuietHours(uint32_t at, const ReminderRules& rules, UtcOffsetFn offsetAt) {
    return static_cast<uint32_t>(skipQuiet(static_cast<int64_t>(at), rules, offsetAt));
}

bool reminderWanted(uint32_t dueReviews, uint32_t nowUtc, const ReminderRules& rules,
                    UtcOffsetFn offsetAt) {
    if (rules.minDue == 0 && rules.dailyMinute == 0) return false;
    const uint32_t threshold = rules.minDue > 0 ? rules.minDue : 1;
    if (dueReviews < threshold) return false;
    return !inQuietHours(nowUtc, rules, offsetAt);
}

}  // namespace kk

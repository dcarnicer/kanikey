#pragma once
// SPEED-06: P5 "Light sleep" and P6 "Sleep during review" decisions, plus the
// timer math for the light-sleep slice. Pure C++: no Arduino headers, compiled
// by the native tests.
//
// The device blanks the panel after P3 and, when P5 is On, light-sleeps until a
// key or the timer. The timer must not fire before deep sleep is allowed: at
// home that is the P4 "Sleep after" deadline; in a review it is P6's ten
// minutes (P4 does not deep-sleep during a review). A zero return means "no
// timer; wait for a key", not "wake immediately".
#include <cstdint>

namespace kk {

// P6 values: the registry stores the choice's meaning (0 = Never, 600 =
// After 10 min). The policy enum itself is only a mode; the conversion below
// maps 600 to After10Min and every other value to Never.
enum class SleepDuringReview : uint8_t {
    Never = 0,
    After10Min = 1,
};

constexpr uint32_t kReviewSleepMs = 10u * 60u * 1000u;

// Registry value to policy. Anything other than the 600 s choice is Never, the
// default.
SleepDuringReview sleepDuringReviewFromValue(int value);

// True when a review may deep-sleep after idleMs without a key: Never blocks
// it at every age, After10Min allows it from ten minutes on.
bool reviewMayDeepSleep(SleepDuringReview policy, uint32_t idleMs);

// The whole predicate for the deep-sleep condition. A non-review screen is
// controlled by P4 and the caller keeps today's idleDue() check; a review is
// controlled by P6 alone, so P4 = Never still lets "After 10 min" end it.
bool deepSleepAllowed(bool reviewOpen, SleepDuringReview policy, uint32_t idleMs);

// True when the P4 deadline is enabled and idleMs has reached it. The light
// sleep loop uses >=: a timer that wakes exactly on the deadline must run the
// deep-sleep check, not sleep again without a timer.
bool sleepDeadlineReached(uint32_t idleMs, uint32_t sleepMs);

// Milliseconds from idleMs to the P4 deep-sleep deadline, used as the
// light-sleep timer while the panel is blank. 0 when blanking or sleeping is
// disabled, or when the deadline has already passed (the caller then runs the
// deep-sleep check instead of sleeping again).
uint32_t lightSleepSliceMs(uint32_t idleMs, uint32_t blankMs, uint32_t sleepMs);

// Milliseconds from idleMs to P6's ten-minute deadline. 0 for Never and when
// the deadline has already passed.
uint32_t reviewSleepSliceMs(SleepDuringReview policy, uint32_t idleMs);

// The timer for one light sleep: in a review only P6 matters, otherwise the P4
// deadline. 0 = no timer, sleep until a key.
uint32_t lightSleepTimerMs(uint32_t idleMs, uint32_t blankMs, uint32_t sleepMs, bool reviewOpen,
                           SleepDuringReview policy);

}  // namespace kk

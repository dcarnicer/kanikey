#pragma once
// REM-02 device side: the reminder timer across deep sleep. The schedule itself
// is the pure wk/Reminder.h; this file keeps the clock (RTC memory), arms the
// timer before the idle deep sleep and reads M1-M3 from the settings.
//
// PD-04 (PaperDIY): the same timer also carries the sync cadence (wk/PaperCadence.h,
// app/CadenceController.h). It is armed for the earlier of the two targets, and a
// timer wake checks which target is due. The Cardputer never arms a cadence, so
// its timer is the reminder alone, as before.
#include <cstdint>

#include "wk/Summary.h"

namespace kk::app {

// Start of setup(), before anything else: true when this boot is the timer the
// reminder armed in reminderArmIdle() or reminderArmAfterWake(). Clears the arm
// flag, so the next sleep starts fresh, and resets the fired mark, which only
// lives in one boot. *nowUtc gets the UTC time rebuilt from the RTC (no SNTP, no
// time()).
bool reminderBootWake(uint32_t* nowUtc);

// Before the idle deep sleep: the timer in microseconds for the next reminder
// wake, 0 for none (M1 and M3 both off, no trusted clock, or nothing in the
// forecast). With both off this reads no clock and does nothing else, so the
// default sleep is unchanged. `summary` may be null when there is no cache: the
// daily time alone then decides.
uint64_t reminderArmIdle(const kk::Summary* summary);

// Before the deep sleep that follows a timer wake: the same decision at nowUtc,
// the time rebuilt from the RTC. Never time() or effectiveNow() here.
uint64_t reminderArmAfterWake(uint32_t nowUtc, const kk::Summary* summary);

// Banner decision for a timer wake at nowUtc with `dueReviews` due: M1 or the
// M3 time is set and the moment is outside quiet hours.
bool reminderBannerWanted(uint32_t dueReviews, uint32_t nowUtc);

// The banner fired at nowUtc: the next wake must be strictly later.
void reminderNoteFired(uint32_t nowUtc);

// PD-04: after reminderBootWake() returned true, whether the reminder target or
// the sync cadence target that this timer was armed for is due at nowUtc (within
// the RTC slack). A timer that served neither is an early wake.
#if defined(TGC_BOARD_PAPERDIY)
bool bootReminderDue(uint32_t nowUtc);
bool bootCadenceDue(uint32_t nowUtc);
#endif

}  // namespace kk::app

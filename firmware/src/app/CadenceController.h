#pragma once
// PD-04 device side: the PaperDIY sync cadence. The schedule is the pure
// wk/PaperCadence.h. The timer that carries it is ReminderController's, so the
// cadence and the REM-02 reminder share one armed timer. The PaperDIY build only.
#include <cstdint>

#include "wk/PaperCadence.h"
#include "wk/Reminder.h"
#include "wk/Summary.h"

namespace kk::app {

// The sync wake after nowUtc, or none (at 0) when Auto sync on wake (Y1) is Off
// or the device is not set up (no token, no saved Wi-Fi). `quiet` is M2 and
// `offsetAt` the device's local-time rule.
kk::CadenceWake cadenceWakeAfter(uint32_t nowUtc, const kk::Summary* summary,
                                 const kk::ReminderRules& quiet, kk::UtcOffsetFn offsetAt);

// A cadence sync finished at nowUtc. Success clears the back-off; a failure
// counts, and a WaniKani rate-limit wait becomes the next wake's floor.
void cadenceNoteSync(bool ok, uint32_t nowUtc);

// The consecutive failed sync wakes (for the log and the tests of the device).
uint32_t cadenceFailures();

}  // namespace kk::app

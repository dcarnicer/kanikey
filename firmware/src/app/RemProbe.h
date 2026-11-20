// REM-01 dev probe: timer wake, wake cost and tone heap cost. Dev builds only
// (KANIKEY_DEV); the release image has no code from this file.
#pragma once
#ifdef KANIKEY_DEV
#include <Arduino.h>

namespace kk::app {

// Boot hook, called from setup() after firmwareBoot() and before any Wi-Fi,
// storage or sync work. Logs the wake cause (the `[rem] wake cause` line). On a
// repeat run it sleeps again without returning, so probe wakes never start
// Wi-Fi. A key wake, a reset or a power-on ends the run and boots normally.
void remBootProbe();

// Called once the first screen is drawn in setup(): logs `[rem] boot_ms` and,
// when a finished run is pending, the RTC wake history.
void remNoteBootReady();

// Serial `rem`, `rem <seconds> [count]` and `rem tone`. `line` starts with "rem".
void remSerialCommand(const String& line);

}  // namespace kk::app
#endif

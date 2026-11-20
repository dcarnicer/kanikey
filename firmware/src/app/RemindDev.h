// REM-02/REM-03 dev checks: `remind <s>` arms the reminder timer through the real
// wake path and keeps a history in RTC memory; `cue <name>` plays a REM-03 cue on
// the speaker. Dev builds only (KANIKEY_DEV); the release image has no code from
// this file.
#pragma once
#ifdef KANIKEY_DEV
#include <Arduino.h>

namespace kk::app {

// Start of setup(), right after reminderBootWake(). `timerWake` is the wake cause;
// `clockRebuilt` says the RTC rebuilt `nowUtc`. Logs the wake of a `remind` run,
// or ends the run when this boot is a key wake or a reset.
void remindDevBoot(bool timerWake, bool clockRebuilt, uint32_t nowUtc);

// reminderTimerWake(): this wake's due count and whether the banner is shown.
void remindDevWakeDecision(uint32_t due, bool banner);

// The banner ended: `key` true when a key cancelled it (the run ends, normal
// boot), false on the 30 s expiry (the run re-arms before the next sleep).
void remindDevBannerEnd(bool key);

// Called from ReminderController when a timer is armed: `wakeAt` is the UTC of
// the next wake, 0 for none. Records the target of this wake.
void remindDevNoteArm(uint32_t nowUtc, uint32_t wakeAt);

// Serial `remind`, `remind <seconds>`. `line` starts with "remind".
void remindSerialCommand(const String& line);

// Serial `cue <chime|correct|wrong|end>` and `cue vol [low|med|high]`. `line`
// starts with "cue".
void cueSerialCommand(const String& line);

}  // namespace kk::app
#endif

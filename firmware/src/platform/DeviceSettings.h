#pragma once
// Device settings stored in NVS alongside the token: the POSIX time zone rule
// and its display name. The setup portal writes them from the browser.
#include <Arduino.h>

namespace kk {

// Kept as the fallback for devices configured before the setup portal.
constexpr const char* kDefaultTimeZoneRule = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr size_t kTimeZoneRuleMax = 63;
constexpr size_t kTimeZoneNameMax = 47;

// Loads the saved rule (or the default) and applies it with setenv/tzset.
void settingsApplyTimeZone();
String settingsTimeZoneRule();
String settingsTimeZoneName();
// False for an empty/over-long rule or one with characters outside the POSIX
// TZ set. `name` may be empty; it is only for display.
bool settingsSaveTimeZone(const String& rule, const String& name);

// Display and power timings. Brightness is 8..255; the timings are seconds and
// 0 means "never". The defaults match the original hardcoded policy.
int settingsBrightness();
bool settingsSetBrightness(int level);
int settingsDimSeconds();
bool settingsSetDimSeconds(int seconds);
int settingsBlankSeconds();
bool settingsSetBlankSeconds(int seconds);
int settingsSleepSeconds();
bool settingsSetSleepSeconds(int seconds);
// Study preferences: review session size, lesson batch size and whether the
// meaning or the reading is asked first.
int settingsReviewBatchSize();
bool settingsSetReviewBatchSize(int count);
int settingsLessonBatchSize();
bool settingsSetLessonBatchSize(int count);
bool settingsMeaningFirst();
bool settingsSetMeaningFirst(bool meaningFirst);

// "<+01>-1" / "<-0330>3:30": a fixed offset east of UTC, used when the browser
// cannot provide a DST-aware rule.
String formatFixedTimeZone(int offsetMinutes);

}  // namespace kk

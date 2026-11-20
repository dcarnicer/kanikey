#pragma once
// REM-02/REM-03 dev checks: the argument parsers and the drift helper behind the
// serial `remind` and `cue` commands. Pure C++ (no Arduino), so the native tests
// cover them; the device code that calls them is compiled only into dev builds.
#include <cstdint>
#include <string_view>

#include "wk/SoundPlan.h"

namespace kk {

// `remind <s>`: whole seconds, 1-86400, digits only (at most 6 characters).
bool parseRemindSeconds(std::string_view text, uint32_t* seconds);

// `cue <name>`: chime, correct, wrong or end.
bool parseCueName(std::string_view name, SoundCue* cue);

// `cue vol <name>`: low, med (or medium) or high, as the M5 choice values 0, 1, 2.
bool parseCueVolume(std::string_view name, int* volumeChoice);

// The rebuilt clock minus the armed target, in seconds (negative = early).
int64_t remindDriftSeconds(uint32_t rebuiltUtc, uint32_t expectedUtc);

// A `remind` run treats M1 Off as 1, so the banner fires on any due review.
// Any other M1 value is kept.
uint32_t devRemindMinDue(uint32_t minDue);

}  // namespace kk

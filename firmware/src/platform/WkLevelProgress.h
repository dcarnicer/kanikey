#pragma once
// Fetch GET /level_progressions and pick the current level. A missing
// level-progressions permission must not block the sync.
#include <Arduino.h>

#include <cstdint>

#include "wk/LevelProgress.h"

namespace kk {

LevelProgress fetchLevelProgress(const String& token, uint8_t currentLevel);

}  // namespace kk

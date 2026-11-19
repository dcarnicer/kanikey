#pragma once
// Fetch GET /level_progressions and pick the current level. A missing
// level-progressions permission must not block the sync. The two kanji counts
// are separate requests so the sync job can perform one per loop() pass.
#include <Arduino.h>

#include <cstdint>

#include "wk/LevelProgress.h"

namespace kk {

// GET /level_progressions only; no kanji counts.
LevelProgress fetchLevelProgressions(const String& token, uint8_t currentLevel);
// One tiny collection request that reads only total_count. `passed` selects
// the assignments at Guru or above, otherwise every kanji in the level.
// Leaves `out` untouched and returns false on a failed request.
bool fetchLevelKanjiCount(const String& token, uint8_t level, bool passed, uint16_t* out);
// Both counts; on any failure the progress keeps its timestamps and both
// counts stay 0.
bool fetchLevelCounts(const String& token, LevelProgress* progress);

// Blocking wrapper for the existing callers: progression plus both counts.
LevelProgress fetchLevelProgress(const String& token, uint8_t currentLevel);

}  // namespace kk

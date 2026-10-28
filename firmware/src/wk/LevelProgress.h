#pragma once
// The current level's progression from GET /level_progressions: when it
// unlocked/started/passed/completed. Pure C++.
#include <cstdint>

#include "wk/Json.h"

namespace kk {

struct LevelProgress {
    uint8_t level = 0;
    uint32_t unlockedAt = 0;
    uint32_t startedAt = 0;
    uint32_t passedAt = 0;
    uint32_t completedAt = 0;
    bool valid = false;
};

// Picks the entry for `currentLevel`; when it is missing, the highest level in
// the response is used.
bool parseLevelProgressions(const JsonValue& root, uint8_t currentLevel, LevelProgress* out);

}  // namespace kk

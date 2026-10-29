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
    // Kanji in this level and how many reached Guru (stages 5-9). Both stay 0
    // when either count request fails; the timestamps remain usable.
    uint16_t kanjiTotal = 0;
    uint16_t kanjiPassed = 0;
    bool valid = false;
};

// Picks the entry for `currentLevel`; when it is missing, the highest level in
// the response is used.
bool parseLevelProgressions(const JsonValue& root, uint8_t currentLevel, LevelProgress* out);

// Rounded percentage of the level's kanji that reached Guru. Returns 0 when the
// total is unknown and never exceeds 100.
uint8_t levelKanjiPercent(uint16_t passed, uint16_t total);

}  // namespace kk

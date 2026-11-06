#pragma once
// Stats screen: forecast, SRS distribution and level progress, all from cached
// data. The caller owns startWrite()/endWrite() and present().
#include <cstdint>

#include "board/Board.h"
#include "ui/Theme.h"
#include "wk/SrsCounts.h"
#include "wk/Stats.h"

namespace kk::app {

struct StatsLevelView {
    bool valid = false;
    int level = 0;
    char days[16] = "";
    uint16_t kanjiPassed = 0;
    uint16_t kanjiTotal = 0;
    uint8_t kanjiPercent = 0;
    uint16_t needed = 0;
    bool ninetyReached = false;
    uint32_t apprentice = 0;
    bool srsValid = false;
};

struct StatsView {
    uint8_t page = 0;
    uint8_t pages = 3;
    kk::ForecastStats forecast;
    char labels[4][6] = {};
    kk::SrsCounts srs;
    StatsLevelView level;
};

void drawStatsScreen(lgfx::LovyanGFX& d, const StatsView& view);

}  // namespace kk::app

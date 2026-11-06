#pragma once
// Statistics derived from cached data: the 24-hour review forecast and the
// remaining kanji needed for WaniKani's 90% level-up threshold. Pure C++.
#include <cstddef>
#include <cstdint>

#include "wk/Summary.h"

namespace kk {

constexpr size_t kForecastSlots = 24;

struct ForecastStats {
    // Slot 0 is the current local hour; the array covers the next 24 hours.
    uint16_t counts[kForecastSlots] = {};
    uint16_t max = 0;
    uint32_t startHour = 0;
    bool empty() const { return max == 0; }
};

// Fills `out` from the cached summary buckets. Buckets before the current hour
// or outside the 24-hour window are ignored; gaps stay zero.
void buildForecast(const Summary& summary, uint32_t now, ForecastStats* out);

// Kanji still needed to reach ceil(90% of total). Returns 0 when the threshold
// is reached or the total is unknown.
uint16_t kanjiNeededForNinetyPercent(uint16_t passed, uint16_t total);

}  // namespace kk

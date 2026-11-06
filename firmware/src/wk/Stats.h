#pragma once
// Statistics derived from cached data: the 24-hour review forecast, the
// remaining kanji needed for WaniKani's 90% level-up threshold and the local
// review history. Pure C++.
#include <cstddef>
#include <cstdint>

#include "wk/Summary.h"

namespace kk {

constexpr size_t kForecastSlots = 24;
constexpr size_t kHistoryDays = 7;

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

// Local calendar day count since 1970-01-01. `correct` and `incorrect` count
// completed reviews without/with at least one wrong part.
struct DayStats {
    uint32_t dayKey = 0;
    uint16_t reviews = 0;
    uint16_t correct = 0;
    uint16_t incorrect = 0;
};

struct HistoryStats {
    uint16_t counts[kHistoryDays] = {};
    bool any = false;
    bool hasToday = false;
    uint8_t todayAccuracy = 0;
    uint16_t currentStreak = 0;
    uint16_t bestStreak = 0;
};

// `days` may be unsorted or contain duplicate keys; only days with reviews > 0
// count towards the streaks. Day keys are local days, so DST-safe callers
// derive them from local time.
void buildHistory(const DayStats* days, size_t n, uint32_t todayKey, HistoryStats* out);

// Days since 1970-01-01 for a proleptic Gregorian date (month 1-12).
uint32_t civilDayKey(int year, unsigned month, unsigned day);

// Same conversion for a UTC epoch plus a fixed offset east of UTC. Used by the
// native tests; the app derives the key from localtime so DST is included.
uint32_t localDayKeyAtOffset(uint32_t epoch, int32_t offsetSeconds);

}  // namespace kk

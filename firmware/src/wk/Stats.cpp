#include "wk/Stats.h"

#include <algorithm>
#include <vector>

namespace kk {

void buildForecast(const Summary& summary, uint32_t now, ForecastStats* out) {
    if (!out) return;
    *out = ForecastStats{};
    const uint32_t start = (now / 3600u) * 3600u;
    out->startHour = start;
    const uint64_t end = static_cast<uint64_t>(start) + kForecastSlots * 3600ull;
    for (const SummaryBucket& bucket : summary.reviews) {
        if (bucket.count == 0) continue;
        const uint64_t at = bucket.availableAt;
        if (at < start || at >= end) continue;
        const size_t slot = static_cast<size_t>((at - start) / 3600u);
        if (slot >= kForecastSlots) continue;
        const uint32_t sum = static_cast<uint32_t>(out->counts[slot]) + bucket.count;
        out->counts[slot] = static_cast<uint16_t>(std::min<uint32_t>(sum, 0xFFFFu));
        out->max = std::max(out->max, out->counts[slot]);
    }
}

uint16_t kanjiNeededForNinetyPercent(uint16_t passed, uint16_t total) {
    if (total == 0) return 0;
    const uint32_t threshold = (static_cast<uint32_t>(total) * 9u + 9u) / 10u;
    return passed >= threshold ? 0 : static_cast<uint16_t>(threshold - passed);
}

uint32_t civilDayKey(int year, unsigned month, unsigned day) {
    int y = year;
    unsigned m = month;
    y -= m <= 2 ? 1 : 0;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const int mp = static_cast<int>(m) + (m > 2 ? -3 : 9);
    const unsigned doy = (153u * static_cast<unsigned>(mp) + 2u) / 5u + day - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    const int64_t days = static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
    return days < 0 ? 0 : static_cast<uint32_t>(days);
}

uint32_t localDayKeyAtOffset(uint32_t epoch, int32_t offsetSeconds) {
    const int64_t local = static_cast<int64_t>(epoch) + offsetSeconds;
    if (local < 0) return 0;
    return static_cast<uint32_t>(local / 86400);
}

void buildHistory(const DayStats* days, size_t n, uint32_t todayKey, HistoryStats* out) {
    if (!out) return;
    *out = HistoryStats{};
    if (!days || n == 0) return;

    std::vector<DayStats> active;
    active.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (days[i].reviews == 0) continue;
        active.push_back(days[i]);
    }
    if (active.empty()) return;
    out->any = true;
    std::sort(active.begin(), active.end(),
              [](const DayStats& a, const DayStats& b) { return a.dayKey < b.dayKey; });

    // Merge duplicate day keys before walking the streaks.
    size_t unique = 0;
    for (size_t i = 0; i < active.size(); ++i) {
        if (unique > 0 && active[unique - 1].dayKey == active[i].dayKey) {
            DayStats& dst = active[unique - 1];
            dst.reviews = static_cast<uint16_t>(std::min<uint32_t>(
                0xFFFFu, static_cast<uint32_t>(dst.reviews) + active[i].reviews));
            dst.correct = static_cast<uint16_t>(std::min<uint32_t>(
                0xFFFFu, static_cast<uint32_t>(dst.correct) + active[i].correct));
            dst.incorrect = static_cast<uint16_t>(std::min<uint32_t>(
                0xFFFFu, static_cast<uint32_t>(dst.incorrect) + active[i].incorrect));
        } else {
            active[unique++] = active[i];
        }
    }
    active.resize(unique);

    const uint32_t firstKey = todayKey >= kHistoryDays - 1 ? todayKey - (kHistoryDays - 1) : 0;
    for (const DayStats& day : active) {
        if (day.dayKey < firstKey || day.dayKey > todayKey) continue;
        out->counts[day.dayKey - firstKey] =
            static_cast<uint16_t>(std::min<uint32_t>(0xFFFFu, day.reviews));
        if (day.dayKey == todayKey) {
            out->hasToday = true;
            const uint32_t total = static_cast<uint32_t>(day.correct) + day.incorrect;
            if (total)
                out->todayAccuracy = static_cast<uint8_t>(
                    (static_cast<uint64_t>(day.correct) * 100u + total / 2u) / total);
        }
    }

    auto hasDay = [&active](uint32_t key) {
        for (const DayStats& day : active)
            if (day.dayKey == key) return true;
        return false;
    };

    if (todayKey > 0 || out->hasToday) {
        uint32_t cursor = out->hasToday ? todayKey : todayKey - 1;
        while (hasDay(cursor)) {
            ++out->currentStreak;
            if (cursor == 0) break;
            --cursor;
        }
    }

    uint16_t run = 0;
    uint32_t previous = 0;
    bool havePrevious = false;
    for (const DayStats& day : active) {
        if (havePrevious && day.dayKey == previous + 1) {
            ++run;
        } else {
            run = 1;
        }
        previous = day.dayKey;
        havePrevious = true;
        if (run > out->bestStreak) out->bestStreak = run;
    }
}

}  // namespace kk

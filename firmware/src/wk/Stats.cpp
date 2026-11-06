#include "wk/Stats.h"

#include <algorithm>

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

}  // namespace kk

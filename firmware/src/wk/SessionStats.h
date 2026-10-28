#pragma once
// Pure helpers for the session summary: completed parts, mistakes, accuracy
// and the compact elapsed-time text. No Arduino or display dependencies.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "wk/ReviewQueue.h"

namespace kk {

struct SessionStats {
    uint32_t parts = 0;      // required answers in completed items
    uint32_t mistakes = 0;   // wrong attempts across those parts
    uint32_t accuracy = 0;   // percent of answers that were finally correct
};

// Each completed item ends with every required part correct, so accuracy is
// the number of parts divided by the parts plus the wrong attempts.
inline uint32_t sessionAccuracyPercent(uint32_t parts, uint32_t mistakes) {
    const uint64_t total = static_cast<uint64_t>(parts) + mistakes;
    if (total == 0) return 0;
    const uint64_t scaled = static_cast<uint64_t>(parts) * 100u;
    return static_cast<uint32_t>((scaled + total / 2u) / total);
}

inline SessionStats summarizeSession(const std::vector<SessionItem>& completed) {
    SessionStats stats;
    for (const SessionItem& item : completed) {
        stats.parts += static_cast<uint32_t>(item.needsMeaning) +
                       static_cast<uint32_t>(item.needsReading);
        stats.mistakes += item.incorrectMeaning + item.incorrectReading;
    }
    stats.accuracy = sessionAccuracyPercent(stats.parts, stats.mistakes);
    return stats;
}

// 42s, 12m or 1h 5m. Whole seconds/minutes, matching the wall clock the user
// sees; an exact hour is shown as 1h 0m.
inline void formatSessionDuration(uint32_t elapsedMs, char* out, size_t size) {
    if (!out || size == 0) return;
    const uint32_t seconds = elapsedMs / 1000u;
    if (seconds < 60u) {
        snprintf(out, size, "%us", static_cast<unsigned>(seconds));
    } else if (seconds < 3600u) {
        snprintf(out, size, "%um", static_cast<unsigned>(seconds / 60u));
    } else {
        snprintf(out, size, "%uh %um", static_cast<unsigned>(seconds / 3600u),
                 static_cast<unsigned>((seconds / 60u) % 60u));
    }
}

}  // namespace kk

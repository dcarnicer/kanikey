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

// REV-11: one row of the session summary's mistake list. A completed item is
// listed when either part recorded a wrong answer; the counters are the
// archived wrong attempts, not the current queue state.
struct SummaryMistake {
    uint32_t assignmentId = 0;
    uint32_t subjectId = 0;
    uint16_t incorrectMeaning = 0;
    uint16_t incorrectReading = 0;
};

// Completed items in assignment order, without the ones answered right the
// first time.
inline std::vector<SummaryMistake> summaryMistakes(
    const std::vector<SessionItem>& completed) {
    std::vector<SummaryMistake> out;
    for (const SessionItem& item : completed) {
        if (item.incorrectMeaning == 0 && item.incorrectReading == 0) continue;
        SummaryMistake row;
        row.assignmentId = item.assignmentId;
        row.subjectId = item.subjectId;
        row.incorrectMeaning = item.incorrectMeaning;
        row.incorrectReading = item.incorrectReading;
        out.push_back(row);
    }
    return out;
}

// REV-11: compact stage text for a summary row. A pending POST and an unknown
// stage both say `pending`; an unchanged stage shows a single number.
inline void formatSummaryStage(bool valid, uint8_t from, uint8_t to, char* out,
                               size_t size) {
    if (!out || size == 0) return;
    if (!valid || from == 0 || to == 0) {
        snprintf(out, size, "pending");
    } else if (from == to) {
        snprintf(out, size, "SRS %u", static_cast<unsigned>(from));
    } else {
        snprintf(out, size, "SRS %u->%u", static_cast<unsigned>(from),
                 static_cast<unsigned>(to));
    }
}

// REV-11: move the mistake-list selection with wrap-around. An empty list
// keeps the selection at 0; an out-of-range index is clamped first.
inline int summaryMoveSelection(int selected, int count, int delta) {
    if (count <= 0) return 0;
    if (selected < 0) selected = 0;
    if (selected >= count) selected = count - 1;
    int next = (selected + delta) % count;
    if (next < 0) next += count;
    return next;
}

// REV-11: first row of a `visible`-row window that contains the selection.
inline int summaryWindowStart(int selected, int count, int visible) {
    if (count <= 0 || visible <= 0) return 0;
    if (selected < 0) selected = 0;
    if (selected >= count) selected = count - 1;
    const int first = selected - visible + 1;
    return first < 0 ? 0 : first;
}

// REV-11: `R: next batch` exists only while the server summary still reports
// due reviews (and never for a local practice summary; the caller checks).
inline bool summaryCanNextBatch(unsigned dueReviews) { return dueReviews > 0; }

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

#pragma once
// Per-subject review statistics from GET /review_statistics: correct/incorrect
// counts, streaks and WaniKani's overall percentage. They are merged into the
// cached Subject for the review header and the session summary. Pure C++.
#include <cstdint>
#include <vector>

#include "wk/Json.h"
#include "wk/Types.h"

namespace kk {

struct ReviewStatistic {
    uint32_t subjectId = 0;
    uint32_t meaningCorrect = 0;
    uint32_t meaningIncorrect = 0;
    uint32_t readingCorrect = 0;
    uint32_t readingIncorrect = 0;
    uint16_t meaningMaxStreak = 0;
    uint16_t meaningCurrentStreak = 0;
    uint16_t readingMaxStreak = 0;
    uint16_t readingCurrentStreak = 0;
    uint8_t percentageCorrect = 0;
    bool hidden = false;
};

std::vector<ReviewStatistic> parseReviewStatistics(const JsonValue& root);

// Copies the statistics into matching subjects. Hidden statistics are skipped.
// Returns the number of subjects changed.
size_t applyReviewStatistics(const std::vector<ReviewStatistic>& statistics,
                             std::vector<Subject>* subjects);

}  // namespace kk

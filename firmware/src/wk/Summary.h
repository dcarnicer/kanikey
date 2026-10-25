#pragma once
// GET /summary: the review and lesson buckets the Home screen shows. Pure C++.
#include <cstdint>
#include <string>
#include <vector>

#include "wk/Json.h"

namespace kk {

struct SummaryBucket {
    uint32_t availableAt = 0;
    uint16_t count = 0;
    std::vector<uint32_t> subjectIds;
};

struct Summary {
    std::string nextReviewsAt;
    std::vector<SummaryBucket> lessons;
    std::vector<SummaryBucket> reviews;

    uint32_t totalReviews() const;
    uint32_t totalLessons() const;
    uint32_t dueReviews(uint32_t now) const;
    // The earliest review bucket strictly after `now` with at least one item;
    // 0 when there is none.
    uint32_t nextReviewAt(uint32_t now) const;
    // Due subject IDs, oldest bucket first, at most `limit`.
    std::vector<uint32_t> dueSubjectIds(uint32_t now, size_t limit) const;
    std::vector<uint32_t> lessonSubjectIds(size_t limit) const;
};

bool parseSummary(const JsonValue& root, Summary* out);

}  // namespace kk

#pragma once
// Fetch GET /review_statistics for the subjects in a review batch and merge
// the per-subject accuracy/streak fields into those subjects. A missing
// review-statistics permission must not block the batch.
#include <Arduino.h>

#include <cstdint>
#include <vector>

#include "wk/ReviewStatistics.h"
#include "wk/Types.h"

namespace kk {

std::vector<ReviewStatistic> fetchReviewStatistics(const String& token,
                                                   const std::vector<uint32_t>& subjectIds,
                                                   size_t batch = 10);

size_t attachReviewStatistics(const String& token, std::vector<Subject>* subjects);

}  // namespace kk

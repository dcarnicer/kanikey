#pragma once
// Parse the assignment returned by POST /reviews. WaniKani sends the updated
// SRS stage and available_at in resources_updated.assignment. Pure C++.
#include <cstdint>

#include "wk/Json.h"

namespace kk {

struct ReviewUpdate {
    uint32_t assignmentId = 0;
    uint8_t srsStage = 0;
    uint32_t availableAt = 0;  // Unix seconds; 0 = null
    bool valid = false;
};

bool parseReviewUpdate(const JsonValue& root, ReviewUpdate* out);

}  // namespace kk

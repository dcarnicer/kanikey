#pragma once
// The subset of GET /user KaniKey needs. Pure C++.
#include <cstdint>
#include <string>

#include "wk/Json.h"

namespace kk {

enum class ReviewOrder : uint8_t { Shuffled, LowerLevelsFirst };

struct User {
    std::string username;
    int level = 0;
    bool subscriptionActive = false;
    int maxLevelGranted = 3;
    int lessonsBatchSize = 5;
    ReviewOrder reviewsOrder = ReviewOrder::Shuffled;
    uint32_t vacationStartedAt = 0;  // Unix seconds; 0 = not on vacation
};

bool parseUser(const JsonValue& root, User* out);

}  // namespace kk

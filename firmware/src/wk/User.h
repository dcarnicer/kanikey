#pragma once
// The subset of GET /user KaniKey needs. Pure C++.
#include <string>

#include "wk/Json.h"

namespace kk {

struct User {
    std::string username;
    int level = 0;
    bool subscriptionActive = false;
    int maxLevelGranted = 3;
    int lessonsBatchSize = 5;
};

bool parseUser(const JsonValue& root, User* out);

}  // namespace kk

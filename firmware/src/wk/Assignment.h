#pragma once
// Parse GET /assignments into the small assignment records the session uses.
#include <cstdint>
#include <string>
#include <vector>

#include "wk/Json.h"
#include "wk/Types.h"

namespace kk {

SubjectType subjectTypeFromString(std::string_view s);
std::vector<Assignment> parseAssignments(const JsonValue& root);

}  // namespace kk

#pragma once
// Parse GET /assignments into the small assignment records the session uses.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "wk/Json.h"
#include "wk/Types.h"

namespace kk {

SubjectType subjectTypeFromString(std::string_view s);
std::vector<Assignment> parseAssignments(const JsonValue& root);

// Streaming counterpart for large pages: walks the top-level `data` array and
// copies only the assignment fields, without building a JSON DOM. A valid body
// with no data array returns true and an empty vector. On a parse error the
// output is cleared and false is returned.
bool parseAssignmentsStream(std::string_view text, std::vector<Assignment>* out);

}  // namespace kk

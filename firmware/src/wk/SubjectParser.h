#pragma once
// Parse GET /subjects into the core Subject records: characters, meanings,
// readings and auxiliary meanings. Mnemonics, audio and context sentences are
// intentionally ignored in the first review milestone.
#include <string>
#include <vector>

#include "wk/Json.h"
#include "wk/Types.h"

namespace kk {

std::vector<Subject> parseSubjects(const JsonValue& root);

}  // namespace kk

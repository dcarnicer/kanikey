#pragma once
// Parse GET /subjects into the core Subject records: characters, meanings,
// readings and auxiliary meanings. Mnemonics, audio and context sentences are
// intentionally ignored in the first review milestone.
#include <string>
#include <string_view>
#include <vector>

#include "wk/Json.h"
#include "wk/Types.h"

namespace kk {

std::vector<Subject> parseSubjects(const JsonValue& root, bool details = false);

// Streaming counterpart for large pages: builds the same Subject records from
// the top-level `data` array without a JSON DOM. With details=false the
// mnemonic/hint/parts-of-speech/context-sentence fields are ignored, like the
// DOM parser. A valid body with no data array returns true and an empty vector;
// on a parse error the output is cleared and false is returned.
bool parseSubjectsStream(std::string_view text, std::vector<Subject>* out, bool details = false);

}  // namespace kk

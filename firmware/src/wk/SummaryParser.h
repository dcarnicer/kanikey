#pragma once
// Streaming parser for GET /summary. The summary's subject_ids arrays can hold
// hundreds of IDs; building a DOM for them has already caused a bad_alloc on
// the Cardputer. This parser keeps counts and up to `maxIdsPerBucket` IDs per
// bucket (1000 by default: 4 B per id, about 4 KB per bucket).
#include <string_view>

#include "wk/Summary.h"

namespace kk {

// `truncated` (optional) is set when the text stopped inside a JSON value,
// which is what a body cut at the response cap looks like. It is false for a
// structurally malformed body.
bool parseSummaryText(std::string_view text, Summary* out,
                      size_t maxIdsPerBucket = 1000, bool* truncated = nullptr);

}  // namespace kk

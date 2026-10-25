#pragma once
// Streaming parser for GET /summary. The summary's subject_ids arrays can hold
// hundreds of IDs; building a DOM for them has already caused a bad_alloc on
// the Cardputer. This parser keeps counts and at most a few IDs per bucket.
#include <string_view>

#include "wk/Summary.h"

namespace kk {

bool parseSummaryText(std::string_view text, Summary* out,
                      size_t maxIdsPerBucket = 20);

}  // namespace kk

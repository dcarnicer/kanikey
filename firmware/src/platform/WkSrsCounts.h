#pragma once
// Fetch the five SRS distribution buckets from GET /assignments. Each request
// asks for one resource (per_page=1) and only reads the collection's
// total_count, so the payload is a few hundred bytes.
#include <Arduino.h>

#include "wk/SrsCounts.h"

namespace kk {

// Leaves `out` untouched and returns false if any bucket cannot be fetched, so
// a partial response never replaces a complete cached distribution.
bool fetchSrsCounts(const String& token, SrsCounts* out);

}  // namespace kk

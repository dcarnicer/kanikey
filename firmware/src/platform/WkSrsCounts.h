#pragma once
// Fetch the five SRS distribution buckets from GET /assignments. Each request
// asks for one resource (per_page=1) and only reads the collection's
// total_count, so the payload is a few hundred bytes. The sync job fetches one
// bucket per loop() pass.
#include <Arduino.h>

#include <cstdint>

#include "wk/SrsCounts.h"

namespace kk {

// One bucket, 0 = apprentice .. 4 = burned. Leaves `out` untouched and returns
// false on a failed request.
bool fetchSrsBucket(const String& token, uint8_t bucket, uint32_t* out);

// Leaves `out` untouched and returns false if any bucket cannot be fetched, so
// a partial response never replaces a complete cached distribution.
bool fetchSrsCounts(const String& token, SrsCounts* out);

}  // namespace kk

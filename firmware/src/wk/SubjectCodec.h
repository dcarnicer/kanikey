#pragma once
// Standalone subject payload used by the microSD subject catalog. The field
// order is byte-identical to the KKCS subject payload in wk/Cache.cpp; a native
// test compares both encoders so the formats cannot drift.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "wk/Types.h"

namespace kk {

bool subjectEncodePayload(const Subject& subject, std::vector<uint8_t>* out);
bool subjectDecodePayload(const uint8_t* data, size_t size, Subject* out);

}  // namespace kk

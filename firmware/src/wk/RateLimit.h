#pragma once
// WaniKani rate-limit wait calculation. Kept pure so it can be native-tested.
#include <cstdint>

namespace kk {

constexpr uint32_t kRateLimitFallbackSeconds = 60;
constexpr uint32_t kRateLimitMaxSeconds = 120;

// Seconds to wait after a 429. A missing/past reset or an untrusted clock uses
// the fallback; a far-future reset is capped so a bogus header cannot park the
// client indefinitely.
uint32_t rateLimitWaitSeconds(uint32_t resetEpoch, uint32_t nowEpoch, bool clockTrusted);

}  // namespace kk

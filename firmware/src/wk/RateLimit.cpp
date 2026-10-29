#include "wk/RateLimit.h"

namespace kk {

uint32_t rateLimitWaitSeconds(uint32_t resetEpoch, uint32_t nowEpoch, bool clockTrusted) {
    if (!clockTrusted || resetEpoch == 0 || resetEpoch <= nowEpoch)
        return kRateLimitFallbackSeconds;
    const uint32_t wait = resetEpoch - nowEpoch;
    return wait > kRateLimitMaxSeconds ? kRateLimitMaxSeconds : wait;
}

}  // namespace kk

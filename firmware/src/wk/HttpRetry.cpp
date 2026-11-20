#include "wk/HttpRetry.h"

namespace kk {

bool isCapOverflow(size_t bodyLen, size_t cap, bool sawMoreData) {
    return sawMoreData && bodyLen >= cap;
}

bool httpRetryable(int httpCode, bool truncated, bool parsed, bool capOverflow) {
    if (httpCode == 0 || httpCode >= 500) return true;
    return httpCode == 200 && !capOverflow && (truncated || !parsed);
}

}  // namespace kk

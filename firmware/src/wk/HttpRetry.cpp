#include "wk/HttpRetry.h"

namespace kk {

bool httpRetryable(int httpCode, bool truncated, bool parsed) {
    if (httpCode == 0 || httpCode >= 500) return true;
    return httpCode == 200 && (truncated || !parsed);
}

}  // namespace kk

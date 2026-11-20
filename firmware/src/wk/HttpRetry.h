#pragma once
// Retry predicate shared by the JSON/body GET helpers. Kept pure so it can be
// native-tested.

#include <cstddef>

namespace kk {

// A cap overflow: the server had more body than the buffer or the cap let us
// keep. `sawMoreData` is true when the declared length or the stream still had
// bytes past the cap, and `bodyLen` is what was stored when the cap was hit.
// Only a body that reached the cap with data pending is an overflow; a short
// read below the cap is not.
bool isCapOverflow(size_t bodyLen, size_t cap, bool sawMoreData);

// True when another GET can plausibly succeed: transport error (0), server
// error (5xx), or a 200 whose body was truncated or did not parse. Other 4xx
// (including 401/403/429) and non-200 2xx/3xx are final.
//
// A 200 with `capOverflow` is never retried: the same request returns the same
// oversized body, so the retry only doubles the traffic. The caller's page
// fallback (a smaller page) is the recovery for that case.
bool httpRetryable(int httpCode, bool truncated, bool parsed, bool capOverflow = false);

}  // namespace kk

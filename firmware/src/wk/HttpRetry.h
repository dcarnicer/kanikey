#pragma once
// Retry predicate shared by the JSON/body GET helpers. Kept pure so it can be
// native-tested.

namespace kk {

// True when another GET can plausibly succeed: transport error (0), server
// error (5xx), or a 200 whose body was truncated or did not parse. Other 4xx
// (including 401/403/429) and non-200 2xx/3xx are final.
bool httpRetryable(int httpCode, bool truncated, bool parsed);

}  // namespace kk

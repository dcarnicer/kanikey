#include "platform/WkJson.h"

namespace kk {

namespace {
bool retryable(const WkResponse& http, bool parsed) {
    if (http.httpCode == 0 || http.httpCode >= 500) return true;
    return http.httpCode == 200 && (http.truncated || !parsed);
}

void logAttempt(const char* result, const String& path, const WkResponse& http, bool parsed) {
    Serial.printf("[http] %s %s (%d, %u bytes%s%s)\n", result, path.c_str(), http.httpCode,
                  static_cast<unsigned>(http.bytes), http.truncated ? ", truncated" : "",
                  http.httpCode == 200 && !parsed ? ", parse failed" : "");
}

// `reset` releases the previous parse result before the next request. The JSON
// parser deep-copies everything it keeps; clearing here frees that memory for
// the retry and keeps the shared body buffer safe to overwrite. The streaming
// callback is responsible for resetting its own output.
template <typename Reset, typename Parse>
bool getParsed(const String& path, const String& token, size_t maxBodyBytes,
               uint32_t timeoutMs, uint8_t attempts, WkResponse* http, Reset reset,
               Parse parse) {
    if (attempts == 0) attempts = 1;
    bool parsed = false;
    for (uint8_t i = 0; i < attempts; ++i) {
        reset();
        *http = wkGet(path, token, maxBodyBytes, timeoutMs);
        parsed = http->httpCode == 200 && parse(http->body);
        if (!retryable(*http, parsed)) break;
        if (i + 1 < attempts) {
            logAttempt("retry", path, *http, parsed);
            delay(300);
        } else {
            logAttempt("failed", path, *http, parsed);
        }
    }
    return parsed;
}
}  // namespace

WkJsonResult wkGetJsonRetry(const String& path, const String& token, size_t maxBodyBytes,
                            uint32_t timeoutMs, uint8_t attempts) {
    WkJsonResult result;
    result.parsed =
        getParsed(path, token, maxBodyBytes, timeoutMs, attempts, &result.http,
                  [&result]() { result.json = JsonValue{}; },
                  [&result](std::string_view body) {
                      JsonParser p;
                      return p.parse(body, &result.json);
                  });
    return result;
}

WkBodyResult wkGetBodyRetry(const String& path, const String& token, size_t maxBodyBytes,
                            WkBodyParser parse, void* out, uint32_t timeoutMs,
                            uint8_t attempts) {
    WkBodyResult result;
    result.parsed = getParsed(
        path, token, maxBodyBytes, timeoutMs, attempts, &result.http, []() {},
        [parse, out](std::string_view body) { return parse && parse(body, out); });
    return result;
}

}  // namespace kk

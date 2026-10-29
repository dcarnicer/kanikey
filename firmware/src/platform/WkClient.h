#pragma once
// Minimal HTTPS GET for the WaniKani API. One request at a time, with the
// embedded root CAs and a body cap so a bad response cannot exhaust the heap.
#include <Arduino.h>

#include <string_view>

namespace kk {

struct WkResponse {
    bool connected = false;
    int httpCode = 0;
    int tlsError = 0;
    uint32_t ms = 0;
    size_t bytes = 0;  // body bytes
    bool truncated = false;
    String statusLine;
    // 429 details parsed from RateLimit-Reset / RateLimit-Remaining.
    uint32_t retryAfter = 0;         // seconds to wait
    uint32_t rateLimitReset = 0;     // UTC epoch, 0 when absent
    int32_t rateLimitRemaining = -1; // requests left, -1 when absent
    // Points into a static 32 KB buffer owned by WkClient. It stays valid until
    // the next request, which is enough because callers parse it immediately.
    std::string_view body;
};

// Optional callbacks around each TLS request. The UI uses them to free its
// frame buffer before a request and restore it before parsing the response.
using WkMemHook = void (*)();
void wkSetRequestHooks(WkMemHook before, WkMemHook after);

// `path` starts with /v2/. `maxBodyBytes` bounds the String body.
WkResponse wkGet(const String& path, const String& token, size_t maxBodyBytes,
                 uint32_t timeoutMs = 20000);
WkResponse wkPost(const String& path, const String& token, const String& body,
                  size_t maxBodyBytes, uint32_t timeoutMs = 20000);
WkResponse wkPut(const String& path, const String& token, const String& body,
                 size_t maxBodyBytes, uint32_t timeoutMs = 20000);

// Whole seconds left in the shared 429 gate; 0 when requests may go out.
// Safe across the millis() wrap. While the gate is active every request returns
// a synthetic 429 without opening TLS, so callers can show a countdown.
uint32_t wkRateLimitWaitSeconds();

// Shared 401 gate. A real 401 arms it; every request then returns a synthetic
// 401 without opening TLS until setup starts, a new token is saved/validated,
// the dev token is cleared, or the user signs out. A 403 is endpoint-specific
// and does not arm the gate.
bool wkAuthBlocked();
void wkClearAuthBlock();

}  // namespace kk

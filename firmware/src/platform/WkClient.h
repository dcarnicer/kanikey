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
// Same as wkGet, retrying a failed or truncated response a couple of times.
WkResponse wkGetRetry(const String& path, const String& token, size_t maxBodyBytes,
                      uint32_t timeoutMs = 20000, uint8_t attempts = 2);
WkResponse wkPost(const String& path, const String& token, const String& body,
                  size_t maxBodyBytes, uint32_t timeoutMs = 20000);
WkResponse wkPut(const String& path, const String& token, const String& body,
                 size_t maxBodyBytes, uint32_t timeoutMs = 20000);

}  // namespace kk

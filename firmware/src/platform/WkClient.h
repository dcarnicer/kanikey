#pragma once
// Minimal HTTPS GET for the WaniKani API. One request at a time, with the
// embedded root CAs and a body cap so a bad response cannot exhaust the heap.
#include <Arduino.h>

namespace kk {

struct WkResponse {
    bool connected = false;
    int httpCode = 0;
    int tlsError = 0;
    uint32_t ms = 0;
    size_t bytes = 0;  // body bytes
    bool truncated = false;
    String statusLine;
    String body;
};

// `path` starts with /v2/. `maxBodyBytes` bounds the String body.
WkResponse wkGet(const String& path, const String& token, size_t maxBodyBytes,
                 uint32_t timeoutMs = 20000);
WkResponse wkPost(const String& path, const String& token, const String& body,
                  size_t maxBodyBytes, uint32_t timeoutMs = 20000);

}  // namespace kk

#pragma once
// A tiny HTTPS probe for the first hardware milestone: GET /v2/user with no
// token, so a 401 proves DNS, TLS and the WaniKani endpoint while measuring the
// heap the secure client costs.
#include <Arduino.h>

namespace kk {

struct WkProbeResult {
    bool connected = false;  // TCP/TLS connection established
    bool tlsOk = false;      // certificate verified
    int httpCode = 0;        // 0 when no HTTP response was read
    int tlsError = 0;
    uint32_t ms = 0;
    size_t bytes = 0;
    String statusLine;
    String username;
    int level = 0;
};

// With an empty token the request has no Authorization header, so a 401 still
// proves DNS, TLS and the endpoint. With a token, a 200 also proves the token
// and returns username/level when they can be extracted.
WkProbeResult probeWaniKaniUser(const String& token = String());

}  // namespace kk

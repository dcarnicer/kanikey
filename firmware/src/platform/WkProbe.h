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
};

WkProbeResult probeWaniKaniUser();

}  // namespace kk

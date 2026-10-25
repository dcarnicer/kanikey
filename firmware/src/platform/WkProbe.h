#pragma once
// WaniKani probes used by the first milestones: GET /user and GET /summary.
#include <Arduino.h>

#include "wk/Summary.h"
#include "wk/User.h"

namespace kk {

struct WkProbeResult {
    bool connected = false;
    int httpCode = 0;
    int tlsError = 0;
    uint32_t ms = 0;
    size_t bytes = 0;
    bool parsed = false;
    String statusLine;
    User user;
};

struct SummaryProbeResult {
    bool connected = false;
    int httpCode = 0;
    int tlsError = 0;
    uint32_t ms = 0;
    size_t bytes = 0;
    bool parsed = false;
    Summary summary;
};

WkProbeResult probeWaniKaniUser(const String& token);
SummaryProbeResult probeWaniKaniSummary(const String& token);

}  // namespace kk

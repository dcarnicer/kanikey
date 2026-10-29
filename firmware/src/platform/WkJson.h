#pragma once
// GET helpers that run a parser on each 200 body before the next request.
// A truncated or unparsable 200 is retried, not just a transport error.
#include <Arduino.h>

#include <string_view>

#include "platform/WkClient.h"
#include "wk/Json.h"

namespace kk {

struct WkJsonResult {
    WkResponse http;
    JsonValue json;
    bool parsed = false;
};

// Each attempt calls wkGet and parses a 200 body before the next request.
// Retries a truncated/unparsable 200, httpCode 0 and 5xx; never retries other
// 4xx or 429. `parsed` describes the last attempt and can stay true for a
// truncated body that still parsed. The previous DOM is released before each
// request; the parser itself deep-copies whatever it keeps.
WkJsonResult wkGetJsonRetry(const String& path, const String& token, size_t maxBodyBytes,
                            uint32_t timeoutMs = 12000, uint8_t attempts = 2);

struct WkBodyResult {
    WkResponse http;
    bool parsed = false;
};

// Streaming counterpart for responses too large for a DOM (/summary). `parse`
// runs on each 200 body before the next request, must copy whatever it keeps,
// and is responsible for resetting `out` before the next attempt. It gets the
// same retry policy as wkGetJsonRetry.
using WkBodyParser = bool (*)(std::string_view body, void* out);
WkBodyResult wkGetBodyRetry(const String& path, const String& token, size_t maxBodyBytes,
                            WkBodyParser parse, void* out, uint32_t timeoutMs = 12000,
                            uint8_t attempts = 2);

}  // namespace kk

#pragma once
// GET helpers that run a parser on each 200 body before the next request.
// A truncated or unparsable 200 is retried, not just a transport error.
#include <Arduino.h>

#include <string_view>
#include <vector>

#include "platform/WkClient.h"
#include "wk/Assignment.h"
#include "wk/Json.h"
#include "wk/SubjectParser.h"

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

// Large-collection counterparts: run the streaming sinks on each 200 body and
// return the domain vector. `reserve` only sizes the initial growth. The retry
// policy, body cap and static response buffer are the same as wkGetJsonRetry.
struct WkAssignmentsResult {
    WkResponse http;
    bool parsed = false;
    std::vector<Assignment> assignments;
};
WkAssignmentsResult wkGetAssignmentsRetry(const String& path, const String& token,
                                          size_t maxBodyBytes, size_t reserve = 0,
                                          uint32_t timeoutMs = 12000, uint8_t attempts = 2);

struct WkSubjectsResult {
    WkResponse http;
    bool parsed = false;
    std::vector<Subject> subjects;
};
WkSubjectsResult wkGetSubjectsRetry(const String& path, const String& token, size_t maxBodyBytes,
                                    bool details = true, uint32_t timeoutMs = 12000,
                                    uint8_t attempts = 2);

}  // namespace kk

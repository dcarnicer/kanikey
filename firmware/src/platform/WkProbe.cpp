#include "platform/WkProbe.h"

#include <esp_heap_caps.h>

#include <string_view>

#include "platform/WkClient.h"
#include "platform/WkJson.h"
#include "wk/SummaryParser.h"

namespace kk {

namespace {
constexpr size_t kSummaryBodyBytes = 32768;
// A summary body larger than the shared buffer is retried once in a larger
// heap buffer, but only when there is also room for the next TLS handshake.
constexpr size_t kSummaryLargeBodyBytes = 65536;
constexpr size_t kSummaryLargeReserve = 48 * 1024;

bool parseSummaryBody(std::string_view body, void* out) {
    return parseSummaryText(body, static_cast<Summary*>(out), 1000);
}
}  // namespace

WkProbeResult probeWaniKaniUser(const String& token) {
    WkProbeResult r;
    const WkJsonResult res = wkGetJsonRetry("/v2/user", token, 4096, 20000);
    r.connected = res.http.connected;
    r.httpCode = res.http.httpCode;
    r.tlsError = res.http.tlsError;
    r.ms = res.http.ms;
    r.bytes = res.http.bytes;
    r.statusLine = res.http.statusLine;
    if (res.http.httpCode == 200) {
        if (res.parsed) r.parsed = parseUser(res.json, &r.user);
        if (r.parsed)
            Serial.printf("[user] %s level %d, max level %d, batch %d, order %s\n",
                          r.user.username.c_str(), r.user.level, r.user.maxLevelGranted,
                          r.user.lessonsBatchSize,
                          r.user.reviewsOrder == ReviewOrder::LowerLevelsFirst
                              ? "lower_levels_first"
                              : "shuffled");
        else
            Serial.println("[user] 200 but the JSON did not parse");
    }
    return r;
}

SummaryProbeResult probeWaniKaniSummary(const String& token) {
    SummaryProbeResult r;
    const WkBodyResult res =
        wkGetBodyRetry("/v2/summary", token, kSummaryBodyBytes, parseSummaryBody, &r.summary,
                       20000);
    WkResponse http = res.http;
    bool parsed = res.parsed && !res.http.truncated;
    Serial.printf("[summary] body %u bytes%s\n", static_cast<unsigned>(http.bytes),
                  http.truncated ? " (truncated)" : "");
    if (http.truncated) {
        const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        if (largest >= kSummaryLargeBodyBytes + kSummaryLargeReserve) {
            char* buffer =
                static_cast<char*>(heap_caps_malloc(kSummaryLargeBodyBytes, MALLOC_CAP_INTERNAL));
            if (buffer) {
                Serial.printf("[summary] retrying with a %u-byte cap (%u largest)\n",
                              static_cast<unsigned>(kSummaryLargeBodyBytes),
                              static_cast<unsigned>(largest));
                WkResponse bigger = wkGetToBuffer("/v2/summary", token, buffer,
                                                  kSummaryLargeBodyBytes,
                                                  kSummaryLargeBodyBytes, 20000);
                parsed = bigger.httpCode == 200 && !bigger.truncated &&
                         parseSummaryBody(bigger.body, &r.summary);
                Serial.printf("[summary] large body %u bytes%s\n",
                              static_cast<unsigned>(bigger.bytes),
                              bigger.truncated ? " (truncated)" : "");
                http = bigger;
                heap_caps_free(buffer);
            } else {
                parsed = false;
            }
        } else {
            parsed = false;
            Serial.printf("[summary] truncated at %u bytes, no room for a larger cap "
                          "(%u largest)\n",
                          static_cast<unsigned>(http.bytes), static_cast<unsigned>(largest));
        }
    }
    r.connected = http.connected;
    r.httpCode = http.httpCode;
    r.tlsError = http.tlsError;
    r.ms = http.ms;
    r.bytes = http.bytes;
    if (r.httpCode == 200) {
        r.parsed = parsed;
        if (r.parsed)
            Serial.printf("[summary] %u reviews, %u lessons, next %s\n",
                          static_cast<unsigned>(r.summary.totalReviews()),
                          static_cast<unsigned>(r.summary.totalLessons()),
                          r.summary.nextReviewsAt.c_str());
        else
            Serial.println("[summary] 200 but the JSON did not parse");
    }
    return r;
}

}  // namespace kk

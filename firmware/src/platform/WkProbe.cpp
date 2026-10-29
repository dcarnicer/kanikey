#include "platform/WkProbe.h"

#include <string_view>

#include "platform/WkClient.h"
#include "platform/WkJson.h"
#include "wk/SummaryParser.h"

namespace kk {

namespace {
bool parseSummaryBody(std::string_view body, void* out) {
    return parseSummaryText(body, static_cast<Summary*>(out));
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
        wkGetBodyRetry("/v2/summary", token, 32768, parseSummaryBody, &r.summary, 20000);
    r.connected = res.http.connected;
    r.httpCode = res.http.httpCode;
    r.tlsError = res.http.tlsError;
    r.ms = res.http.ms;
    r.bytes = res.http.bytes;
    if (res.http.httpCode == 200) {
        r.parsed = res.parsed;
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

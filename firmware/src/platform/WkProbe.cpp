#include "platform/WkProbe.h"

#include <string_view>

#include "platform/WkClient.h"
#include "wk/Json.h"
#include "wk/SummaryParser.h"

namespace kk {

namespace {
bool parseBody(const String& body, JsonValue* out) {
    JsonParser p;
    return p.parse(std::string_view(body.c_str(), body.length()), out);
}
}  // namespace

WkProbeResult probeWaniKaniUser(const String& token) {
    WkProbeResult r;
    const WkResponse http = wkGet("/v2/user", token, 4096);
    r.connected = http.connected;
    r.httpCode = http.httpCode;
    r.tlsError = http.tlsError;
    r.ms = http.ms;
    r.bytes = http.bytes;
    r.statusLine = http.statusLine;
    if (http.httpCode == 200) {
        JsonValue v;
        if (parseBody(http.body, &v)) r.parsed = parseUser(v, &r.user);
        if (r.parsed)
            Serial.printf("[user] %s level %d, max level %d, batch %d\n",
                          r.user.username.c_str(), r.user.level, r.user.maxLevelGranted,
                          r.user.lessonsBatchSize);
        else
            Serial.println("[user] 200 but the JSON did not parse");
    }
    return r;
}

SummaryProbeResult probeWaniKaniSummary(const String& token) {
    SummaryProbeResult r;
    const WkResponse http = wkGet("/v2/summary", token, 32768);
    r.connected = http.connected;
    r.httpCode = http.httpCode;
    r.tlsError = http.tlsError;
    r.ms = http.ms;
    r.bytes = http.bytes;
    if (http.httpCode == 200) {
        r.parsed = parseSummaryText(std::string_view(http.body.c_str(), http.body.length()),
                                    &r.summary);
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

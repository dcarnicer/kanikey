#include "platform/WkSrsCounts.h"

#include <string_view>

#include "platform/WkClient.h"
#include "wk/Json.h"

namespace kk {

namespace {
// `per_page` is not a WaniKani filter: the server always pages at up to 500
// assignments. `page_after_id` past the end asks for an empty data array while
// total_count still reports the full filtered collection.
constexpr const char* kBucketPaths[5] = {
    "/v2/assignments?srs_stages=1,2,3,4&hidden=false&page_after_id=999999999",
    "/v2/assignments?srs_stages=5,6&hidden=false&page_after_id=999999999",
    "/v2/assignments?srs_stages=7&hidden=false&page_after_id=999999999",
    "/v2/assignments?srs_stages=8&hidden=false&page_after_id=999999999",
    "/v2/assignments?srs_stages=9&hidden=false&page_after_id=999999999",
};

bool fetchTotal(const String& token, const char* path, uint32_t* out) {
    const WkResponse r = wkGetRetry(path, token, 4096, 12000);
    if (r.httpCode != 200) {
        Serial.printf("[srs] %s -> HTTP %d\n", path, r.httpCode);
        return false;
    }
    JsonParser p;
    JsonValue v;
    if (!p.parse(std::string_view(r.body.data(), r.body.size()), &v)) {
        Serial.printf("[srs] JSON failed: len=%u trunc=%d\n", static_cast<unsigned>(r.body.size()),
                      r.truncated ? 1 : 0);
        return false;
    }
    if (!parseCollectionTotal(v, out)) {
        Serial.printf("[srs] %s has no total_count\n", path);
        return false;
    }
    return true;
}
}  // namespace

bool fetchSrsCounts(const String& token, SrsCounts* out) {
    if (!out) return false;
    uint32_t values[5] = {};
    for (size_t i = 0; i < 5; ++i) {
        if (!fetchTotal(token, kBucketPaths[i], &values[i])) return false;
    }
    SrsCounts counts;
    counts.apprentice = values[0];
    counts.guru = values[1];
    counts.master = values[2];
    counts.enlightened = values[3];
    counts.burned = values[4];
    counts.valid = true;
    Serial.printf("[srs] A %u, G %u, M %u, E %u, B %u\n",
                  static_cast<unsigned>(counts.apprentice), static_cast<unsigned>(counts.guru),
                  static_cast<unsigned>(counts.master),
                  static_cast<unsigned>(counts.enlightened),
                  static_cast<unsigned>(counts.burned));
    *out = counts;
    return true;
}

}  // namespace kk

#include "platform/WkReviewStatistics.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "platform/WkClient.h"
#include "wk/Json.h"

namespace kk {

namespace {
String idList(const std::vector<uint32_t>& ids, size_t begin, size_t end) {
    String s;
    for (size_t i = begin; i < end; ++i) {
        if (i > begin) s += ',';
        s += String(static_cast<unsigned long>(ids[i]));
    }
    return s;
}

bool parseJson(std::string_view body, JsonValue* out) {
    JsonParser p;
    return p.parse(body, out);
}
}  // namespace

std::vector<ReviewStatistic> fetchReviewStatistics(const String& token,
                                                   const std::vector<uint32_t>& subjectIds,
                                                   size_t batch) {
    std::vector<ReviewStatistic> out;
    if (subjectIds.empty() || batch == 0) return out;
    for (size_t begin = 0; begin < subjectIds.size(); begin += batch) {
        const size_t end = std::min(subjectIds.size(), begin + batch);
        const String path = "/v2/review_statistics?subject_ids=" + idList(subjectIds, begin, end);
        const WkResponse r = wkGetRetry(path, token, 32768, 12000);
        if (r.httpCode != 200) {
            Serial.printf("[stats] HTTP %d for %u ids\n", r.httpCode,
                          static_cast<unsigned>(end - begin));
            if (r.httpCode == 401 || r.httpCode == 403) {
                Serial.println("[stats] continuing without review statistics");
                break;
            }
            if (r.httpCode == 429) {
                Serial.println("[stats] rate limited; stopping batches");
                break;
            }
            continue;
        }
        JsonValue v;
        if (!parseJson(r.body, &v)) {
            Serial.printf("[stats] JSON failed: len=%u trunc=%d\n",
                          static_cast<unsigned>(r.body.size()), r.truncated ? 1 : 0);
            continue;
        }
        std::vector<ReviewStatistic> part = parseReviewStatistics(v);
        v = JsonValue{};
        Serial.printf("[stats] %u statistics for %u ids\n", static_cast<unsigned>(part.size()),
                      static_cast<unsigned>(end - begin));
        for (ReviewStatistic& stat : part) out.push_back(std::move(stat));
    }
    return out;
}

size_t attachReviewStatistics(const String& token, std::vector<Subject>* subjects) {
    if (!subjects || subjects->empty()) return 0;
    std::vector<uint32_t> ids;
    ids.reserve(subjects->size());
    for (const Subject& s : *subjects)
        if (std::find(ids.begin(), ids.end(), s.id) == ids.end()) ids.push_back(s.id);
    const std::vector<ReviewStatistic> statistics = fetchReviewStatistics(token, ids);
    const size_t applied = applyReviewStatistics(statistics, subjects);
    Serial.printf("[stats] %u applied to subjects\n", static_cast<unsigned>(applied));
    return applied;
}

}  // namespace kk

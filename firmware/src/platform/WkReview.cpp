#include "platform/WkReview.h"

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

bool parseJson(const String& body, JsonValue* out) {
    JsonParser p;
    return p.parse(std::string_view(body.c_str(), body.length()), out);
}
}  // namespace

ReviewBatch fetchReviewBatch(const String& token, const std::vector<uint32_t>& dueSubjectIds,
                             uint32_t now, size_t limit) {
    ReviewBatch batch;
    if (dueSubjectIds.empty()) {
        batch.error = "no due subject ids";
        return batch;
    }
    const size_t want = std::min(limit, dueSubjectIds.size());
    String path = "/v2/assignments?subject_ids=" + idList(dueSubjectIds, 0, want);
    Serial.printf("[review] assignments for %u subjects\n", static_cast<unsigned>(want));
    const WkResponse ar = wkGet(path, token, 32768, 12000);
    if (ar.httpCode != 200) {
        batch.error = "assignments HTTP " + String(ar.httpCode);
        return batch;
    }
    JsonValue av;
    if (!parseJson(ar.body, &av)) {
        batch.error = "assignments JSON";
        return batch;
    }
    const std::vector<Assignment> parsed = parseAssignments(av);
    for (const Assignment& a : parsed) {
        if (a.hidden || a.srsStage < 1 || a.srsStage > 8) continue;
        if (a.availableAt != 0 && a.availableAt > now) continue;
        batch.assignments.push_back(a);
        if (batch.assignments.size() >= want) break;
    }
    if (batch.assignments.empty()) {
        batch.error = "no due assignments";
        return batch;
    }

    std::vector<uint32_t> ids;
    for (const Assignment& a : batch.assignments)
        if (std::find(ids.begin(), ids.end(), a.subjectId) == ids.end())
            ids.push_back(a.subjectId);

    constexpr size_t kSubjectBatch = 5;
    for (size_t begin = 0; begin < ids.size(); begin += kSubjectBatch) {
        const size_t end = std::min(ids.size(), begin + kSubjectBatch);
        String sp = "/v2/subjects?ids=" + idList(ids, begin, end);
        const WkResponse sr = wkGet(sp, token, 32768, 12000);
        if (sr.httpCode != 200) {
            batch.error = "subjects HTTP " + String(sr.httpCode);
            return batch;
        }
        JsonValue sv;
        if (!parseJson(sr.body, &sv)) {
            batch.error = "subjects JSON";
            return batch;
        }
        std::vector<Subject> part = parseSubjects(sv);
        for (Subject& s : part) batch.subjects.push_back(std::move(s));
        Serial.printf("[review] subjects %u/%u, total %u\n", static_cast<unsigned>(end),
                      static_cast<unsigned>(ids.size()),
                      static_cast<unsigned>(batch.subjects.size()));
    }

    batch.fetchedAt = now;
    batch.ok = !batch.subjects.empty();
    if (!batch.ok) batch.error = "no subjects";
    return batch;
}

}  // namespace kk

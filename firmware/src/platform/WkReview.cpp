#include "platform/WkReview.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "platform/WkClient.h"
#include "platform/WkStudyMaterials.h"
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

ReviewBatch fetchReviewBatch(const String& token, const std::vector<uint32_t>& dueSubjectIds,
                             uint32_t now, size_t limit) {
    ReviewBatch batch;
    if (dueSubjectIds.empty()) {
        batch.error = "no due subject ids";
        return batch;
    }
    const size_t want = std::min(limit, dueSubjectIds.size());
    // Reserve before the subject DOMs start fragmenting the heap: growing this
    // vector while a 15 KB subject page is alive can throw bad_alloc.
    batch.assignments.reserve(want);
    batch.subjects.reserve(want);
    String path = "/v2/assignments?subject_ids=" + idList(dueSubjectIds, 0, want);
    Serial.printf("[review] assignments for %u subjects\n", static_cast<unsigned>(want));
    const WkResponse ar = wkGetRetry(path, token, 32768, 12000);
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
    av = JsonValue{};  // the assignment DOM is not needed while subjects load
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
        const WkResponse sr = wkGetRetry(sp, token, 32768, 12000);
        if (sr.httpCode != 200) {
            batch.error = "subjects HTTP " + String(sr.httpCode);
            return batch;
        }
        JsonValue sv;
        if (!parseJson(sr.body, &sv)) {
            const size_t n = sr.body.size();
            Serial.printf("[review] subjects JSON failed: len=%u trunc=%d\n",
                          static_cast<unsigned>(n), sr.truncated ? 1 : 0);
            Serial.printf("[review] head: %.*s\n", static_cast<int>(std::min<size_t>(n, 80)),
                          sr.body.data());
            batch.error = "subjects JSON";
            return batch;
        }
        std::vector<Subject> part = parseSubjects(sv);
        sv = JsonValue{};  // free the page before growing the subject vector
        for (Subject& s : part) batch.subjects.push_back(std::move(s));
        Serial.printf("[review] subjects %u/%u, total %u\n", static_cast<unsigned>(end),
                      static_cast<unsigned>(ids.size()),
                      static_cast<unsigned>(batch.subjects.size()));
    }

    attachStudyMaterials(token, &batch.subjects);

    batch.fetchedAt = now;
    batch.ok = !batch.subjects.empty();
    if (!batch.ok) batch.error = "no subjects";
    return batch;
}

}  // namespace kk

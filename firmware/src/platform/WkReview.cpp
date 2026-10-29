#include "platform/WkReview.h"

#include <algorithm>
#include <utility>

#include "platform/WkClient.h"
#include "platform/WkJson.h"
#include "platform/WkLessons.h"
#include "platform/WkReviewStatistics.h"
#include "platform/WkStudyMaterials.h"

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
    WkAssignmentsResult ar = wkGetAssignmentsRetry(path, token, 32768, want);
    if (ar.http.httpCode != 200) {
        batch.httpCode = ar.http.httpCode;
        batch.error = "assignments HTTP " + String(ar.http.httpCode);
        return batch;
    }
    if (!ar.parsed) {
        batch.error = "assignments JSON";
        return batch;
    }
    const std::vector<Assignment> parsed = std::move(ar.assignments);
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
        WkSubjectsResult sr = wkGetSubjectsRetry(sp, token, 32768, true);
        if (sr.http.httpCode != 200) {
            batch.httpCode = sr.http.httpCode;
            batch.error = "subjects HTTP " + String(sr.http.httpCode);
            return batch;
        }
        if (!sr.parsed) {
            const size_t n = sr.http.body.size();
            Serial.printf("[review] subjects JSON failed: len=%u trunc=%d\n",
                          static_cast<unsigned>(n), sr.http.truncated ? 1 : 0);
            Serial.printf("[review] head: %.*s\n", static_cast<int>(std::min<size_t>(n, 80)),
                          sr.http.body.data());
            batch.error = "subjects JSON";
            return batch;
        }
        std::vector<Subject> part = std::move(sr.subjects);
        for (Subject& s : part) batch.subjects.push_back(std::move(s));
        Serial.printf("[review] subjects %u/%u, total %u\n", static_cast<unsigned>(end),
                      static_cast<unsigned>(ids.size()),
                      static_cast<unsigned>(batch.subjects.size()));
    }

    attachStudyMaterials(token, &batch.subjects);
    attachReviewStatistics(token, &batch.subjects);

    // Detail data: fetch the component subjects once and keep them in the same
    // subject list. The queue ignores subjects without an assignment.
    std::vector<uint32_t> componentIds;
    for (const Subject& s : batch.subjects) {
        for (uint32_t id : s.componentSubjectIds) {
            if (std::find(componentIds.begin(), componentIds.end(), id) != componentIds.end())
                continue;
            if (std::any_of(batch.subjects.begin(), batch.subjects.end(),
                            [id](const Subject& x) { return x.id == id; }))
                continue;
            componentIds.push_back(id);
        }
    }
    if (!componentIds.empty()) {
        // Small pages: a 10-subject component page is ~20 KB and its DOM can
        // exceed the largest free block after the review subjects are cached.
        std::vector<Subject> components = fetchSubjectsByIds(token, componentIds, 3);
        batch.subjects.reserve(batch.subjects.size() + components.size());
        for (Subject& c : components) {
            if (std::any_of(batch.subjects.begin(), batch.subjects.end(),
                            [&c](const Subject& x) { return x.id == c.id; }))
                continue;
            batch.subjects.push_back(std::move(c));
        }
        Serial.printf("[review] components %u/%u\n", static_cast<unsigned>(components.size()),
                      static_cast<unsigned>(componentIds.size()));
    }

    batch.fetchedAt = now;
    batch.ok = !batch.subjects.empty();
    if (!batch.ok) batch.error = "no subjects";
    return batch;
}

}  // namespace kk

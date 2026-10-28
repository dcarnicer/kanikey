#include "platform/WkLessons.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "platform/WkClient.h"
#include "platform/WkStudyMaterials.h"
#include "wk/Json.h"
#include "wk/Time.h"

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

LessonBatch fetchLessonBatch(const String& token, const std::vector<uint32_t>& lessonSubjectIds,
                             size_t limit) {
    LessonBatch batch;
    if (lessonSubjectIds.empty()) {
        batch.error = "no lessons";
        return batch;
    }
    const size_t want = std::min(limit, lessonSubjectIds.size());
    batch.assignments.reserve(want);
    batch.subjects.reserve(want);
    String path = "/v2/assignments?subject_ids=" + idList(lessonSubjectIds, 0, want);
    Serial.printf("[lesson] assignments for %u subjects\n", static_cast<unsigned>(want));
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
        if (a.hidden || a.srsStage != 0) continue;
        batch.assignments.push_back(a);
        if (batch.assignments.size() >= want) break;
    }
    if (batch.assignments.empty()) {
        batch.error = "no due lessons";
        return batch;
    }

    std::vector<uint32_t> ids;
    for (const Assignment& a : batch.assignments)
        if (std::find(ids.begin(), ids.end(), a.subjectId) == ids.end())
            ids.push_back(a.subjectId);

    constexpr size_t kSubjectBatch = 3;
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
            batch.error = "subjects JSON";
            return batch;
        }
        std::vector<Subject> part = parseSubjects(sv, true);
        sv = JsonValue{};  // free the page before growing the subject vector
        for (Subject& s : part) batch.subjects.push_back(std::move(s));
        Serial.printf("[lesson] subjects %u/%u, total %u\n", static_cast<unsigned>(end),
                      static_cast<unsigned>(ids.size()),
                      static_cast<unsigned>(batch.subjects.size()));
    }
    attachStudyMaterials(token, &batch.subjects);

    // Keep radical-only lessons too: their prompt is a bitmap from the visual
    // pack, not a Unicode character. The UI reports a missing glyph or pack.
    std::vector<Assignment> usable;
    for (const Assignment& a : batch.assignments) {
        for (const Subject& s : batch.subjects)
            if (s.id == a.subjectId) {
                usable.push_back(a);
                break;
            }
    }
    batch.assignments = std::move(usable);
    batch.ok = !batch.assignments.empty();
    if (!batch.ok) batch.error = "no lesson subjects";
    return batch;
}

std::vector<Subject> fetchSubjectsByIds(const String& token, const std::vector<uint32_t>& ids,
                                        size_t batch) {
    std::vector<Subject> out;
    if (ids.empty() || batch == 0) return out;
    out.reserve(ids.size());
    for (size_t begin = 0; begin < ids.size(); begin += batch) {
        const size_t end = std::min(ids.size(), begin + batch);
        const String path = "/v2/subjects?ids=" + idList(ids, begin, end);
        const WkResponse r = wkGetRetry(path, token, 32768, 12000);
        if (r.httpCode != 200) {
            Serial.printf("[lesson] components HTTP %d\n", r.httpCode);
            return {};
        }
        JsonValue v;
        if (!parseJson(r.body, &v)) {
            Serial.printf("[lesson] components JSON failed: len=%u\n",
                          static_cast<unsigned>(r.body.size()));
            return {};
        }
        std::vector<Subject> part = parseSubjects(v);
        v = JsonValue{};  // free the page before growing the output vector
        for (Subject& s : part) out.push_back(std::move(s));
        Serial.printf("[lesson] components %u/%u\n", static_cast<unsigned>(end),
                      static_cast<unsigned>(ids.size()));
    }
    return out;
}

bool startAssignment(const String& token, uint32_t assignmentId, uint32_t startedAt) {
    String path = "/v2/assignments/";
    path += String(static_cast<unsigned long>(assignmentId));
    path += "/start";
    String body = "{\"assignment\":{";
    if (startedAt >= 1700000000u) {
        body += "\"started_at\":\"";
        body += formatIso8601(startedAt).c_str();
        body += "\"";
    }
    body += "}}";
    const WkResponse r = wkPut(path, token, body, 4096, 12000);
    Serial.printf("[lesson] start %u -> %d\n", static_cast<unsigned>(assignmentId), r.httpCode);
    return r.httpCode >= 200 && r.httpCode < 300;
}

}  // namespace kk

#include "platform/WkLessons.h"

#include <algorithm>
#include <utility>

#include "platform/WkClient.h"
#include "platform/WkJson.h"
#include "platform/WkStudyMaterials.h"
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

}  // namespace

LessonAssignments fetchLessonAssignments(const String& token,
                                         const std::vector<uint32_t>& lessonSubjectIds,
                                         size_t maxCandidates) {
    LessonAssignments result;
    if (lessonSubjectIds.empty()) {
        result.error = "no lessons";
        return result;
    }
    const size_t total = std::min(maxCandidates, lessonSubjectIds.size());
    result.assignments.reserve(total);
    size_t pageSize = 30;
    for (size_t begin = 0; begin < total;) {
        const size_t count = std::min(pageSize, total - begin);
        const size_t end = begin + count;
        const String path = "/v2/assignments?subject_ids=" +
                            idList(lessonSubjectIds, begin, end);
        Serial.printf("[lesson] assignment page %u-%u\n", static_cast<unsigned>(begin),
                      static_cast<unsigned>(end));
        WkAssignmentsResult ar = wkGetAssignmentsRetry(path, token, 32768, count);
        if (ar.http.httpCode != 200) {
            result.httpCode = ar.http.httpCode;
            result.error = "assignments HTTP " + String(ar.http.httpCode);
            return result;
        }
        if (!ar.parsed) {
            if (ar.http.truncated && count > 1) {
                const size_t smaller = count > 10 ? 10 : (count > 2 ? 2 : 1);
                Serial.printf("[lesson] assignment page %u-%u truncated, retrying with %u\n",
                              static_cast<unsigned>(begin), static_cast<unsigned>(end),
                              static_cast<unsigned>(smaller));
                pageSize = smaller;
                continue;
            }
            result.error = "assignments JSON";
            return result;
        }
        for (const Assignment& a : ar.assignments) {
            if (a.hidden || a.srsStage != 0) continue;
            result.assignments.push_back(a);
        }
        begin = end;
    }
    if (result.assignments.empty()) {
        result.error = "no due lessons";
        return result;
    }
    result.ok = true;
    return result;
}

AssignmentBatch fetchAssignmentsForSubjects(const String& token,
                                            const std::vector<uint32_t>& subjectIds,
                                            size_t batch) {
    AssignmentBatch result;
    if (subjectIds.empty() || batch == 0) {
        result.error = "no subjects";
        return result;
    }
    result.assignments.reserve(subjectIds.size());
    for (size_t begin = 0; begin < subjectIds.size(); begin += batch) {
        const size_t end = std::min(subjectIds.size(), begin + batch);
        const String path = "/v2/assignments?subject_ids=" + idList(subjectIds, begin, end);
        WkAssignmentsResult ar = wkGetAssignmentsRetry(path, token, 32768, end - begin);
        if (ar.http.httpCode != 200) {
            result.httpCode = ar.http.httpCode;
            result.error = "assignments HTTP " + String(ar.http.httpCode);
            return result;
        }
        if (!ar.parsed) {
            result.error = "assignments JSON";
            return result;
        }
        for (const Assignment& a : ar.assignments) result.assignments.push_back(a);
        Serial.printf("[assignments] %u/%u\n", static_cast<unsigned>(end),
                      static_cast<unsigned>(subjectIds.size()));
    }
    result.ok = true;
    return result;
}

SubjectLevels fetchSubjectLevels(const String& token,
                                 const std::vector<uint32_t>& subjectIds) {
    SubjectLevels result;
    if (subjectIds.empty()) {
        result.ok = true;
        return result;
    }
    result.levels.reserve(subjectIds.size());
    size_t pageSize = 10;
    for (size_t begin = 0; begin < subjectIds.size();) {
        const size_t count = std::min(pageSize, subjectIds.size() - begin);
        const size_t end = begin + count;
        const String path = "/v2/subjects?ids=" + idList(subjectIds, begin, end);
        WkSubjectsResult sr = wkGetSubjectsRetry(path, token, 32768, false);
        Serial.printf("[lesson] level page %u-%u: %u bytes%s\n", static_cast<unsigned>(begin),
                      static_cast<unsigned>(end), static_cast<unsigned>(sr.http.bytes),
                      sr.http.truncated ? " (truncated)" : "");
        if (sr.http.httpCode != 200) {
            result.httpCode = sr.http.httpCode;
            result.error = "subjects HTTP " + String(sr.http.httpCode);
            return result;
        }
        if (!sr.parsed) {
            if (sr.http.truncated && count > 1) {
                const size_t smaller = count > 5 ? 5 : (count > 2 ? 2 : 1);
                Serial.printf("[lesson] level page %u-%u truncated, retrying with %u\n",
                              static_cast<unsigned>(begin), static_cast<unsigned>(end),
                              static_cast<unsigned>(smaller));
                pageSize = smaller;
                continue;
            }
            result.error = "subjects JSON";
            return result;
        }
        for (const Subject& s : sr.subjects)
            result.levels.push_back({s.id, s.level, s.lessonPosition});
        begin = end;
    }
    if (result.levels.empty()) {
        result.error = "no subject levels";
        return result;
    }
    result.ok = true;
    return result;
}

LessonBatch fetchLessonDetails(const String& token, std::vector<Assignment> assignments) {
    LessonBatch batch;
    batch.assignments = std::move(assignments);
    if (batch.assignments.empty()) {
        batch.error = "no lessons";
        return batch;
    }
    batch.subjects.reserve(batch.assignments.size());

    std::vector<uint32_t> ids;
    ids.reserve(batch.assignments.size());
    for (const Assignment& a : batch.assignments)
        if (std::find(ids.begin(), ids.end(), a.subjectId) == ids.end())
            ids.push_back(a.subjectId);

    size_t pageSize = 3;
    for (size_t begin = 0; begin < ids.size();) {
        const size_t count = std::min(pageSize, ids.size() - begin);
        const size_t end = begin + count;
        String sp = "/v2/subjects?ids=" + idList(ids, begin, end);
        WkSubjectsResult sr = wkGetSubjectsRetry(sp, token, 32768, true);
        Serial.printf("[lesson] subject page %u-%u: %u bytes%s\n", static_cast<unsigned>(begin),
                      static_cast<unsigned>(end), static_cast<unsigned>(sr.http.bytes),
                      sr.http.truncated ? " (truncated)" : "");
        if (sr.http.httpCode != 200) {
            batch.httpCode = sr.http.httpCode;
            batch.error = "subjects HTTP " + String(sr.http.httpCode);
            return batch;
        }
        if (!sr.parsed) {
            if (sr.http.truncated && count > 1) {
                // Lesson mnemonics are large: fall back from 3 subjects to 1.
                Serial.printf("[lesson] subject page %u-%u truncated, retrying with 1\n",
                              static_cast<unsigned>(begin), static_cast<unsigned>(end));
                pageSize = 1;
                continue;
            }
            batch.error = "subjects JSON";
            return batch;
        }
        std::vector<Subject> part = std::move(sr.subjects);
        for (Subject& s : part) batch.subjects.push_back(std::move(s));
        Serial.printf("[lesson] subjects %u/%u, total %u\n", static_cast<unsigned>(end),
                      static_cast<unsigned>(ids.size()),
                      static_cast<unsigned>(batch.subjects.size()));
        begin = end;
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
        WkSubjectsResult r = wkGetSubjectsRetry(path, token, 32768, false);
        if (r.http.httpCode != 200) {
            Serial.printf("[subjects] components HTTP %d\n", r.http.httpCode);
            return {};
        }
        if (!r.parsed) {
            Serial.printf("[subjects] components JSON failed: len=%u trunc=%d\n",
                          static_cast<unsigned>(r.http.body.size()), r.http.truncated ? 1 : 0);
            return {};
        }
        std::vector<Subject> part = std::move(r.subjects);
        for (Subject& s : part) out.push_back(std::move(s));
        Serial.printf("[subjects] components %u/%u\n", static_cast<unsigned>(end),
                      static_cast<unsigned>(ids.size()));
    }
    return out;
}

int startAssignment(const String& token, uint32_t assignmentId, uint32_t startedAt) {
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
    return r.httpCode;
}

}  // namespace kk

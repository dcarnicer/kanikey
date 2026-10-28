#include "platform/WkStudyMaterials.h"

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

std::vector<StudyMaterial> fetchStudyMaterials(const String& token,
                                               const std::vector<uint32_t>& subjectIds,
                                               size_t batch) {
    std::vector<StudyMaterial> out;
    if (subjectIds.empty() || batch == 0) return out;
    for (size_t begin = 0; begin < subjectIds.size(); begin += batch) {
        const size_t end = std::min(subjectIds.size(), begin + batch);
        const String path = "/v2/study_materials?subject_ids=" + idList(subjectIds, begin, end);
        const WkResponse r = wkGetRetry(path, token, 32768, 12000);
        if (r.httpCode != 200) {
            Serial.printf("[study] HTTP %d for %u ids\n", r.httpCode,
                          static_cast<unsigned>(end - begin));
            // A token without the study-materials scope is a normal case: keep
            // the subjects and let reviews/lessons work without notes.
            if (r.httpCode == 401 || r.httpCode == 403) {
                Serial.println("[study] continuing without notes");
                break;
            }
            continue;
        }
        JsonValue v;
        if (!parseJson(r.body, &v)) {
            Serial.printf("[study] JSON failed: len=%u trunc=%d\n",
                          static_cast<unsigned>(r.body.size()), r.truncated ? 1 : 0);
            continue;
        }
        std::vector<StudyMaterial> part = parseStudyMaterials(v);
        Serial.printf("[study] %u materials for %u ids\n", static_cast<unsigned>(part.size()),
                      static_cast<unsigned>(end - begin));
        for (StudyMaterial& m : part) out.push_back(std::move(m));
    }
    return out;
}

size_t attachStudyMaterials(const String& token, std::vector<Subject>* subjects) {
    if (!subjects || subjects->empty()) return 0;
    std::vector<uint32_t> ids;
    ids.reserve(subjects->size());
    for (const Subject& s : *subjects)
        if (std::find(ids.begin(), ids.end(), s.id) == ids.end()) ids.push_back(s.id);
    const std::vector<StudyMaterial> materials = fetchStudyMaterials(token, ids);
    const size_t applied = applyStudyMaterials(materials, subjects);
    Serial.printf("[study] %u applied to subjects\n", static_cast<unsigned>(applied));
    return applied;
}

}  // namespace kk

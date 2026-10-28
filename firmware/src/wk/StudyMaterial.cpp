#include "wk/StudyMaterial.h"

#include <algorithm>
#include <utility>

#include "wk/Text.h"

namespace kk {

namespace {
std::string noteField(const JsonValue& data, const char* key) {
    const JsonValue* value = data.find(key);
    if (!value || !value->isString()) return std::string();
    return collapseSpaces(trim(value->string));
}
}  // namespace

std::vector<StudyMaterial> parseStudyMaterials(const JsonValue& root) {
    std::vector<StudyMaterial> out;
    const JsonValue* data = root.find("data");
    if (!data || !data->isArray()) return out;
    out.reserve(data->size());
    for (const JsonValue& item : data->array) {
        const JsonValue* d = item.find("data");
        if (!d || !d->isObject()) continue;
        StudyMaterial m;
        if (const JsonValue* sid = d->find("subject_id"); sid && sid->isNumber())
            m.subjectId = static_cast<uint32_t>(sid->number);
        if (const JsonValue* hidden = d->find("hidden"); hidden && hidden->type == JsonType::Bool)
            m.hidden = hidden->boolean;
        m.meaningNote = noteField(*d, "meaning_note");
        m.readingNote = noteField(*d, "reading_note");
        if (const JsonValue* synonyms = d->find("meaning_synonyms");
            synonyms && synonyms->isArray()) {
            for (const JsonValue& synonym : synonyms->array)
                if (synonym.isString() && !synonym.string.empty())
                    m.meaningSynonyms.push_back(synonym.string);
        }
        if (m.subjectId) out.push_back(std::move(m));
    }
    return out;
}

size_t applyStudyMaterials(const std::vector<StudyMaterial>& materials,
                           std::vector<Subject>* subjects) {
    if (!subjects) return 0;
    size_t applied = 0;
    for (const StudyMaterial& material : materials) {
        if (material.hidden) continue;
        for (Subject& subject : *subjects) {
            if (subject.id != material.subjectId) continue;
            if (!material.meaningNote.empty()) subject.meaningNote = material.meaningNote;
            if (!material.readingNote.empty()) subject.readingNote = material.readingNote;
            for (const std::string& synonym : material.meaningSynonyms) {
                if (std::find(subject.meaningSynonyms.begin(), subject.meaningSynonyms.end(),
                              synonym) == subject.meaningSynonyms.end())
                    subject.meaningSynonyms.push_back(synonym);
            }
            ++applied;
            break;
        }
    }
    return applied;
}

}  // namespace kk

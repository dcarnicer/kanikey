#include "wk/Assignment.h"

#include "wk/Time.h"

namespace kk {

SubjectType subjectTypeFromString(std::string_view s) {
    if (s == "radical") return SubjectType::Radical;
    if (s == "vocabulary") return SubjectType::Vocabulary;
    if (s == "kana_vocabulary") return SubjectType::KanaVocabulary;
    return SubjectType::Kanji;
}

std::vector<Assignment> parseAssignments(const JsonValue& root) {
    std::vector<Assignment> out;
    const JsonValue* data = root.find("data");
    if (!data || !data->isArray()) return out;
    out.reserve(data->size());
    for (const JsonValue& item : data->array) {
        const JsonValue* d = item.find("data");
        if (!d || !d->isObject()) continue;
        Assignment a;
        if (const JsonValue* id = item.find("id"); id && id->isNumber())
            a.id = static_cast<uint32_t>(id->number);
        if (const JsonValue* sid = d->find("subject_id"); sid && sid->isNumber())
            a.subjectId = static_cast<uint32_t>(sid->number);
        if (const JsonValue* t = d->find("subject_type"); t && t->isString())
            a.type = subjectTypeFromString(t->string);
        if (const JsonValue* stage = d->find("srs_stage"); stage && stage->isNumber())
            a.srsStage = static_cast<uint8_t>(stage->number);
        if (const JsonValue* at = d->find("available_at"); at && at->isString())
            parseIso8601(at->string, &a.availableAt);
        if (const JsonValue* h = d->find("hidden"); h && h->type == JsonType::Bool)
            a.hidden = h->boolean;
        if (const JsonValue* upd = item.find("data_updated_at"); upd && upd->isString())
            parseIso8601(upd->string, &a.dataUpdatedAt);
        if (a.id && a.subjectId) out.push_back(a);
    }
    return out;
}

}  // namespace kk

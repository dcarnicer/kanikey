#include "wk/SubjectParser.h"

#include <utility>

#include "wk/Text.h"

#include "wk/Assignment.h"

namespace kk {

namespace {
ReadingType readingTypeFromString(std::string_view s) {
    if (s == "onyomi") return ReadingType::Onyomi;
    if (s == "kunyomi") return ReadingType::Kunyomi;
    if (s == "nanori") return ReadingType::Nanori;
    return ReadingType::Unknown;
}
}  // namespace

std::vector<Subject> parseSubjects(const JsonValue& root, bool details) {
    std::vector<Subject> out;
    const JsonValue* data = root.find("data");
    if (!data || !data->isArray()) return out;
    out.reserve(data->size());
    for (const JsonValue& item : data->array) {
        const JsonValue* d = item.find("data");
        if (!d || !d->isObject()) continue;
        Subject s;
        if (const JsonValue* id = item.find("id"); id && id->isNumber())
            s.id = static_cast<uint32_t>(id->number);
        if (const JsonValue* o = item.find("object"); o && o->isString())
            s.type = subjectTypeFromString(o->string);
        if (const JsonValue* level = d->find("level"); level && level->isNumber())
            s.level = static_cast<uint8_t>(level->number);
        if (const JsonValue* chars = d->find("characters"); chars && chars->isString())
            s.characters = chars->string;

        if (const JsonValue* ms = d->find("meanings"); ms && ms->isArray()) {
            s.meanings.reserve(ms->size());
            for (const JsonValue& m : ms->array) {
                Meaning meaning;
                if (const JsonValue* text = m.find("meaning"); text && text->isString())
                    meaning.text = text->string;
                if (const JsonValue* p = m.find("primary"); p && p->type == JsonType::Bool)
                    meaning.primary = p->boolean;
                if (const JsonValue* a = m.find("accepted_answer"); a && a->type == JsonType::Bool)
                    meaning.accepted = a->boolean;
                if (!meaning.text.empty()) s.meanings.push_back(std::move(meaning));
            }
        }
        if (const JsonValue* rs = d->find("readings"); rs && rs->isArray()) {
            s.readings.reserve(rs->size());
            for (const JsonValue& r : rs->array) {
                Reading reading;
                if (const JsonValue* text = r.find("reading"); text && text->isString())
                    reading.text = text->string;
                if (const JsonValue* p = r.find("primary"); p && p->type == JsonType::Bool)
                    reading.primary = p->boolean;
                if (const JsonValue* a = r.find("accepted_answer"); a && a->type == JsonType::Bool)
                    reading.accepted = a->boolean;
                if (const JsonValue* t = r.find("type"); t && t->isString())
                    reading.type = readingTypeFromString(t->string);
                if (!reading.text.empty()) s.readings.push_back(std::move(reading));
            }
        }
        if (const JsonValue* comps = d->find("component_subject_ids"); comps && comps->isArray()) {
            for (const JsonValue& c : comps->array)
                if (c.isNumber()) s.componentSubjectIds.push_back(static_cast<uint32_t>(c.number));
        }
        if (details) {
            auto textField = [d](const char* key) {
                const JsonValue* v = d->find(key);
                return v && v->isString() ? stripMarkup(v->string) : std::string();
            };
            s.meaningMnemonic = textField("meaning_mnemonic");
            s.readingMnemonic = textField("reading_mnemonic");
            s.meaningHint = textField("meaning_hint");
            s.readingHint = textField("reading_hint");
        }
        if (const JsonValue* aux = d->find("auxiliary_meanings"); aux && aux->isArray()) {
            for (const JsonValue& a : aux->array) {
                const JsonValue* text = a.find("meaning");
                const JsonValue* type = a.find("type");
                if (!text || !text->isString() || !type || !type->isString()) continue;
                if (type->string == "whitelist")
                    s.whitelist.push_back(text->string);
                else if (type->string == "blacklist")
                    s.blacklist.push_back(text->string);
            }
        }
        if (s.id) out.push_back(std::move(s));
    }
    return out;
}

}  // namespace kk

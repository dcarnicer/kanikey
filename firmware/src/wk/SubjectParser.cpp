#include "wk/SubjectParser.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "wk/JsonStream.h"
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

void assignStripped(std::string* out, std::string_view value) {
    out->clear();
    out->reserve(value.size());
    bool tag = false;
    for (char c : value) {
        if (c == '<') {
            tag = true;
            continue;
        }
        if (c == '>') {
            tag = false;
            continue;
        }
        if (!tag) out->push_back(c);
    }
}

void assignCollapsed(std::string* out, std::string_view value) {
    out->clear();
    bool space = false;
    for (char c : value) {
        if (isSpace(c)) {
            if (!out->empty()) space = true;
            continue;
        }
        if (space) out->push_back(' ');
        space = false;
        out->push_back(c);
    }
}

uint32_t streamU32(double value) {
    return value < 0 ? 0 : static_cast<uint32_t>(value);
}

uint16_t streamU16(double value) {
    const uint32_t n = streamU32(value);
    return n > 0xFFFFu ? 0xFFFFu : static_cast<uint16_t>(n);
}

uint8_t streamPercent(double value) {
    const uint32_t n = streamU32(value);
    return n > 100 ? 100 : static_cast<uint8_t>(n);
}

class SubjectSink : public JsonSink {
public:
    SubjectSink(std::vector<Subject>* out, bool details) : out_(out), details_(details) {}

    bool onObjectStart() override {
        ObjKind kind = ObjKind::Unknown;
        if (depth_ == 2 && arrayKind_[2] == ArrayKind::Data) {
            kind = ObjKind::Item;
        } else if (depth_ == 3 && objectKind_[3] == ObjKind::Item && pending_ == Key::Data) {
            kind = ObjKind::SubjectData;
        } else if (depth_ == 5) {
            switch (arrayKind_[5]) {
                case ArrayKind::Meanings:
                    kind = ObjKind::Meaning;
                    currentMeaning_ = Meaning{};
                    break;
                case ArrayKind::Readings:
                    kind = ObjKind::Reading;
                    currentReading_ = Reading{};
                    break;
                case ArrayKind::ContextSentences:
                    kind = ObjKind::Context;
                    currentContext_ = ContextSentence{};
                    break;
                case ArrayKind::AuxiliaryMeanings:
                    kind = ObjKind::Auxiliary;
                    auxMeaning_.clear();
                    auxType_.clear();
                    auxHasMeaning_ = false;
                    auxHasType_ = false;
                    break;
                default: break;
            }
        }

        if (kind == ObjKind::Item) {
            current_ = Subject{};
            active_ = true;
            hasSubjectData_ = false;
        } else if (kind == ObjKind::SubjectData) {
            hasSubjectData_ = true;
        }

        pending_ = Key::None;
        ++depth_;
        objectKind_[depth_] = kind;
        return true;
    }

    bool onObjectEnd() override {
        const ObjKind kind = objectKind_[depth_];
        if (kind == ObjKind::Meaning) {
            if (!currentMeaning_.text.empty())
                current_.meanings.push_back(std::move(currentMeaning_));
        } else if (kind == ObjKind::Reading) {
            if (!currentReading_.text.empty())
                current_.readings.push_back(std::move(currentReading_));
        } else if (kind == ObjKind::Context) {
            if (details_ && (!currentContext_.en.empty() || !currentContext_.ja.empty()) &&
                current_.contextSentences.size() < 4)
                current_.contextSentences.push_back(std::move(currentContext_));
        } else if (kind == ObjKind::Auxiliary) {
            if (auxHasMeaning_ && auxHasType_) {
                if (auxType_ == "whitelist")
                    current_.whitelist.push_back(std::move(auxMeaning_));
                else if (auxType_ == "blacklist")
                    current_.blacklist.push_back(std::move(auxMeaning_));
            }
        } else if (kind == ObjKind::Item) {
            if (active_ && hasSubjectData_ && current_.id) out_->push_back(std::move(current_));
            active_ = false;
            hasSubjectData_ = false;
        }
        objectKind_[depth_] = ObjKind::Unknown;
        --depth_;
        return true;
    }

    bool onArrayStart() override {
        ArrayKind kind = ArrayKind::None;
        if (depth_ == 1 && pending_ == Key::Data) {
            kind = ArrayKind::Data;
        } else if (depth_ == 4 && objectKind_[4] == ObjKind::SubjectData) {
            switch (pending_) {
                case Key::Meanings: kind = ArrayKind::Meanings; break;
                case Key::Readings: kind = ArrayKind::Readings; break;
                case Key::ComponentIds: kind = ArrayKind::Components; break;
                case Key::SimilarIds: kind = ArrayKind::Similar; break;
                case Key::AmalgamationIds: kind = ArrayKind::Amalgamation; break;
                case Key::PartsOfSpeech: kind = ArrayKind::PartsOfSpeech; break;
                case Key::ContextSentences: kind = ArrayKind::ContextSentences; break;
                case Key::AuxiliaryMeanings: kind = ArrayKind::AuxiliaryMeanings; break;
                default: break;
            }
        }
        pending_ = Key::None;
        ++depth_;
        arrayKind_[depth_] = kind;
        return true;
    }

    bool onArrayEnd() override {
        arrayKind_[depth_] = ArrayKind::None;
        --depth_;
        return true;
    }

    bool onKey(std::string_view key) override {
        pending_ = Key::None;
        if (depth_ == 1) {
            if (key == "data") pending_ = Key::Data;
        } else if (depth_ == 3 && objectKind_[3] == ObjKind::Item) {
            if (key == "id")
                pending_ = Key::Id;
            else if (key == "object")
                pending_ = Key::ObjectType;
            else if (key == "data")
                pending_ = Key::Data;
        } else if (depth_ == 4 && objectKind_[4] == ObjKind::SubjectData) {
            pending_ = subjectKey(key);
        } else if (depth_ == 6) {
            switch (objectKind_[6]) {
                case ObjKind::Meaning:
                    if (key == "meaning")
                        pending_ = Key::MeaningText;
                    else if (key == "primary")
                        pending_ = Key::Primary;
                    else if (key == "accepted_answer")
                        pending_ = Key::Accepted;
                    break;
                case ObjKind::Reading:
                    if (key == "reading")
                        pending_ = Key::ReadingText;
                    else if (key == "primary")
                        pending_ = Key::Primary;
                    else if (key == "accepted_answer")
                        pending_ = Key::Accepted;
                    else if (key == "type")
                        pending_ = Key::ReadingType;
                    break;
                case ObjKind::Context:
                    if (key == "en")
                        pending_ = Key::ContextEn;
                    else if (key == "ja")
                        pending_ = Key::ContextJa;
                    break;
                case ObjKind::Auxiliary:
                    if (key == "meaning")
                        pending_ = Key::MeaningText;
                    else if (key == "type")
                        pending_ = Key::ReadingType;
                    break;
                default: break;
            }
        }
        return true;
    }

    bool onString(std::string_view value) override {
        if (depth_ == 3 && objectKind_[3] == ObjKind::Item && pending_ == Key::ObjectType) {
            current_.type = subjectTypeFromString(value);
        } else if (depth_ == 4 && objectKind_[4] == ObjKind::SubjectData) {
            applySubjectString(value);
        } else if (depth_ == 5 && arrayKind_[5] == ArrayKind::PartsOfSpeech) {
            if (details_ && !value.empty() && current_.partsOfSpeech.size() < 8)
                current_.partsOfSpeech.emplace_back(value);
        } else if (depth_ == 6) {
            switch (objectKind_[6]) {
                case ObjKind::Meaning:
                    if (pending_ == Key::MeaningText) currentMeaning_.text = std::string(value);
                    break;
                case ObjKind::Reading:
                    if (pending_ == Key::ReadingText)
                        currentReading_.text = std::string(value);
                    else if (pending_ == Key::ReadingType)
                        currentReading_.type = readingTypeFromString(value);
                    break;
                case ObjKind::Context:
                    if (details_ && pending_ == Key::ContextEn)
                        currentContext_.en = std::string(value);
                    else if (details_ && pending_ == Key::ContextJa)
                        currentContext_.ja = std::string(value);
                    break;
                case ObjKind::Auxiliary:
                    if (pending_ == Key::MeaningText) {
                        auxMeaning_ = std::string(value);
                        auxHasMeaning_ = true;
                    } else if (pending_ == Key::ReadingType) {
                        auxType_ = std::string(value);
                        auxHasType_ = true;
                    }
                    break;
                default: break;
            }
        }
        pending_ = Key::None;
        return true;
    }

    bool onNumber(double value) override {
        if (depth_ == 3 && objectKind_[3] == ObjKind::Item && pending_ == Key::Id) {
            current_.id = static_cast<uint32_t>(value);
        } else if (depth_ == 4 && objectKind_[4] == ObjKind::SubjectData) {
            applySubjectNumber(value);
        } else if (depth_ == 5) {
            if (arrayKind_[5] == ArrayKind::Components) {
                current_.componentSubjectIds.push_back(static_cast<uint32_t>(value));
            } else if (arrayKind_[5] == ArrayKind::Similar) {
                if (current_.visuallySimilarSubjectIds.size() < 32)
                    current_.visuallySimilarSubjectIds.push_back(static_cast<uint32_t>(value));
            } else if (arrayKind_[5] == ArrayKind::Amalgamation) {
                if (current_.amalgamationSubjectIds.size() < 32)
                    current_.amalgamationSubjectIds.push_back(static_cast<uint32_t>(value));
            }
        }
        pending_ = Key::None;
        return true;
    }

    bool onBool(bool value) override {
        if (depth_ == 6) {
            if (objectKind_[6] == ObjKind::Meaning) {
                if (pending_ == Key::Primary)
                    currentMeaning_.primary = value;
                else if (pending_ == Key::Accepted)
                    currentMeaning_.accepted = value;
            } else if (objectKind_[6] == ObjKind::Reading) {
                if (pending_ == Key::Primary)
                    currentReading_.primary = value;
                else if (pending_ == Key::Accepted)
                    currentReading_.accepted = value;
            }
        }
        pending_ = Key::None;
        return true;
    }

    bool onNull() override {
        if (depth_ == 4 && objectKind_[4] == ObjKind::SubjectData) {
            if (pending_ == Key::Characters)
                current_.characters.clear();
            else if (details_ && pending_ == Key::MeaningMnemonic)
                current_.meaningMnemonic.clear();
            else if (details_ && pending_ == Key::ReadingMnemonic)
                current_.readingMnemonic.clear();
            else if (details_ && pending_ == Key::MeaningHint)
                current_.meaningHint.clear();
            else if (details_ && pending_ == Key::ReadingHint)
                current_.readingHint.clear();
            else if (details_ && pending_ == Key::MeaningNote)
                current_.meaningNote.clear();
            else if (details_ && pending_ == Key::ReadingNote)
                current_.readingNote.clear();
        }
        pending_ = Key::None;
        return true;
    }

private:
    enum class Key : uint8_t {
        None,
        Data,
        Id,
        ObjectType,
        Level,
        Characters,
        Meanings,
        Readings,
        ComponentIds,
        SimilarIds,
        AmalgamationIds,
        MeaningMnemonic,
        ReadingMnemonic,
        MeaningHint,
        ReadingHint,
        PartsOfSpeech,
        ContextSentences,
        AuxiliaryMeanings,
        MeaningNote,
        ReadingNote,
        MeaningText,
        ReadingText,
        ReadingType,
        ContextEn,
        ContextJa,
        Primary,
        Accepted,
        MeaningCorrect,
        MeaningIncorrect,
        ReadingCorrect,
        ReadingIncorrect,
        MeaningMaxStreak,
        MeaningCurrentStreak,
        ReadingMaxStreak,
        ReadingCurrentStreak,
        PercentageCorrect,
    };

    enum class ArrayKind : uint8_t {
        None,
        Data,
        Meanings,
        Readings,
        Components,
        Similar,
        Amalgamation,
        PartsOfSpeech,
        ContextSentences,
        AuxiliaryMeanings,
    };

    enum class ObjKind : uint8_t {
        Unknown,
        Item,
        SubjectData,
        Meaning,
        Reading,
        Context,
        Auxiliary,
    };

    static Key subjectKey(std::string_view key) {
        if (key == "level") return Key::Level;
        if (key == "characters") return Key::Characters;
        if (key == "meanings") return Key::Meanings;
        if (key == "readings") return Key::Readings;
        if (key == "component_subject_ids") return Key::ComponentIds;
        if (key == "visually_similar_subject_ids") return Key::SimilarIds;
        if (key == "amalgamation_subject_ids") return Key::AmalgamationIds;
        if (key == "meaning_mnemonic") return Key::MeaningMnemonic;
        if (key == "reading_mnemonic") return Key::ReadingMnemonic;
        if (key == "meaning_hint") return Key::MeaningHint;
        if (key == "reading_hint") return Key::ReadingHint;
        if (key == "parts_of_speech") return Key::PartsOfSpeech;
        if (key == "context_sentences") return Key::ContextSentences;
        if (key == "auxiliary_meanings") return Key::AuxiliaryMeanings;
        if (key == "meaning_note") return Key::MeaningNote;
        if (key == "reading_note") return Key::ReadingNote;
        if (key == "meaning_correct") return Key::MeaningCorrect;
        if (key == "meaning_incorrect") return Key::MeaningIncorrect;
        if (key == "reading_correct") return Key::ReadingCorrect;
        if (key == "reading_incorrect") return Key::ReadingIncorrect;
        if (key == "meaning_max_streak") return Key::MeaningMaxStreak;
        if (key == "meaning_current_streak") return Key::MeaningCurrentStreak;
        if (key == "reading_max_streak") return Key::ReadingMaxStreak;
        if (key == "reading_current_streak") return Key::ReadingCurrentStreak;
        if (key == "percentage_correct") return Key::PercentageCorrect;
        return Key::None;
    }

    void applySubjectString(std::string_view value) {
        switch (pending_) {
            case Key::Characters: current_.characters = std::string(value); break;
            case Key::MeaningMnemonic:
                if (details_) {
                    assignStripped(&current_.meaningMnemonic, value);
                }
                break;
            case Key::ReadingMnemonic:
                if (details_) {
                    assignStripped(&current_.readingMnemonic, value);
                }
                break;
            case Key::MeaningHint:
                if (details_) {
                    assignStripped(&current_.meaningHint, value);
                }
                break;
            case Key::ReadingHint:
                if (details_) {
                    assignStripped(&current_.readingHint, value);
                }
                break;
            case Key::MeaningNote:
                if (details_) assignCollapsed(&current_.meaningNote, value);
                break;
            case Key::ReadingNote:
                if (details_) assignCollapsed(&current_.readingNote, value);
                break;
            default: break;
        }
    }

    void applySubjectNumber(double value) {
        switch (pending_) {
            case Key::Level: current_.level = static_cast<uint8_t>(value); break;
            case Key::MeaningCorrect:
                current_.meaningCorrect = streamU32(value);
                current_.hasReviewStats = true;
                break;
            case Key::MeaningIncorrect:
                current_.meaningIncorrect = streamU32(value);
                current_.hasReviewStats = true;
                break;
            case Key::ReadingCorrect:
                current_.readingCorrect = streamU32(value);
                current_.hasReviewStats = true;
                break;
            case Key::ReadingIncorrect:
                current_.readingIncorrect = streamU32(value);
                current_.hasReviewStats = true;
                break;
            case Key::MeaningMaxStreak:
                current_.meaningMaxStreak = streamU16(value);
                current_.hasReviewStats = true;
                break;
            case Key::MeaningCurrentStreak:
                current_.meaningCurrentStreak = streamU16(value);
                current_.hasReviewStats = true;
                break;
            case Key::ReadingMaxStreak:
                current_.readingMaxStreak = streamU16(value);
                current_.hasReviewStats = true;
                break;
            case Key::ReadingCurrentStreak:
                current_.readingCurrentStreak = streamU16(value);
                current_.hasReviewStats = true;
                break;
            case Key::PercentageCorrect:
                current_.percentageCorrect = streamPercent(value);
                current_.hasReviewStats = true;
                break;
            default: break;
        }
    }

    static constexpr int kStackDepth = JsonStreamParser::kDefaultMaxDepth + 4;

    std::vector<Subject>* out_;
    bool details_ = false;
    Subject current_;
    Meaning currentMeaning_;
    Reading currentReading_;
    ContextSentence currentContext_;
    std::string auxMeaning_;
    std::string auxType_;
    bool auxHasMeaning_ = false;
    bool auxHasType_ = false;
    Key pending_ = Key::None;
    int depth_ = 0;
    ObjKind objectKind_[kStackDepth] = {};
    ArrayKind arrayKind_[kStackDepth] = {};
    bool active_ = false;
    bool hasSubjectData_ = false;
};

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
        if (const JsonValue* similar = d->find("visually_similar_subject_ids");
            similar && similar->isArray()) {
            for (const JsonValue& c : similar->array)
                if (c.isNumber() && s.visuallySimilarSubjectIds.size() < 32)
                    s.visuallySimilarSubjectIds.push_back(static_cast<uint32_t>(c.number));
        }
        if (const JsonValue* amalg = d->find("amalgamation_subject_ids");
            amalg && amalg->isArray()) {
            for (const JsonValue& c : amalg->array)
                if (c.isNumber() && s.amalgamationSubjectIds.size() < 32)
                    s.amalgamationSubjectIds.push_back(static_cast<uint32_t>(c.number));
        }
        if (details) {
            auto textField = [d](const char* key) {
                const JsonValue* v = d->find(key);
                return v && v->isString() ? stripMarkup(v->string) : std::string();
            };
            auto noteField = [d](const char* key) {
                const JsonValue* v = d->find(key);
                return v && v->isString() ? collapseSpaces(trim(v->string)) : std::string();
            };
            s.meaningMnemonic = textField("meaning_mnemonic");
            s.readingMnemonic = textField("reading_mnemonic");
            s.meaningHint = textField("meaning_hint");
            s.readingHint = textField("reading_hint");
            s.meaningNote = noteField("meaning_note");
            s.readingNote = noteField("reading_note");
            if (const JsonValue* pos = d->find("parts_of_speech"); pos && pos->isArray()) {
                for (const JsonValue& p : pos->array)
                    if (p.isString() && !p.string.empty() && s.partsOfSpeech.size() < 8)
                        s.partsOfSpeech.push_back(p.string);
            }
            if (const JsonValue* ctx = d->find("context_sentences"); ctx && ctx->isArray()) {
                for (const JsonValue& c : ctx->array) {
                    ContextSentence sentence;
                    if (const JsonValue* en = c.find("en"); en && en->isString())
                        sentence.en = en->string;
                    if (const JsonValue* ja = c.find("ja"); ja && ja->isString())
                        sentence.ja = ja->string;
                    if (sentence.en.empty() && sentence.ja.empty()) continue;
                    s.contextSentences.push_back(std::move(sentence));
                    if (s.contextSentences.size() >= 4) break;
                }
            }
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
        // Optional statistics on the subject record (normally a separate
        // /review_statistics resource). They only set hasReviewStats when a
        // field is actually present.
        bool hasStats = false;
        if (const JsonValue* v = d->find("meaning_correct"); v && v->isNumber()) {
            s.meaningCorrect = streamU32(v->number);
            hasStats = true;
        }
        if (const JsonValue* v = d->find("meaning_incorrect"); v && v->isNumber()) {
            s.meaningIncorrect = streamU32(v->number);
            hasStats = true;
        }
        if (const JsonValue* v = d->find("reading_correct"); v && v->isNumber()) {
            s.readingCorrect = streamU32(v->number);
            hasStats = true;
        }
        if (const JsonValue* v = d->find("reading_incorrect"); v && v->isNumber()) {
            s.readingIncorrect = streamU32(v->number);
            hasStats = true;
        }
        if (const JsonValue* v = d->find("meaning_max_streak"); v && v->isNumber()) {
            s.meaningMaxStreak = streamU16(v->number);
            hasStats = true;
        }
        if (const JsonValue* v = d->find("meaning_current_streak"); v && v->isNumber()) {
            s.meaningCurrentStreak = streamU16(v->number);
            hasStats = true;
        }
        if (const JsonValue* v = d->find("reading_max_streak"); v && v->isNumber()) {
            s.readingMaxStreak = streamU16(v->number);
            hasStats = true;
        }
        if (const JsonValue* v = d->find("reading_current_streak"); v && v->isNumber()) {
            s.readingCurrentStreak = streamU16(v->number);
            hasStats = true;
        }
        if (const JsonValue* v = d->find("percentage_correct"); v && v->isNumber()) {
            s.percentageCorrect = streamPercent(v->number);
            hasStats = true;
        }
        if (hasStats) s.hasReviewStats = true;
        if (s.id) out.push_back(std::move(s));
    }
    return out;
}

bool parseSubjectsStream(std::string_view text, std::vector<Subject>* out, bool details) {
    if (!out) return false;
    out->clear();
    SubjectSink sink(out, details);
    JsonStreamParser parser(&sink);
    if (!parser.parse(text)) {
        out->clear();
        return false;
    }
    return true;
}

}  // namespace kk

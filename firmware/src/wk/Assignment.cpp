#include "wk/Assignment.h"

#include <utility>

#include "wk/JsonStream.h"
#include "wk/Time.h"

namespace kk {

namespace {

class AssignmentSink : public JsonSink {
public:
    explicit AssignmentSink(std::vector<Assignment>* out) : out_(out) {}

    bool onObjectStart() override {
        if (depth_ == 2 && inRootData_) {
            current_ = Assignment{};
            active_ = true;
            inAssignmentData_ = false;
        } else if (depth_ == 3 && active_ && pending_ == Key::Data) {
            inAssignmentData_ = true;
        }
        pending_ = Key::None;
        ++depth_;
        return true;
    }

    bool onObjectEnd() override {
        if (depth_ == 4 && inAssignmentData_) {
            inAssignmentData_ = false;
        } else if (depth_ == 3 && active_) {
            if (current_.id && current_.subjectId) out_->push_back(std::move(current_));
            active_ = false;
            inAssignmentData_ = false;
        }
        --depth_;
        return true;
    }

    bool onArrayStart() override {
        if (depth_ == 1 && pending_ == Key::Data) inRootData_ = true;
        pending_ = Key::None;
        ++depth_;
        return true;
    }

    bool onArrayEnd() override {
        if (depth_ == 2 && inRootData_) inRootData_ = false;
        --depth_;
        return true;
    }

    bool onKey(std::string_view key) override {
        pending_ = Key::None;
        if (depth_ == 1) {
            if (key == "data") pending_ = Key::Data;
        } else if (depth_ == 3 && active_ && !inAssignmentData_) {
            if (key == "id")
                pending_ = Key::Id;
            else if (key == "data")
                pending_ = Key::Data;
            else if (key == "data_updated_at")
                pending_ = Key::DataUpdatedAt;
        } else if (depth_ == 4 && inAssignmentData_) {
            if (key == "subject_id")
                pending_ = Key::SubjectId;
            else if (key == "subject_type")
                pending_ = Key::SubjectType;
            else if (key == "srs_stage")
                pending_ = Key::SrsStage;
            else if (key == "lesson_position")
                pending_ = Key::LessonPosition;
            else if (key == "available_at")
                pending_ = Key::AvailableAt;
            else if (key == "hidden")
                pending_ = Key::Hidden;
        }
        return true;
    }

    bool onString(std::string_view value) override {
        if (pending_ == Key::SubjectType && depth_ == 4 && inAssignmentData_)
            current_.type = subjectTypeFromString(value);
        else if (pending_ == Key::AvailableAt && depth_ == 4 && inAssignmentData_)
            parseIso8601(value, &current_.availableAt);
        else if (pending_ == Key::DataUpdatedAt && depth_ == 3 && active_)
            parseIso8601(value, &current_.dataUpdatedAt);
        pending_ = Key::None;
        return true;
    }

    bool onNumber(double value) override {
        if (pending_ == Key::Id && depth_ == 3 && active_ && !inAssignmentData_)
            current_.id = static_cast<uint32_t>(value);
        else if (pending_ == Key::SubjectId && depth_ == 4 && inAssignmentData_)
            current_.subjectId = static_cast<uint32_t>(value);
        else if (pending_ == Key::SrsStage && depth_ == 4 && inAssignmentData_)
            current_.srsStage = static_cast<uint8_t>(value);
        else if (pending_ == Key::LessonPosition && depth_ == 4 && inAssignmentData_)
            current_.lessonPosition = static_cast<uint16_t>(value);
        pending_ = Key::None;
        return true;
    }

    bool onBool(bool value) override {
        if (pending_ == Key::Hidden && depth_ == 4 && inAssignmentData_) current_.hidden = value;
        pending_ = Key::None;
        return true;
    }

    bool onNull() override {
        pending_ = Key::None;
        return true;
    }

private:
    enum class Key : uint8_t {
        None,
        Data,
        Id,
        SubjectId,
        SubjectType,
        SrsStage,
        LessonPosition,
        AvailableAt,
        Hidden,
        DataUpdatedAt,
    };

    std::vector<Assignment>* out_;
    Assignment current_;
    Key pending_ = Key::None;
    int depth_ = 0;
    bool inRootData_ = false;
    bool inAssignmentData_ = false;
    bool active_ = false;
};

}  // namespace

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
        if (const JsonValue* pos = d->find("lesson_position"); pos && pos->isNumber())
            a.lessonPosition = static_cast<uint16_t>(pos->number);
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

bool parseAssignmentsStream(std::string_view text, std::vector<Assignment>* out) {
    if (!out) return false;
    out->clear();
    AssignmentSink sink(out);
    JsonStreamParser parser(&sink);
    if (!parser.parse(text)) {
        out->clear();
        return false;
    }
    return true;
}

}  // namespace kk

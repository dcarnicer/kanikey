#include "wk/ReviewResponse.h"

#include "wk/Time.h"

namespace kk {

bool parseReviewUpdate(const JsonValue& root, ReviewUpdate* out) {
    if (!out) return false;
    *out = ReviewUpdate{};
    const JsonValue* resources = root.find("resources_updated");
    if (!resources || !resources->isObject()) return false;
    const JsonValue* assignment = resources->find("assignment");
    if (!assignment || !assignment->isObject()) return false;
    const JsonValue* data = assignment->find("data");
    if (!data || !data->isObject()) return false;

    if (const JsonValue* id = assignment->find("id"); id && id->isNumber())
        out->assignmentId = static_cast<uint32_t>(id->number);
    bool hasStage = false;
    if (const JsonValue* stage = data->find("srs_stage"); stage && stage->isNumber()) {
        out->srsStage = static_cast<uint8_t>(stage->number);
        hasStage = true;
    }
    if (const JsonValue* at = data->find("available_at"); at && at->isString())
        parseIso8601(at->string, &out->availableAt);
    out->valid = out->assignmentId != 0 && hasStage;
    return out->valid;
}

}  // namespace kk

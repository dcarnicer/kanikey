#include "wk/User.h"

namespace kk {

namespace {
int intField(const JsonValue* obj, const char* key, int fallback) {
    if (!obj) return fallback;
    const JsonValue* v = obj->find(key);
    if (!v || !v->isNumber()) return fallback;
    return static_cast<int>(v->number);
}
}  // namespace

bool parseUser(const JsonValue& root, User* out) {
    if (!out) return false;
    const JsonValue* data = root.find("data");
    if (!data || !data->isObject()) return false;
    out->username.clear();
    if (const JsonValue* u = data->find("username"); u && u->isString()) out->username = u->string;
    out->level = intField(data, "level", 0);
    const JsonValue* sub = data->find("subscription");
    if (sub && sub->isObject()) {
        if (const JsonValue* a = sub->find("active"); a && a->type == JsonType::Bool)
            out->subscriptionActive = a->boolean;
        out->maxLevelGranted = intField(sub, "max_level_granted", out->maxLevelGranted);
    }
    const JsonValue* prefs = data->find("preferences");
    if (prefs && prefs->isObject())
        out->lessonsBatchSize = intField(prefs, "lessons_batch_size", out->lessonsBatchSize);
    return !out->username.empty() && out->level > 0;
}

}  // namespace kk

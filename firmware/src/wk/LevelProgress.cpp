#include "wk/LevelProgress.h"

#include "wk/Time.h"

namespace kk {

namespace {
uint32_t u32Field(const JsonValue& data, const char* key) {
    const JsonValue* value = data.find(key);
    if (!value || !value->isNumber() || value->number < 0) return 0;
    return static_cast<uint32_t>(value->number);
}

uint32_t timeField(const JsonValue& data, const char* key) {
    const JsonValue* value = data.find(key);
    if (!value || !value->isString()) return 0;
    uint32_t epoch = 0;
    return parseIso8601(value->string, &epoch) ? epoch : 0;
}

LevelProgress parseEntry(const JsonValue& item) {
    LevelProgress p;
    const JsonValue* d = item.find("data");
    if (!d || !d->isObject()) return p;
    p.level = static_cast<uint8_t>(u32Field(*d, "level"));
    p.unlockedAt = timeField(*d, "unlocked_at");
    p.startedAt = timeField(*d, "started_at");
    p.passedAt = timeField(*d, "passed_at");
    p.completedAt = timeField(*d, "completed_at");
    p.valid = p.level > 0;
    return p;
}
}  // namespace

bool parseLevelProgressions(const JsonValue& root, uint8_t currentLevel, LevelProgress* out) {
    if (!out) return false;
    *out = LevelProgress{};
    const JsonValue* data = root.find("data");
    if (!data || !data->isArray()) return false;

    LevelProgress current;
    LevelProgress highest;
    for (const JsonValue& item : data->array) {
        const LevelProgress p = parseEntry(item);
        if (!p.valid) continue;
        if (!highest.valid || p.level > highest.level) highest = p;
        if (p.level == currentLevel) current = p;
    }
    if (current.valid)
        *out = current;
    else if (highest.valid)
        *out = highest;
    return out->valid;
}

uint8_t levelKanjiPercent(uint16_t passed, uint16_t total) {
    if (total == 0) return 0;
    const uint32_t percent = (static_cast<uint32_t>(passed) * 100u + total / 2u) / total;
    return static_cast<uint8_t>(percent > 100u ? 100u : percent);
}

}  // namespace kk

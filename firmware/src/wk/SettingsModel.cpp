#include "wk/SettingsModel.h"

#include <cstdio>
#include <cstring>

namespace kk {

namespace {

const SettingChoice* findChoice(const SettingDef& def, int value) {
    if (!def.choices) return nullptr;
    for (uint8_t i = 0; i < def.choiceCount; ++i)
        if (def.choices[i].value == value) return &def.choices[i];
    return nullptr;
}

}  // namespace

const SettingDef* settingById(const char* id) {
    if (!id) return nullptr;
    size_t count = 0;
    const SettingDef* all = settingsAll(&count);
    for (size_t i = 0; i < count; ++i)
        if (std::strcmp(all[i].id, id) == 0) return &all[i];
    return nullptr;
}

const SettingDef* settingByNvsKey(const char* nvsKey) {
    if (!nvsKey) return nullptr;
    size_t count = 0;
    const SettingDef* all = settingsAll(&count);
    for (size_t i = 0; i < count; ++i)
        if (all[i].nvsKey && std::strcmp(all[i].nvsKey, nvsKey) == 0) return &all[i];
    return nullptr;
}

int settingNextValue(const SettingDef& def, int current) {
    if (!def.choices || def.choiceCount == 0) return def.defaultValue;
    for (uint8_t i = 0; i < def.choiceCount; ++i)
        if (def.choices[i].value == current)
            return def.choices[(i + 1) % def.choiceCount].value;
    return def.defaultValue;
}

int settingPrevValue(const SettingDef& def, int current) {
    if (!def.choices || def.choiceCount == 0) return def.defaultValue;
    for (uint8_t i = 0; i < def.choiceCount; ++i)
        if (def.choices[i].value == current)
            return def.choices[(i + def.choiceCount - 1) % def.choiceCount].value;
    return def.defaultValue;
}

int settingValidate(const SettingDef& def, int stored) {
    if (!def.choices || def.choiceCount == 0) return def.defaultValue;
    return findChoice(def, stored) ? stored : def.defaultValue;
}

void settingValueLabel(const SettingDef& def, int value, char* buf, size_t size) {
    if (!buf || size == 0) return;
    if (def.kind == SettingKind::Action || def.kind == SettingKind::Submenu ||
        def.kind == SettingKind::Info) {
        std::snprintf(buf, size, "%s", def.valueText ? def.valueText : "");
        return;
    }
    const SettingChoice* choice = findChoice(def, value);
    if (choice) {
        std::snprintf(buf, size, "%s", choice->label);
        return;
    }
    std::snprintf(buf, size, "%d", value);
}

size_t settingsInSection(const char* section, const SettingDef** out, size_t max) {
    if (!section || !out || max == 0) return 0;
    size_t count = 0;
    size_t written = 0;
    const SettingDef* all = settingsAll(&count);
    for (size_t i = 0; i < count && written < max; ++i)
        if (std::strcmp(all[i].section, section) == 0) out[written++] = &all[i];
    return written;
}

int settingFirstQuestionValue(bool hasFirst, int first, bool hasOrder, int order) {
    if (hasFirst) return first;
    if (hasOrder) return order == 0 ? 0 : 1;
    return 2;  // R4 default: Random
}

void SettingsCache::load(Reader reader, void* context) {
    count_ = 0;
    size_t total = 0;
    const SettingDef* all = settingsAll(&total);
    for (size_t i = 0; i < total && count_ < kCapacity; ++i) {
        if (!all[i].nvsKey || !all[i].nvsKey[0]) continue;
        int value = reader ? reader(context, all[i].nvsKey, all[i].defaultValue)
                           : all[i].defaultValue;
        value = settingValidate(all[i], value);
        entries_[count_++] = {all[i].nvsKey, value};
    }
    loaded_ = true;
}

bool SettingsCache::get(const char* nvsKey, int* out) const {
    if (!nvsKey || !out) return false;
    for (size_t i = 0; i < count_; ++i) {
        if (std::strcmp(entries_[i].nvsKey, nvsKey) != 0) continue;
        *out = entries_[i].value;
        return true;
    }
    return false;
}

bool SettingsCache::set(const char* nvsKey, int value) {
    if (!nvsKey) return false;
    for (size_t i = 0; i < count_; ++i) {
        if (std::strcmp(entries_[i].nvsKey, nvsKey) != 0) continue;
        if (entries_[i].value == value) return false;
        entries_[i].value = value;
        return true;
    }
    return false;
}

}  // namespace kk

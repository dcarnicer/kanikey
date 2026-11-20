#pragma once
// Pure model for the Settings registry: metadata, choice cycling and a fixed
// in-RAM cache. No Arduino headers; natively compiled and tested.
#include <cstddef>
#include <cstdint>

namespace kk {

enum class SettingKind { Choice, Toggle, Action, Submenu, Info };

struct SettingChoice {
    const char* label;
    int value;
};

struct SettingDef {
    const char* id;
    const char* section;
    const char* label;
    const char* help;
    SettingKind kind;
    const char* nvsKey;  // nullptr for actions and information rows
    const SettingChoice* choices;
    uint8_t choiceCount;
    int defaultValue;
    bool live;
    // Short affordance for Action, Submenu and Info rows ("open", "run",
    // "info"); nullptr for keyed rows. The menu draws it in the value column.
    const char* valueText;
};

// The table lives in wk/SettingsRegistry.cpp. The pointer stays valid for the
// life of the program; count may be null.
const SettingDef* settingsAll(size_t* count);
const SettingDef* settingById(const char* id);
const SettingDef* settingByNvsKey(const char* nvsKey);

// Choice cycling. Both wrap at either end; a current value that is not one of
// the choices returns the default.
int settingNextValue(const SettingDef& def, int current);
int settingPrevValue(const SettingDef& def, int current);

// Returns `stored` when it is one of the choices, otherwise the default. Rows
// without choices always return the default.
int settingValidate(const SettingDef& def, int stored);

// Writes `off` / `15s` / `2m` / `128` style text. Action, Submenu and Info rows
// produce an empty string.
void settingValueLabel(const SettingDef& def, int value, char* buf, size_t size);

// Copies the rows of one section into `out`, up to `max`, and returns how many
// were written. Section names are the eight CFG-02 names.
size_t settingsInSection(const char* section, const SettingDef** out, size_t max);

// The eight section names in menu order. The index is stable; unknown indexes
// give an empty string.
size_t settingsSectionCount();
const char* settingsSectionName(size_t index);

// R4 migration: `rv.first` replaces the old `order` key. When `rv.first` has
// never been written, a stored `order` (0 Reading first, 1 Meaning first) is
// mapped onto the new values. Neither key present means Random (the default).
int settingFirstQuestionValue(bool hasFirst, int first, bool hasOrder, int order);

// Fixed-capacity cache of {nvsKey, value}; no heap and no NVS dependency. Load
// once at boot, then read and write through get()/set().
class SettingsCache {
public:
    using Reader = int (*)(void* context, const char* nvsKey, int fallback);

    // Room for the full CFG-02 registry (39 keyed rows); static RAM only,
    // roughly 12 bytes per keyed entry.
    static constexpr size_t kCapacity = 64;

    // Fills from the registry. A null reader yields every default; otherwise
    // the reader supplies the stored value and settingValidate() cleans it.
    void load(Reader reader = nullptr, void* context = nullptr);
    bool loaded() const { return loaded_; }
    size_t size() const { return count_; }

    // False when the key is unknown. `out` is untouched on failure.
    bool get(const char* nvsKey, int* out) const;
    // False when the key is unknown or already holds `value`.
    bool set(const char* nvsKey, int value);

private:
    struct Entry {
        const char* nvsKey;
        int value;
    };

    Entry entries_[kCapacity]{};
    size_t count_ = 0;
    bool loaded_ = false;
};

}  // namespace kk

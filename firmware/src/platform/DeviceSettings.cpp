#include "platform/DeviceSettings.h"

#include <Preferences.h>

#include <cctype>
#include <cstdlib>
#include <cstring>

#include "wk/SettingsModel.h"

namespace kk {

namespace {
constexpr const char* kNs = "kanikey";
constexpr const char* kKeyRule = "tzrule";
constexpr const char* kKeyName = "tzname";
constexpr const char* kKeyBrightness = "bright";
constexpr const char* kKeyDim = "dim";
constexpr const char* kKeyBlank = "blank";
constexpr const char* kKeySleep = "sleep";
constexpr const char* kKeyReviewSize = "revsize";
constexpr const char* kKeyLessonSize = "lessonsz";
constexpr const char* kKeyLessonQuiz = "ls.quiz";
constexpr const char* kKeyFirstPart = "rv.first";
constexpr const char* kKeyCriticalFirst = "rv.critical";
constexpr const char* kKeyCriticalPercent = "st.critpct";
constexpr const char* kKeyCriticalMin = "st.critmin";
constexpr const char* kKeyPracticeMistakes = "st.mistakes";
constexpr const char* kKeyPracticeLessons = "st.recent";
constexpr const char* kKeyPracticeSize = "st.practice";
constexpr const char* kKeyAutoSync = "sy.wake";
constexpr const char* kKeySyncFull = "sy.full";
constexpr const char* kKeyPrefetch = "sy.prefetch";

int readPreference(void* context, const char* key, int fallback) {
    Preferences& p = *static_cast<Preferences*>(context);
    // R4 migration: `rv.first` replaces the old `order` key. Until the user
    // changes the value, `order` (0 Reading, 1 Meaning) supplies it.
    if (std::strcmp(key, kKeyFirstPart) == 0) {
        const bool hasFirst = p.isKey(kKeyFirstPart);
        const int first = p.getInt(kKeyFirstPart, fallback);
        return settingFirstQuestionValue(hasFirst, first, p.isKey("order"), p.getInt("order", 1));
    }
    return p.getInt(key, fallback);
}

SettingsCache& settingsCache() {
    static SettingsCache cache;
    static bool loaded = false;
    if (loaded) return cache;
    loaded = true;
    Preferences p;
    if (!p.begin(kNs, false)) {
        cache.load();
        return cache;
    }
    cache.load(readPreference, &p);
    p.end();
    return cache;
}

int cachedSetting(const char* key, int fallback) {
    int value = fallback;
    settingsCache().get(key, &value);
    return value;
}

bool saveInt(const char* key, int value) {
    Preferences p;
    if (!p.begin(kNs, false)) return false;
    const bool ok = p.putInt(key, value) == sizeof(int32_t);
    p.end();
    return ok;
}

// One NVS write per change, cache first so a failed write rolls back. The
// value is not validated: only the dev idle-sleep hook bypasses the choices.
bool saveSettingRaw(const char* key, int value) {
    SettingsCache& cache = settingsCache();
    int previous = value;
    const bool hadPrevious = cache.get(key, &previous);
    if (hadPrevious && previous == value) return true;
    if (!cache.set(key, value)) return false;
    if (!saveInt(key, value)) {
        if (hadPrevious) cache.set(key, previous);
        return false;
    }
    return true;
}

bool validRule(const String& rule) {
    if (rule.isEmpty() || rule.length() > kTimeZoneRuleMax) return false;
    for (size_t i = 0; i < rule.length(); ++i) {
        const char c = rule[i];
        if (isalnum(static_cast<unsigned char>(c)) || std::strchr("<>+-,./:", c)) continue;
        return false;
    }
    return true;
}

String loadString(const char* key) {
    Preferences p;
    if (!p.begin(kNs, false)) return String();
    const String value = p.isKey(key) ? p.getString(key, "") : String();
    p.end();
    return value;
}
}  // namespace

void settingsApplyTimeZone() {
    String rule = loadString(kKeyRule);
    if (!validRule(rule)) rule = kDefaultTimeZoneRule;
    setenv("TZ", rule.c_str(), 1);
    tzset();
}

String settingsTimeZoneRule() {
    String rule = loadString(kKeyRule);
    return validRule(rule) ? rule : String(kDefaultTimeZoneRule);
}

String settingsTimeZoneName() { return loadString(kKeyName); }

bool settingsSaveTimeZone(const String& rule, const String& name) {
    if (!validRule(rule)) return false;
    Preferences p;
    if (!p.begin(kNs, false)) return false;
    bool ok = p.putString(kKeyRule, rule) == rule.length();
    const String clean = name.length() > kTimeZoneNameMax ? name.substring(0, kTimeZoneNameMax) : name;
    ok = ok && p.putString(kKeyName, clean) == clean.length();
    p.end();
    if (ok) {
        setenv("TZ", rule.c_str(), 1);
        tzset();
        Serial.printf("[setup] time zone %s (%s)\n", clean.length() ? clean.c_str() : "-",
                      rule.c_str());
    }
    return ok;
}

bool settingsGetInt(const char* nvsKey, int* out) {
    if (!nvsKey || !out) return false;
    return settingsCache().get(nvsKey, out);
}

bool settingsSetInt(const char* nvsKey, int value) {
    const SettingDef* def = settingByNvsKey(nvsKey);
    if (!def || !def->nvsKey) return false;
    if (settingValidate(*def, value) != value) return false;
    return saveSettingRaw(nvsKey, value);
}

bool settingsResetAll() {
    Preferences p;
    if (!p.begin(kNs, false)) return false;
    size_t count = 0;
    const SettingDef* all = settingsAll(&count);
    for (size_t i = 0; i < count; ++i)
        if (all[i].nvsKey && all[i].nvsKey[0]) p.remove(all[i].nvsKey);
    // The legacy R4 key would otherwise resurrect Reading first on the next
    // boot, because rv.first is now absent and the migration falls back to it.
    p.remove("order");
    p.end();
    // The token, time zone and display name stay; only settings keys were
    // removed, and the cache now returns every default without a reboot.
    settingsCache().load();
    return true;
}

int settingsBrightness() { return cachedSetting(kKeyBrightness, 128); }
bool settingsSetBrightness(int level) {
    if (level < 8) level = 8;
    if (level > 255) level = 255;
    return settingsSetInt(kKeyBrightness, level);
}

int settingsDimSeconds() { return cachedSetting(kKeyDim, 30); }
bool settingsSetDimSeconds(int seconds) {
    if (seconds < 0) seconds = 0;
    if (seconds > 3600) seconds = 3600;
    return settingsSetInt(kKeyDim, seconds);
}

int settingsBlankSeconds() { return cachedSetting(kKeyBlank, 60); }
bool settingsSetBlankSeconds(int seconds) {
    if (seconds < 0) seconds = 0;
    if (seconds > 3600) seconds = 3600;
    return settingsSetInt(kKeyBlank, seconds);
}

int settingsSleepSeconds() { return cachedSetting(kKeySleep, 120); }
bool settingsSetSleepSeconds(int seconds) {
    if (seconds < 0) seconds = 0;
    if (seconds > 3600) seconds = 3600;
    return settingsSetInt(kKeySleep, seconds);
}

#ifdef KANIKEY_DEV
bool settingsSetSleepSecondsDev(int seconds) {
    if (seconds < 0) seconds = 0;
    if (seconds > 3600) seconds = 3600;
    return saveSettingRaw(kKeySleep, seconds);
}
#endif

int settingsReviewBatchSize() { return cachedSetting(kKeyReviewSize, 20); }
bool settingsSetReviewBatchSize(int count) {
    if (count < 5) count = 5;
    if (count > 50) count = 50;
    return settingsSetInt(kKeyReviewSize, count);
}

int settingsLessonBatchSize() { return cachedSetting(kKeyLessonSize, 5); }
bool settingsSetLessonBatchSize(int count) {
    if (count < 1) count = 1;
    if (count > 10) count = 10;
    return settingsSetInt(kKeyLessonSize, count);
}

bool settingsLessonQuizRequired() { return cachedSetting(kKeyLessonQuiz, 1) != 0; }
bool settingsSetLessonQuizRequired(bool required) {
    return settingsSetInt(kKeyLessonQuiz, required ? 1 : 0);
}

// R4: 1 Meaning, 0 Reading, 2 Random (the default).
FirstPart settingsFirstPart() { return firstPartFromValue(cachedSetting(kKeyFirstPart, 2)); }

bool settingsCriticalFirst() { return cachedSetting(kKeyCriticalFirst, 0) != 0; }
bool settingsSetCriticalFirst(bool criticalFirst) {
    return settingsSetInt(kKeyCriticalFirst, criticalFirst ? 1 : 0);
}

int settingsCriticalPercent() { return cachedSetting(kKeyCriticalPercent, 75); }
bool settingsSetCriticalPercent(int percent) {
    if (percent != 60 && percent != 70 && percent != 75 && percent != 80 && percent != 90)
        percent = 75;
    return settingsSetInt(kKeyCriticalPercent, percent);
}

int settingsCriticalMinAnswers() { return cachedSetting(kKeyCriticalMin, 5); }
bool settingsSetCriticalMinAnswers(int answers) {
    if (answers != 3 && answers != 5 && answers != 10) answers = 5;
    return settingsSetInt(kKeyCriticalMin, answers);
}

int settingsPracticeMistakesWindow() { return cachedSetting(kKeyPracticeMistakes, 86400); }
bool settingsSetPracticeMistakesWindow(int seconds) {
    if (seconds != 86400 && seconds != 3 * 86400 && seconds != 7 * 86400) seconds = 86400;
    return settingsSetInt(kKeyPracticeMistakes, seconds);
}

int settingsPracticeLessonsMode() { return cachedSetting(kKeyPracticeLessons, 0); }
bool settingsSetPracticeLessonsMode(int mode) {
    if (mode < 0) mode = 0;
    if (mode > 3) mode = 3;
    return settingsSetInt(kKeyPracticeLessons, mode);
}

int settingsPracticeSize() { return cachedSetting(kKeyPracticeSize, 10); }
bool settingsSetPracticeSize(int size) {
    if (size != 10 && size != 20) size = 10;
    return settingsSetInt(kKeyPracticeSize, size);
}

int settingsAutoSyncMode() { return cachedSetting(kKeyAutoSync, 1); }
bool settingsSetAutoSyncMode(int mode) {
    if (mode < 0 || mode > 2) mode = 1;
    return settingsSetInt(kKeyAutoSync, mode);
}

int settingsSyncFullEverySeconds() { return cachedSetting(kKeySyncFull, 21600); }
bool settingsSetSyncFullEverySeconds(int seconds) {
    if (seconds != 3600 && seconds != 21600 && seconds != 86400) seconds = 21600;
    return settingsSetInt(kKeySyncFull, seconds);
}

bool settingsPrefetchReviews() { return cachedSetting(kKeyPrefetch, 1) != 0; }
bool settingsSetPrefetchReviews(bool prefetch) {
    return settingsSetInt(kKeyPrefetch, prefetch ? 1 : 0);
}

String formatFixedTimeZone(int offsetMinutes) {
    const int a = offsetMinutes < 0 ? -offsetMinutes : offsetMinutes;
    const char sign = offsetMinutes < 0 ? '-' : '+';
    const char* posixSign = offsetMinutes > 0 ? "-" : "";
    char out[20];
    if (a % 60)
        snprintf(out, sizeof out, "<%c%02d%02d>%s%d:%02d", sign, a / 60, a % 60, posixSign,
                 a / 60, a % 60);
    else
        snprintf(out, sizeof out, "<%c%02d>%s%d", sign, a / 60, posixSign, a / 60);
    return String(out);
}

}  // namespace kk

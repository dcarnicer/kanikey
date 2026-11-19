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
constexpr const char* kKeyMeaningFirst = "order";
constexpr const char* kKeyCriticalFirst = "rv.critical";
constexpr const char* kKeyCriticalPercent = "st.critpct";
constexpr const char* kKeyCriticalMin = "st.critmin";
constexpr const char* kKeyPracticeMistakes = "st.mistakes";
constexpr const char* kKeyPracticeLessons = "st.recent";
constexpr const char* kKeyPracticeSize = "st.practice";
constexpr const char* kKeyAutoSync = "sy.wake";
constexpr const char* kKeySyncFull = "sy.full";
constexpr const char* kKeyPrefetch = "sy.prefetch";

struct DisplaySettings {
    SettingsCache cache;
    bool lessonQuiz = true;
    bool criticalFirst = false;
    int criticalPercent = 75;
    int criticalMin = 5;
    int practiceMistakes = 24 * 3600;
    int practiceLessons = 0;
    int practiceSize = 10;
    int autoSync = 1;  // 1 = Quick, 2 = Full, 0 = Off
    int syncFull = 6 * 3600;
    bool prefetch = true;
};

int readPreference(void* context, const char* key, int fallback) {
    return static_cast<Preferences*>(context)->getInt(key, fallback);
}

DisplaySettings& displaySettings();

int cachedSetting(const char* key, int fallback) {
    int value = fallback;
    displaySettings().cache.get(key, &value);
    return value;
}

DisplaySettings& displaySettings() {
    static DisplaySettings values;
    static bool loaded = false;
    if (loaded) return values;
    loaded = true;
    Preferences p;
    if (!p.begin(kNs, false)) {
        values.cache.load();
    } else {
        values.cache.load(readPreference, &p);
        values.lessonQuiz = p.getInt(kKeyLessonQuiz, values.lessonQuiz ? 1 : 0) != 0;
        values.criticalFirst = p.getInt(kKeyCriticalFirst, values.criticalFirst ? 1 : 0) != 0;
        values.criticalPercent = p.getInt(kKeyCriticalPercent, values.criticalPercent);
        values.criticalMin = p.getInt(kKeyCriticalMin, values.criticalMin);
        values.practiceMistakes = p.getInt(kKeyPracticeMistakes, values.practiceMistakes);
        values.practiceLessons = p.getInt(kKeyPracticeLessons, values.practiceLessons);
        values.practiceSize = p.getInt(kKeyPracticeSize, values.practiceSize);
        values.autoSync = p.getInt(kKeyAutoSync, values.autoSync);
        values.syncFull = p.getInt(kKeySyncFull, values.syncFull);
        values.prefetch = p.getInt(kKeyPrefetch, values.prefetch ? 1 : 0) != 0;
        p.end();
    }
    if (values.criticalPercent != 60 && values.criticalPercent != 70 &&
        values.criticalPercent != 75 && values.criticalPercent != 80 &&
        values.criticalPercent != 90)
        values.criticalPercent = 75;
    if (values.criticalMin != 3 && values.criticalMin != 5 && values.criticalMin != 10)
        values.criticalMin = 5;
    if (values.practiceMistakes < 86400 || values.practiceMistakes > 7 * 86400)
        values.practiceMistakes = 86400;
    if (values.practiceLessons < 0 || values.practiceLessons > 3) values.practiceLessons = 0;
    if (values.practiceSize < 10 || values.practiceSize > 20) values.practiceSize = 10;
    if (values.autoSync < 0 || values.autoSync > 2) values.autoSync = 1;
    if (values.syncFull != 3600 && values.syncFull != 21600 && values.syncFull != 86400)
        values.syncFull = 21600;
    return values;
}

bool saveInt(const char* key, int value) {
    Preferences p;
    if (!p.begin(kNs, false)) return false;
    const bool ok = p.putInt(key, value) == sizeof(int32_t);
    p.end();
    return ok;
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
    return displaySettings().cache.get(nvsKey, out);
}

bool settingsSetInt(const char* nvsKey, int value) {
    const SettingDef* def = settingByNvsKey(nvsKey);
    if (!def || !def->nvsKey) return false;
    if (settingValidate(*def, value) != value) return false;
    SettingsCache& cache = displaySettings().cache;
    int previous = value;
    const bool hadPrevious = cache.get(nvsKey, &previous);
    if (hadPrevious && previous == value) return true;
    if (!cache.set(nvsKey, value)) return false;
    if (!saveInt(nvsKey, value)) {
        if (hadPrevious) cache.set(nvsKey, previous);
        return false;
    }
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
    DisplaySettings& v = displaySettings();
    int previous = seconds;
    const bool hadPrevious = v.cache.get(kKeySleep, &previous);
    if (hadPrevious && previous == seconds) return true;
    if (!v.cache.set(kKeySleep, seconds)) return false;
    if (!saveInt(kKeySleep, seconds)) {
        if (hadPrevious) v.cache.set(kKeySleep, previous);
        return false;
    }
    return true;
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

bool settingsLessonQuizRequired() { return displaySettings().lessonQuiz; }
bool settingsSetLessonQuizRequired(bool required) {
    DisplaySettings& v = displaySettings();
    if (v.lessonQuiz == required) return true;
    v.lessonQuiz = required;
    return saveInt(kKeyLessonQuiz, required ? 1 : 0);
}

bool settingsMeaningFirst() { return cachedSetting(kKeyMeaningFirst, 1) != 0; }
bool settingsSetMeaningFirst(bool meaningFirst) {
    return settingsSetInt(kKeyMeaningFirst, meaningFirst ? 1 : 0);
}

bool settingsCriticalFirst() { return displaySettings().criticalFirst; }
bool settingsSetCriticalFirst(bool criticalFirst) {
    DisplaySettings& v = displaySettings();
    if (v.criticalFirst == criticalFirst) return true;
    v.criticalFirst = criticalFirst;
    return saveInt(kKeyCriticalFirst, criticalFirst ? 1 : 0);
}

int settingsCriticalPercent() { return displaySettings().criticalPercent; }
bool settingsSetCriticalPercent(int percent) {
    if (percent != 60 && percent != 70 && percent != 75 && percent != 80 && percent != 90)
        percent = 75;
    DisplaySettings& v = displaySettings();
    if (v.criticalPercent == percent) return true;
    v.criticalPercent = percent;
    return saveInt(kKeyCriticalPercent, percent);
}

int settingsCriticalMinAnswers() { return displaySettings().criticalMin; }
bool settingsSetCriticalMinAnswers(int answers) {
    if (answers != 3 && answers != 5 && answers != 10) answers = 5;
    DisplaySettings& v = displaySettings();
    if (v.criticalMin == answers) return true;
    v.criticalMin = answers;
    return saveInt(kKeyCriticalMin, answers);
}

int settingsPracticeMistakesWindow() { return displaySettings().practiceMistakes; }
bool settingsSetPracticeMistakesWindow(int seconds) {
    if (seconds != 86400 && seconds != 3 * 86400 && seconds != 7 * 86400) seconds = 86400;
    DisplaySettings& v = displaySettings();
    if (v.practiceMistakes == seconds) return true;
    v.practiceMistakes = seconds;
    return saveInt(kKeyPracticeMistakes, seconds);
}

int settingsPracticeLessonsMode() { return displaySettings().practiceLessons; }
bool settingsSetPracticeLessonsMode(int mode) {
    if (mode < 0) mode = 0;
    if (mode > 3) mode = 3;
    DisplaySettings& v = displaySettings();
    if (v.practiceLessons == mode) return true;
    v.practiceLessons = mode;
    return saveInt(kKeyPracticeLessons, mode);
}

int settingsPracticeSize() { return displaySettings().practiceSize; }
bool settingsSetPracticeSize(int size) {
    if (size != 10 && size != 20) size = 10;
    DisplaySettings& v = displaySettings();
    if (v.practiceSize == size) return true;
    v.practiceSize = size;
    return saveInt(kKeyPracticeSize, size);
}

int settingsAutoSyncMode() { return displaySettings().autoSync; }
bool settingsSetAutoSyncMode(int mode) {
    if (mode < 0 || mode > 2) mode = 1;
    DisplaySettings& v = displaySettings();
    if (v.autoSync == mode) return true;
    v.autoSync = mode;
    return saveInt(kKeyAutoSync, mode);
}

int settingsSyncFullEverySeconds() { return displaySettings().syncFull; }
bool settingsSetSyncFullEverySeconds(int seconds) {
    if (seconds != 3600 && seconds != 21600 && seconds != 86400) seconds = 21600;
    DisplaySettings& v = displaySettings();
    if (v.syncFull == seconds) return true;
    v.syncFull = seconds;
    return saveInt(kKeySyncFull, seconds);
}

bool settingsPrefetchReviews() { return displaySettings().prefetch; }
bool settingsSetPrefetchReviews(bool prefetch) {
    DisplaySettings& v = displaySettings();
    if (v.prefetch == prefetch) return true;
    v.prefetch = prefetch;
    return saveInt(kKeyPrefetch, prefetch ? 1 : 0);
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

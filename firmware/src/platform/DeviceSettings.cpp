#include "platform/DeviceSettings.h"

#include <Preferences.h>

#include <cctype>
#include <cstdlib>
#include <cstring>

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
constexpr const char* kKeyMeaningFirst = "order";

struct DisplaySettings {
    int brightness = 128;
    int dim = 30;
    int blank = 60;
    int sleep = 120;
    int reviewSize = 20;
    int lessonSize = 5;
    bool meaningFirst = true;
};

DisplaySettings& displaySettings() {
    static DisplaySettings values;
    static bool loaded = false;
    if (loaded) return values;
    loaded = true;
    Preferences p;
    if (!p.begin(kNs, false)) return values;
    values.brightness = p.getInt(kKeyBrightness, values.brightness);
    values.dim = p.getInt(kKeyDim, values.dim);
    values.blank = p.getInt(kKeyBlank, values.blank);
    values.sleep = p.getInt(kKeySleep, values.sleep);
    values.reviewSize = p.getInt(kKeyReviewSize, values.reviewSize);
    values.lessonSize = p.getInt(kKeyLessonSize, values.lessonSize);
    values.meaningFirst = p.getInt(kKeyMeaningFirst, values.meaningFirst ? 1 : 0) != 0;
    p.end();
    if (values.brightness < 8 || values.brightness > 255) values.brightness = 128;
    for (int* v : {&values.dim, &values.blank, &values.sleep})
        if (*v < 0 || *v > 3600) *v = 0;
    if (values.reviewSize < 5 || values.reviewSize > 50) values.reviewSize = 20;
    if (values.lessonSize < 1 || values.lessonSize > 10) values.lessonSize = 5;
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

int settingsBrightness() { return displaySettings().brightness; }
bool settingsSetBrightness(int level) {
    if (level < 8) level = 8;
    if (level > 255) level = 255;
    DisplaySettings& v = displaySettings();
    v.brightness = level;
    return saveInt(kKeyBrightness, level);
}

int settingsDimSeconds() { return displaySettings().dim; }
bool settingsSetDimSeconds(int seconds) {
    if (seconds < 0) seconds = 0;
    if (seconds > 3600) seconds = 3600;
    DisplaySettings& v = displaySettings();
    v.dim = seconds;
    return saveInt(kKeyDim, seconds);
}

int settingsBlankSeconds() { return displaySettings().blank; }
bool settingsSetBlankSeconds(int seconds) {
    if (seconds < 0) seconds = 0;
    if (seconds > 3600) seconds = 3600;
    DisplaySettings& v = displaySettings();
    v.blank = seconds;
    return saveInt(kKeyBlank, seconds);
}

int settingsSleepSeconds() { return displaySettings().sleep; }
bool settingsSetSleepSeconds(int seconds) {
    if (seconds < 0) seconds = 0;
    if (seconds > 3600) seconds = 3600;
    DisplaySettings& v = displaySettings();
    v.sleep = seconds;
    return saveInt(kKeySleep, seconds);
}

int settingsReviewBatchSize() { return displaySettings().reviewSize; }
bool settingsSetReviewBatchSize(int count) {
    if (count < 5) count = 5;
    if (count > 50) count = 50;
    DisplaySettings& v = displaySettings();
    v.reviewSize = count;
    return saveInt(kKeyReviewSize, count);
}

int settingsLessonBatchSize() { return displaySettings().lessonSize; }
bool settingsSetLessonBatchSize(int count) {
    if (count < 1) count = 1;
    if (count > 10) count = 10;
    DisplaySettings& v = displaySettings();
    v.lessonSize = count;
    return saveInt(kKeyLessonSize, count);
}

bool settingsMeaningFirst() { return displaySettings().meaningFirst; }
bool settingsSetMeaningFirst(bool meaningFirst) {
    DisplaySettings& v = displaySettings();
    v.meaningFirst = meaningFirst;
    return saveInt(kKeyMeaningFirst, meaningFirst ? 1 : 0);
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

// REM-02 device side: the reminder timer across deep sleep. See ReminderController.h.
#include "app/ReminderController.h"

#include <Arduino.h>
#include <esp_attr.h>
#include <time.h>

#include "app/CacheController.h"
#include "board/Board.h"
#include "platform/DeviceSettings.h"
#include "wk/Reminder.h"
#include "wk/Time.h"

namespace kk::app {
namespace {

// Earlier than this is not a real clock (effectiveNow() uses the same bound).
constexpr uint32_t kTrustedEpoch = 1700000000u;

// RTC memory survives deep sleep. gRemArmed is set only by reminderArmForSleep()
// right before a timer sleep, and cleared by the boot that reads it, so a stale
// value from a probe or a power-on is never used. gRemFiredAt lives in one boot.
RTC_DATA_ATTR uint32_t gRemArmed = 0;
RTC_DATA_ATTR uint32_t gRemEpoch = 0;    // UTC at the arm
RTC_DATA_ATTR uint64_t gRemRtcUs = 0;    // rtcMicros() at the arm
RTC_DATA_ATTR uint32_t gRemFiredAt = 0;  // the banner's wake time in this boot

// Seconds east of UTC at `utc`, from the POSIX TZ rule in force (set by
// settingsApplyTimeZone()): the local wall clock as if it were UTC, minus UTC.
int32_t localOffsetAt(uint32_t utc) {
    const time_t t = static_cast<time_t>(utc);
    struct tm tm {};
    if (!localtime_r(&t, &tm)) return 0;
    const int64_t wall =
        daysFromCivil(tm.tm_year + 1900, static_cast<unsigned>(tm.tm_mon + 1),
                      static_cast<unsigned>(tm.tm_mday)) *
            86400 +
        tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
    return static_cast<int32_t>(wall - static_cast<int64_t>(utc));
}

// M1 (count), M2 (choice), M3 (minutes). The defaults are the registry's: M2 is
// 22-08, M1 and M3 are off.
kk::ReminderRules rulesFromSettings() {
    int minDue = 0;
    int quiet = 1;
    int daily = 0;
    kk::settingsGetInt("rm.remind", &minDue);
    kk::settingsGetInt("rm.quiet", &quiet);
    kk::settingsGetInt("rm.time", &daily);
    return kk::reminderRulesFromChoices(minDue, quiet, daily);
}

[[maybe_unused]] void logNext(uint32_t nowUtc, const kk::ReminderWake& w) {
#ifdef KANIKEY_DEV
    if (!w.at) {
        Serial.println("[rem] next wake at none (in 0 s) reason none");
        return;
    }
    Serial.printf("[rem] next wake at %s (in %u s) reason %s%s\n",
                  kk::formatIso8601(w.at).c_str(), static_cast<unsigned>(w.at - nowUtc),
                  w.reason == kk::ReminderReason::Bucket ? "bucket" : "daily",
                  w.clamped ? " (24 h cap)" : "");
#else
    (void)nowUtc;
    (void)w;
#endif
}

}  // namespace

bool reminderBootWake(uint32_t* nowUtc) {
    const bool armed = gRemArmed != 0;
    gRemArmed = 0;
    gRemFiredAt = 0;
    if (!armed || !tgc::board::wokeFromTimer() || !nowUtc) return false;
    const uint64_t rtcNow = tgc::board::rtcMicros();
    if (rtcNow < gRemRtcUs) return false;  // the RTC restarted: no clock to rebuild
    const uint64_t epoch = static_cast<uint64_t>(gRemEpoch) + (rtcNow - gRemRtcUs) / 1000000ULL;
    if (epoch > 0xFFFFFFFFULL) return false;
    *nowUtc = static_cast<uint32_t>(epoch);
    return true;
}

namespace {

// The arm itself: the rules, the next wake at nowUtc, and the clock record.
uint64_t armAt(uint32_t nowUtc, const kk::Summary* summary) {
    gRemArmed = 0;
    const kk::ReminderRules rules = rulesFromSettings();
    if (rules.minDue == 0 && rules.dailyMinute == 0) {
#ifdef KANIKEY_DEV
        Serial.println("[rem] M1 and M3 off: no reminder timer");
#endif
        return 0;  // the default: the same deep sleep as before REM-02
    }
    if (nowUtc < kTrustedEpoch) {
#ifdef KANIKEY_DEV
        Serial.println("[rem] no trusted clock: no reminder timer");
#endif
        return 0;
    }
    static const kk::Summary kNoCache;
    const kk::Summary& forecast = summary ? *summary : kNoCache;
    const kk::ReminderWake w =
        kk::reminderNextWakeAt(forecast, nowUtc, gRemFiredAt, rules, localOffsetAt);
    logNext(nowUtc, w);
    if (!w.at) return 0;
    gRemEpoch = nowUtc;
    gRemRtcUs = tgc::board::rtcMicros();
    gRemArmed = 1;
    return static_cast<uint64_t>(w.at - nowUtc) * 1000000ULL;
}

}  // namespace

uint64_t reminderArmIdle(const kk::Summary* summary) {
    gRemArmed = 0;
    const kk::ReminderRules rules = rulesFromSettings();
    if (rules.minDue == 0 && rules.dailyMinute == 0) return 0;  // reads no clock
    return armAt(effectiveNow(), summary);
}

uint64_t reminderArmAfterWake(uint32_t nowUtc, const kk::Summary* summary) {
    return armAt(nowUtc, summary);
}

bool reminderBannerWanted(uint32_t dueReviews, uint32_t nowUtc) {
    return kk::reminderWanted(dueReviews, nowUtc, rulesFromSettings(), localOffsetAt);
}

void reminderNoteFired(uint32_t nowUtc) { gRemFiredAt = nowUtc; }

}  // namespace kk::app

// REM-02 device side: the reminder timer across deep sleep. See ReminderController.h.
#include "app/ReminderController.h"

#include <Arduino.h>
#include <esp_attr.h>
#include <time.h>

#include "app/CacheController.h"
#include "board/Board.h"
#include "platform/DeviceSettings.h"
#include "wk/PaperCadence.h"
#include "wk/Reminder.h"
#include "wk/Time.h"
#if defined(TGC_BOARD_PAPERDIY)
#include "app/CadenceController.h"
#endif
#ifdef KANIKEY_DEV
#include "app/RemindDev.h"
#include "wk/DevRemind.h"
#endif

namespace kk::app {
namespace {

// Earlier than this is not a real clock (effectiveNow() uses the same bound).
constexpr uint32_t kTrustedEpoch = 1700000000u;

// RTC memory survives deep sleep. gRemArmed is set only by the arm right before
// a timer sleep, and cleared by the boot that reads it, so a stale value from a
// probe or a power-on is never used. gRemFiredAt lives in one boot.
RTC_DATA_ATTR uint32_t gRemArmed = 0;
RTC_DATA_ATTR uint32_t gRemEpoch = 0;    // UTC at the arm
RTC_DATA_ATTR uint64_t gRemRtcUs = 0;    // rtcMicros() at the arm
RTC_DATA_ATTR uint32_t gRemFiredAt = 0;  // the banner's wake time in this boot
#ifdef KANIKEY_DEV
// dev `remind`: this run's timers treat M1 Off as 1. Kept across timer wakes;
// any other boot clears it (reminderBootWake()).
RTC_DATA_ATTR uint32_t gRemDevRun = 0;
#endif
// PD-04: the UTC targets of the armed timer (0 = none): the reminder's and the
// sync cadence's, and the cadence's CadenceReason for the log.
#if defined(TGC_BOARD_PAPERDIY)
RTC_DATA_ATTR uint32_t gRemAt = 0;
RTC_DATA_ATTR uint32_t gCadAt = 0;
RTC_DATA_ATTR uint8_t gCadWhy = 0;
// The targets the timer that woke this boot was armed for, copied by
// reminderBootWake() before the RTC copies are cleared.
uint32_t gBootRemAt = 0;
uint32_t gBootCadAt = 0;
#endif

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
#ifdef KANIKEY_DEV
    if (gRemDevRun) minDue = static_cast<int>(kk::devRemindMinDue(static_cast<uint32_t>(minDue)));
#endif
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

// PD-04: the one timer's decision, as the dev log names it.
[[maybe_unused]] void logPlan(uint32_t nowUtc, const kk::WakePlan& p) {
#ifdef KANIKEY_DEV
    if (!p.at) {
        Serial.printf("[pd] next wake none (in 0 s) reason %s\n", kk::wakeReasonName(p.reason));
        return;
    }
    Serial.printf("[pd] next wake %s in %u s reason %s\n", kk::formatIso8601(p.at).c_str(),
                  static_cast<unsigned>(p.at - nowUtc), kk::wakeReasonName(p.reason));
#else
    (void)nowUtc;
    (void)p;
#endif
}

// The arm itself: the reminder target, and on the PaperDIY the sync cadence
// target, one timer for both. `keepCadence` (a reminder wake) keeps a cadence
// target that is still in the future; a cadence that has fired is recomputed.
uint64_t armAt(uint32_t nowUtc, const kk::Summary* summary, bool keepCadence) {
    gRemArmed = 0;
    const kk::ReminderRules rules = rulesFromSettings();
    const bool trusted = nowUtc >= kTrustedEpoch;
    uint32_t remAt = 0;
    if (trusted && (rules.minDue != 0 || rules.dailyMinute != 0)) {
        static const kk::Summary kNoCache;
        const kk::Summary& forecast = summary ? *summary : kNoCache;
        const kk::ReminderWake w =
            kk::reminderNextWakeAt(forecast, nowUtc, gRemFiredAt, rules, localOffsetAt);
#if !defined(TGC_BOARD_PAPERDIY)
        logNext(nowUtc, w);
#endif
        remAt = w.at;
    }
    uint32_t wakeAt = remAt;
#if defined(TGC_BOARD_PAPERDIY)
    kk::CadenceWake cad;
    if (keepCadence && gCadAt && !kk::timerDue(gCadAt, nowUtc)) {
        cad.at = gCadAt;
        cad.reason = static_cast<kk::CadenceReason>(gCadWhy);
    } else if (trusted) {
        cad = kk::app::cadenceWakeAfter(nowUtc, summary, rules, localOffsetAt);
    }
    const kk::WakePlan plan = kk::planWake(remAt, cad);
    logPlan(nowUtc, plan);
    gRemAt = remAt;
    gCadAt = cad.at;
    gCadWhy = static_cast<uint8_t>(cad.reason);
    wakeAt = plan.at;
#else
    (void)keepCadence;
#endif
#ifdef KANIKEY_DEV
    remindDevNoteArm(nowUtc, wakeAt);
#endif
    if (!wakeAt) return 0;
    gRemEpoch = nowUtc;
    gRemRtcUs = tgc::board::rtcMicros();
    gRemArmed = 1;
    return static_cast<uint64_t>(wakeAt - nowUtc) * 1000000ULL;
}

}  // namespace

bool reminderBootWake(uint32_t* nowUtc) {
    const bool armed = gRemArmed != 0;
#if defined(TGC_BOARD_PAPERDIY)
    gBootRemAt = gRemAt;
    gBootCadAt = gCadAt;
    gRemAt = 0;
    gCadAt = 0;
#endif
    gRemArmed = 0;
    gRemFiredAt = 0;
#ifdef KANIKEY_DEV
    if (!tgc::board::wokeFromTimer()) gRemDevRun = 0;
#endif
    if (!armed || !tgc::board::wokeFromTimer() || !nowUtc) return false;
    const uint64_t rtcNow = tgc::board::rtcMicros();
    if (rtcNow < gRemRtcUs) return false;  // the RTC restarted: no clock to rebuild
    const uint64_t epoch = static_cast<uint64_t>(gRemEpoch) + (rtcNow - gRemRtcUs) / 1000000ULL;
    if (epoch > 0xFFFFFFFFULL) return false;
    *nowUtc = static_cast<uint32_t>(epoch);
    return true;
}

#if defined(TGC_BOARD_PAPERDIY)
bool bootReminderDue(uint32_t nowUtc) { return kk::timerDue(gBootRemAt, nowUtc); }

bool bootCadenceDue(uint32_t nowUtc) { return kk::timerDue(gBootCadAt, nowUtc); }
#endif

uint64_t reminderArmIdle(const kk::Summary* summary) {
    gRemArmed = 0;
#if !defined(TGC_BOARD_PAPERDIY)
    const kk::ReminderRules rules = rulesFromSettings();
    if (rules.minDue == 0 && rules.dailyMinute == 0) return 0;  // reads no clock
#endif
    return armAt(effectiveNow(), summary, false);
}

uint64_t reminderArmAfterWake(uint32_t nowUtc, const kk::Summary* summary) {
    return armAt(nowUtc, summary, true);
}

#ifdef KANIKEY_DEV
uint64_t reminderArmDev(uint32_t seconds) {
    const uint32_t now = effectiveNow();
    if (now < kTrustedEpoch || seconds == 0) return 0;
    gRemArmed = 1;
    gRemEpoch = now;
    gRemRtcUs = tgc::board::rtcMicros();
    gRemFiredAt = 0;
    gRemDevRun = 1;
    remindDevNoteArm(now, now + seconds);
    return static_cast<uint64_t>(seconds) * 1000000ULL;
}
#endif

bool reminderBannerWanted(uint32_t dueReviews, uint32_t nowUtc) {
    return kk::reminderWanted(dueReviews, nowUtc, rulesFromSettings(), localOffsetAt);
}

void reminderNoteFired(uint32_t nowUtc) { gRemFiredAt = nowUtc; }

}  // namespace kk::app

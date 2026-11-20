// REM-02/REM-03 dev checks. See RemindDev.h.
#include "app/RemindDev.h"

#ifdef KANIKEY_DEV
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>

#include <cstring>
#include <string>
#include <string_view>

#include "app/AppState.h"
#include "app/CacheController.h"
#include "app/ReminderController.h"
#include "app/SettingsController.h"
#include "board/Board.h"
#include "platform/Sound.h"
#include "wk/DevRemind.h"
#include "wk/ScreenModel.h"
#include "wk/SoundPlan.h"
#include "wk/Time.h"

namespace kk::app {
namespace {

constexpr uint32_t kRemindMagic = 0x52454D44;  // "REMD"
constexpr uint32_t kHistory = 8;               // timer wakes kept in the ring

// Wake causes, as the record stores them.
constexpr uint32_t kWakeTimer = 1;
constexpr uint32_t kWakeKey = 2;
constexpr uint32_t kWakeOther = 3;  // a reset or a power-on

// Outcome of one timer wake.
constexpr uint32_t kOutPending = 0;
constexpr uint32_t kOutKey = 1;       // the banner: a key continued the boot
constexpr uint32_t kOutExpiry = 2;    // the banner: 30 s passed, the device re-arms
constexpr uint32_t kOutNoBanner = 3;  // no banner (nothing due), the device re-arms

// Why a run ended (`endedBy`).
constexpr uint32_t kEndedNone = 0;
constexpr uint32_t kEndedKey = 2;
constexpr uint32_t kEndedOther = 3;     // a reset or a power-on
constexpr uint32_t kEndedNoNext = 4;    // the last wake armed no further timer

struct RemindWake {
    uint32_t cause;
    uint32_t rebuiltAt;  // UTC rebuilt from the RTC; 0 when it was not rebuilt
    uint32_t expectAt;   // the target this wake was armed for
    int32_t driftS;      // rebuiltAt - expectAt, seconds
    uint32_t due;
    uint32_t banner;     // 0 no, 1 yes
    uint32_t outcome;
    uint32_t nextAt;     // the next armed wake, 0 none
};

// RTC_NOINIT keeps the record through the deep sleep and through the reset that
// opening the USB port causes, so `remind` after that reset still shows the wake.
// RTC_DATA_ATTR would be re-initialised by that reset. A power cut leaves garbage,
// which the magic rejects.
struct RemindRecord {
    uint32_t magic;
    uint32_t runs;        // `remind <s>` arms since power-on
    uint32_t active;      // the run's timer is armed
    uint32_t requestedS;  // seconds of the run's first arm
    uint32_t expectAt;    // the target of the timer in progress
    uint32_t timerWakes;  // timer wakes of this run
    uint32_t endedBy;
    RemindWake hist[kHistory];
};

RTC_NOINIT_ATTR RemindRecord gRemind;

bool recordValid() { return gRemind.magic == kRemindMagic; }

void ensureRecord() {
    if (recordValid()) return;
    gRemind = RemindRecord{};
    gRemind.magic = kRemindMagic;
}

bool haveWake() { return gRemind.timerWakes > 0; }

RemindWake& lastWake() { return gRemind.hist[(gRemind.timerWakes - 1) % kHistory]; }

std::string when(uint32_t at) { return at ? kk::formatIso8601(at) : std::string("none"); }

const char* outcomeName(uint32_t outcome) {
    switch (outcome) {
        case kOutKey: return "key";
        case kOutExpiry: return "expiry";
        case kOutNoBanner: return "no-banner";
        default: return "pending";
    }
}

const char* endedName(uint32_t endedBy) {
    switch (endedBy) {
        case kEndedKey: return "key";
        case kEndedOther: return "reset";
        case kEndedNoNext: return "no-next-wake";
        default: return "none";
    }
}

void printRecord() {
    ensureRecord();
    Serial.printf("[remind] record runs %u active %u requested_s %u timer_wakes %u ended_by %s expect %s\n",
                  static_cast<unsigned>(gRemind.runs), static_cast<unsigned>(gRemind.active),
                  static_cast<unsigned>(gRemind.requestedS),
                  static_cast<unsigned>(gRemind.timerWakes), endedName(gRemind.endedBy),
                  when(gRemind.expectAt).c_str());
    const uint32_t first = gRemind.timerWakes > kHistory ? gRemind.timerWakes - kHistory : 0;
    for (uint32_t i = first; i < gRemind.timerWakes; ++i) {
        const RemindWake& w = gRemind.hist[i % kHistory];
        Serial.printf("[remind] hist %u cause timer rebuilt %s expected %s drift %d s due %u banner %s "
                      "outcome %s next %s\n",
                      static_cast<unsigned>(i + 1), when(w.rebuiltAt).c_str(),
                      when(w.expectAt).c_str(), static_cast<int>(w.driftS),
                      static_cast<unsigned>(w.due), w.banner ? "yes" : "no", outcomeName(w.outcome),
                      when(w.nextAt).c_str());
    }
}

const char* volumeName(int choice) {
    if (choice == 0) return "low";
    if (choice == 2) return "high";
    return "med";
}

uint32_t cueLengthMs(const kk::SoundSequence& seq) {
    uint32_t total = 0;
    for (size_t i = 0; i < seq.count; ++i) total += seq.steps[i].durationMs;
    return total;
}

}  // namespace

void remindDevBoot(bool timerWake, bool clockRebuilt, uint32_t nowUtc) {
    ensureRecord();
    if (!gRemind.active) return;  // no remind run: nothing to log
    if (!timerWake) {
        // A key wake or a reset or power-on ends the run; its history stays.
        const bool key = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1;
        gRemind.active = 0;
        gRemind.endedBy = key ? kEndedKey : kEndedOther;
        Serial.printf("[remind] run ended by a %s: normal boot\n", key ? "key wake" : "reset or power-on");
        return;
    }
    RemindWake w{};
    w.cause = kWakeTimer;
    w.rebuiltAt = clockRebuilt ? nowUtc : 0;
    w.expectAt = gRemind.expectAt;
    w.driftS = clockRebuilt ? static_cast<int32_t>(kk::remindDriftSeconds(nowUtc, gRemind.expectAt)) : 0;
    w.outcome = kOutPending;
    gRemind.timerWakes += 1;
    lastWake() = w;
    Serial.printf("[remind] timer wake %u: rebuilt %s expected %s drift %d s\n",
                  static_cast<unsigned>(gRemind.timerWakes),
                  clockRebuilt ? when(nowUtc).c_str() : "none", when(gRemind.expectAt).c_str(),
                  static_cast<int>(w.driftS));
}

void remindDevWakeDecision(uint32_t due, bool banner) {
    if (!gRemind.active || !haveWake()) return;
    RemindWake& w = lastWake();
    w.due = due;
    w.banner = banner ? 1 : 0;
    if (!banner) w.outcome = kOutNoBanner;
}

void remindDevBannerEnd(bool key) {
    if (!gRemind.active || !haveWake()) return;
    lastWake().outcome = key ? kOutKey : kOutExpiry;
    if (key) {
        gRemind.active = 0;
        gRemind.endedBy = kEndedKey;
    }
}

void remindDevNoteArm(uint32_t nowUtc, uint32_t wakeAt) {
    if (!gRemind.active) return;
    gRemind.expectAt = wakeAt;
    if (haveWake()) lastWake().nextAt = wakeAt;
    if (!wakeAt) {
        gRemind.active = 0;
        gRemind.endedBy = kEndedNoNext;
        Serial.println("[remind] no next wake: the device sleeps until a key");
        return;
    }
    Serial.printf("[remind] next wake %s in %u s\n", when(wakeAt).c_str(),
                  static_cast<unsigned>(wakeAt > nowUtc ? wakeAt - nowUtc : 0));
}

void remindSerialCommand(const String& line) {
    String rest = line.substring(6);  // after "remind"
    rest.trim();
    if (rest.isEmpty()) {
        printRecord();
        return;
    }
    uint32_t seconds = 0;
    if (!kk::parseRemindSeconds(std::string_view(rest.c_str(), rest.length()), &seconds)) {
        Serial.println("[remind] use: remind <seconds 1-86400> (arms and sleeps), remind (log)");
        return;
    }
    if (kk::screenBase(state().screen) == kk::Screen::Setup || state().sync.gSyncJob.active) {
        Serial.println("[remind] refused: a sync or the setup portal is open");
        return;
    }
    ensureRecord();
    gRemind.runs += 1;
    gRemind.active = 1;
    gRemind.requestedS = seconds;
    gRemind.timerWakes = 0;
    gRemind.endedBy = kEndedNone;
    const uint64_t timerUs = reminderArmDev(seconds);
    if (!timerUs) {
        gRemind.active = 0;
        Serial.println("[remind] no clock (no synced time and no cache): sync once, then retry");
        return;
    }
    Serial.printf("[remind] run %u armed: %u s, key wake armed, sleeping now\n",
                  static_cast<unsigned>(gRemind.runs), static_cast<unsigned>(seconds));
    saveCacheSnapshot();
    firmwareBeforeSleep();
    tgc::board::deepSleep(timerUs);
}

void cueSerialCommand(const String& line) {
    String rest = line.substring(3);  // after "cue"
    rest.trim();
    static const char kUsage[] = "[cue] use: cue <chime|correct|wrong|end>, cue vol [low|med|high]";
    if (rest == "vol" || rest.startsWith("vol ")) {
        String arg = rest.substring(3);
        arg.trim();
        if (arg.isEmpty()) {
            Serial.printf("[cue] level %u for dev cues (M5 Volume unless `cue vol` set it)\n",
                          static_cast<unsigned>(kk::sound::soundDevLevel()));
            return;
        }
        int choice = 0;
        if (!kk::parseCueVolume(std::string_view(arg.c_str(), arg.length()), &choice)) {
            Serial.println(kUsage);
            return;
        }
        kk::sound::soundDevSetVolume(choice);
        Serial.printf("[cue] volume %s, level %u, until reboot\n", volumeName(choice),
                      static_cast<unsigned>(kk::sound::soundDevLevel()));
        return;
    }
    kk::SoundCue cue = kk::SoundCue::ReminderChime;
    if (!kk::parseCueName(std::string_view(rest.c_str(), rest.length()), &cue)) {
        Serial.println(kUsage);
        return;
    }
    if (state().sync.gSyncJob.active) {
        Serial.println("[cue] refused: a sync is running");
        return;
    }
    // A session-end cue needs one completed review to have any steps.
    const kk::SoundSequence seq = kk::soundSequence(cue, 1);
    const uint32_t lengthMs = cueLengthMs(seq);
    const uint32_t freeBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const uint32_t largestBefore =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t level = kk::sound::soundDevLevel();
    const bool played = kk::sound::soundDevCue(cue, 1);
    // The cue runs from soundTick(); the loop does not run while this command
    // waits, so the command ticks it itself.
    const uint32_t startedMs = millis();
    while (played && millis() - startedMs < lengthMs + 20) {
        tgc::board::update();
        kk::sound::soundTick(millis());
        delay(2);
    }
    kk::sound::soundStop();
    Serial.printf("[cue] %s heap before free %u largest %u; after free %u largest %u min %u; "
                  "played %d (level %u, %u ms)\n",
                  rest.c_str(), static_cast<unsigned>(freeBefore),
                  static_cast<unsigned>(largestBefore),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
                  played ? 1 : 0, static_cast<unsigned>(level), static_cast<unsigned>(lengthMs));
}

}  // namespace kk::app
#endif

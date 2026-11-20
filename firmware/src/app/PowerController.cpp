// Status frame, screen wake and the idle dim/blank/sleep tick.
#include <Arduino.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <time.h>

#include <algorithm>
#include <cstring>

#include "app/AboutScreen.h"
#include "app/App.h"
#include "app/AppState.h"
#include "app/AppUi.h"
#include "app/BootScreen.h"
#include "app/CacheController.h"
#include "app/DebugScreens.h"
#include "app/DevConsole.h"
#include "app/FirmwareScreen.h"
#include "app/HomeScreen.h"
#include "app/LessonController.h"
#include "app/LessonScreens.h"
#include "app/LoadingScreen.h"
#include "app/ModalScreens.h"
#include "app/OutboxController.h"
#include "app/OutboxScreen.h"
#include "app/PowerController.h"
#include "app/ReminderController.h"
#include "app/ReviewController.h"
#include "app/ReviewScreens.h"
#include "app/SettingsController.h"
#include "app/SettingsScreen.h"
#include "app/SetupController.h"
#include "app/SetupScreen.h"
#include "app/StatsController.h"
#include "app/StatsScreen.h"
#include "app/StudyController.h"
#include "app/StudyScreens.h"
#include "app/SyncController.h"
#include "app/SyncScreen.h"
#include "board/Board.h"
#include "Version.h"
#include "platform/AudioCache.h"
#include "platform/CacheStore.h"
#include "platform/CriticalStore.h"
#include "platform/DeviceSettings.h"
#include "platform/DevToken.h"
#include "platform/DevWifi.h"
#include "platform/GlyphPack.h"
#include "platform/JournalStore.h"
#include "platform/OutboxStore.h"
#include "platform/Ota.h"
#include "platform/PracticeStore.h"
#include "platform/SetupPortal.h"
#include "platform/SubjectCatalog.h"
#include "platform/WkClient.h"
#include "platform/WkLessons.h"
#include "platform/WkLevelProgress.h"
#include "platform/WkProbe.h"
#include "platform/WkReview.h"
#include "platform/WkReviewStatistics.h"
#include "platform/WkSrsCounts.h"
#include "wk/AnswerChecker.h"
#include "wk/IdleTimer.h"
#include "wk/IdSample.h"
#include "wk/LessonOrder.h"
#include "wk/LessonQuiz.h"
#include "wk/Outbox.h"
#include "wk/ReviewQueue.h"
#include "wk/ReviewResponse.h"
#include "wk/RomajiIme.h"
#include "wk/SessionStats.h"
#include "wk/SleepPolicy.h"
#include "wk/Stats.h"
#include "wk/SubjectText.h"
#include "wk/Time.h"
#include "ui/Theme.h"

namespace kk::app {
namespace {

using tgc::Key;
using tgc::KeyEvent;
using kk::Screen;
using namespace kk::ui;

// P5 "Light sleep" defaults to On; the registry row may not exist on a device
// that never stored it.
bool lightSleepEnabled() {
    int value = 1;
    kk::settingsGetInt("dp.lightsleep", &value);
    return value != 0;
}

// P6 "Sleep during review" defaults to Never; the stored value is the choice's
// meaning (0 or 600 seconds), not its menu index.
kk::SleepDuringReview reviewSleepPolicy() {
    int value = 0;
    kk::settingsGetInt("dp.revsleep", &value);
    return kk::sleepDuringReviewFromValue(value);
}

// A review or its summary owns the screen. Modals (Pause, Settings, help) keep
// the old "no sleep" rule and are handled by screenCanSleep().
bool reviewScreenOpen() {
    if (state().screen.depth != 0) return false;
    const Screen base = kk::screenBase(state().screen);
    return base == Screen::Review || base == Screen::ReviewSummary;
}

// One blank-panel light-sleep period. Returns true when a key woke it: the
// screen is back and the loop may resume. False means a timer woke it (or the
// deep-sleep deadline was already due); the panel is blank again and the caller
// runs the deep-sleep check. The timer slice is recomputed after an early
// timer wake, so a slightly early RTC does not strand the device with the panel
// off and no pending timer.
bool lightSleepUntilDeadline(uint32_t blankMs, uint32_t sleepMs, bool reviewOpen,
                             kk::SleepDuringReview policy) {
    const uint32_t lastInput = state().ui.gLastInputAt;
    for (;;) {
        const uint32_t now = millis();
        const uint32_t idleMs = kk::idleAgeMs(now, lastInput);
        if (kk::deepSleepAllowed(reviewOpen, policy, idleMs) &&
            (reviewOpen || kk::sleepDeadlineReached(idleMs, sleepMs)))
            return false;
        const uint32_t slice =
            kk::lightSleepTimerMs(idleMs, blankMs, sleepMs, reviewOpen, policy);
        Serial.printf("[power] light sleep: idle %u ms, timer %u ms\n",
                      static_cast<unsigned>(idleMs), static_cast<unsigned>(slice));
        const uint64_t startedUs = tgc::board::rtcMicros();
        const bool keyWake = tgc::board::lightSleep(slice);
        const uint64_t sleptUs = tgc::board::rtcMicros() - startedUs;
        Serial.printf("[power] light sleep: %s wake after %llu us (timer %d)\n",
                      keyWake ? "key" : "timer", static_cast<unsigned long long>(sleptUs),
                      tgc::board::wokeFromTimer() ? 1 : 0);
        if (keyWake) {
            state().ui.gLastInputAt = millis();
            wakeScreen();
            return true;
        }
        // The board brings the panel back before returning; keep it off for the
        // next slice or the deep-sleep path.
        tgc::board::setDisplayOn(false);
        state().ui.gScreenOff = true;
    }
}

}  // namespace

bool devForceBattery() {
#ifdef KANIKEY_DEV
    return state().storage.gDevForceBattery;
#else
    return false;
#endif
}

void drawStatus() {
    if (!state().display.gDisp) return;
    const Screen base = kk::screenBase(state().screen);
    if (base == Screen::Debug) {
#if defined(KANIKEY_DEV) && !defined(TGC_BOARD_PAPERDIY)
        // The old flag kept Home set behind Tab, so the debug screen only
        // refreshed itself when it had been opened from Boot. E-paper (PD-02)
        // does not repaint it once a second: that would refresh the panel
        // every second; the screen redraws on its keys instead.
        if (state().dev.debugReturn == Screen::Boot) showDebug();
#endif
        return;
    }
    if (base != Screen::Boot) return;
    // The boot screen is redrawn on state changes through state().display.gDirty.
}

void wakeScreen() {
    if (!state().ui.gDimmed && !state().ui.gScreenOff) return;
    tgc::board::setDisplayOn(true);
    tgc::board::setBrightness(kk::settingsBrightness());
    state().ui.gDimmed = false;
    state().ui.gScreenOff = false;
    state().display.gDirty = true;
}

// Consumes the wake key/button while the panel is off and wakes the screen on
// the dimmed path. Returns true when loop() must not process the frame.
bool powerWakeCheck(uint32_t now) {
    if (state().ui.gScreenOff) {
        bool wake = false;
        KeyEvent ignored;
        while (tgc::board::nextKeyEvent(&ignored)) wake = true;
        if (tgc::board::wasPressed(tgc::board::Button::Select) ||
            tgc::board::wasHeld(tgc::board::Button::Select))
            wake = true;
        // A manual sync is a foreground operation with a progress screen. It
        // must not sit frozen behind a blank panel waiting for a key; wake it
        // and let syncAdvance() continue. A background job already runs while
        // the panel is blank.
        if (!wake && state().sync.gSyncJob.active && state().sync.gSyncJob.modal) wake = true;
        if (wake) {
            state().ui.gLastInputAt = now;
            wakeScreen();
        } else {
            // The panel is blank. Run the idle check here too: with P5 Off the
            // loop would otherwise park in this branch forever and never reach
            // the P4/P6 deadline in powerTick(); with P5 On it also starts a
            // light sleep that was skipped while a background job was busy.
            if (powerTick(now)) return true;
            delay(10);
            return true;
        }
    }

    if (state().ui.gDimmed &&
        (tgc::board::wasPressed(tgc::board::Button::Select) ||
         tgc::board::wasHeld(tgc::board::Button::Select))) {
        state().ui.gLastInputAt = now;
        wakeScreen();
    }
    return false;
}

// Idle dim/blank/sleep. Returns true when the loop must return; deep sleep does
// not normally come back, but keep the contract for the caller.
bool powerTick(uint32_t now) {
    // REV-06: the review controller owns the correct-answer timer, but the
    // loop tick lives here so App.cpp stays untouched.
    reviewAutoAdvance(now);
    const uint32_t dimMs = static_cast<uint32_t>(kk::settingsDimSeconds()) * 1000u;
    const uint32_t blankMs = static_cast<uint32_t>(kk::settingsBlankSeconds()) * 1000u;
    const uint32_t sleepMs = static_cast<uint32_t>(kk::settingsSleepSeconds()) * 1000u;
    // A sync step may refresh gLastInputAt after the loop captured `now`; a
    // plain subtraction would underflow and look like ~49 days of idle. The
    // helper clamps a future timestamp to zero, and a zero limit disables a
    // timer exactly as before.
    const uint32_t lastInput = state().ui.gLastInputAt;
    const bool portalActive = kk::screenBase(state().screen) == Screen::Setup;
    if (!portalActive && !state().wifi.gWifiActive && !state().ui.gDimmed &&
        !state().ui.gScreenOff && kk::idleDue(now, lastInput, dimMs)) {
        tgc::board::setBrightness(20);
        state().ui.gDimmed = true;
    }
    if (!portalActive && !state().wifi.gWifiActive && !state().ui.gScreenOff &&
        kk::idleDue(now, lastInput, blankMs)) {
        Serial.printf("[power] blank after %u ms idle\n",
                      static_cast<unsigned>(kk::idleAgeMs(now, lastInput)));
        tgc::board::setDisplayOn(false);
        state().ui.gScreenOff = true;
    }
    const bool auxiliaryScreen = state().ui.gNoticeActive || state().wifi.gWifiActive ||
                                 state().stats.gStatsActive ||
                                 state().study.gStudyMenuActive ||
                                 state().study.gCriticalListActive;
    // A deep-sleep wake is a reset, so sleeping while the new image is pending
    // would roll it back; the 10-minute window bounds the wait.
    const bool firmwareBusy = state().ota.gFirmwareStep != FirmwareStep::Idle ||
                              state().ota.gOtaPending;
#ifdef KANIKEY_DEV
    // Dev builds only sleep when the serial test explicitly forces battery.
    // HWCDC::isPlugged() can read false after a light sleep even while the
    // cable is connected, which previously stranded the CDC; a dev image must
    // stay reachable unless `O` asks for the battery path.
    const bool batteryPower = devForceBattery();
#else
    const bool batteryPower = !tgc::board::externalPower();
#endif
    // A background sync and the asset server keep working while the panel is
    // blank (SPEED-03/05); light sleep would freeze them for the whole slice.
    const bool lightSleepBusy =
        state().sync.gSyncJob.active || state().storage.gAssetServerActive || firmwareBusy;
    // SPEED-06: after the blank delay, light sleep instead of staying fully
    // awake until the deep-sleep deadline. USB power keeps the serial port up
    // and never light-sleeps, like the deep-sleep path below.
    if (state().ui.gScreenOff && batteryPower && !portalActive && !state().wifi.gWifiActive &&
        !lightSleepBusy && lightSleepEnabled()) {
        const bool reviewOpen = reviewScreenOpen();
        const kk::SleepDuringReview policy = reviewSleepPolicy();
        if (lightSleepUntilDeadline(blankMs, sleepMs, reviewOpen, policy)) return false;
        // A timer wake brought the panel back; the sleep check below owns it
        // now. Recompute the idle age because the CPU was asleep.
        now = millis();
    }
    const Screen base = kk::screenBase(state().screen);
    const bool reviewOpen =
        state().screen.depth == 0 && (base == Screen::Review || base == Screen::ReviewSummary);
    const uint32_t idleMs = kk::idleAgeMs(now, lastInput);
    const kk::SleepDuringReview policy = reviewSleepPolicy();
    // P6 replaces P4 during a review: Never keeps today's no-deep-sleep rule,
    // After 10 min ends the session even when P4 is "Never". At home the P4
    // deadline is unchanged.
    const bool deepSleepDue = reviewOpen ? kk::reviewMayDeepSleep(policy, idleMs)
                                         : kk::idleDue(now, lastInput, sleepMs);
    const bool screenCanSleepNow =
        reviewOpen ? (!portalActive && !state().storage.gAssetServerActive && !firmwareBusy &&
                      !auxiliaryScreen)
                   : kk::screenCanSleep(state().screen, portalActive,
                                        state().storage.gAssetServerActive, firmwareBusy,
                                        auxiliaryScreen);
    if (deepSleepDue && screenCanSleepNow && batteryPower) {
        if (reviewOpen) {
            // Completed items are already in the outbox; a half-typed answer is
            // dropped by endReview(false) and nothing is sent.
            Serial.printf("[power] review idle sleep after %u ms\n",
                          static_cast<unsigned>(idleMs));
            endReview(false);
        } else {
            Serial.println("[power] idle sleep");
        }
        saveCacheSnapshot();
        firmwareBeforeSleep();
        // REM-02: a reminder timer when M1 or M3 is set. Both off (the defaults)
        // arms nothing and this is the same key-only deep sleep as before.
        tgc::board::deepSleep(kk::app::reminderArmIdle(
            state().session.gHaveSummary ? &state().session.gSummary : nullptr));
        return true;
    }
    return false;
}

}  // namespace kk::app

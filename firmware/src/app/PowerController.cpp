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
#include "wk/IdSample.h"
#include "wk/LessonOrder.h"
#include "wk/LessonQuiz.h"
#include "wk/Outbox.h"
#include "wk/ReviewQueue.h"
#include "wk/ReviewResponse.h"
#include "wk/RomajiIme.h"
#include "wk/SessionStats.h"
#include "wk/Stats.h"
#include "wk/SubjectText.h"
#include "wk/Time.h"
#include "ui/Theme.h"

namespace kk::app {
namespace {

using tgc::Key;
using tgc::KeyEvent;
using namespace kk::ui;

}  // namespace

bool devForceBattery() {
#ifdef KANIKEY_DEV
    return state().storage.gDevForceBattery;
#else
    return false;
#endif
}

void drawStatus() {
    if (!state().display.gDisp || state().setup.gPortalActive || state().wifi.gWifiActive || state().ui.gLowBatteryPrompt || state().ui.gSignOutConfirm || state().ui.gAboutActive ||
        state().ota.gFirmwareActive || state().ui.gSettingsActive || state().ui.gPauseActive || state().sync.gSyncScreen || state().session.gShowHome ||
        state().review.gReviewActive || state().review.gReviewSummary || state().lesson.gLessonListActive || state().lesson.gLessonEmptyActive ||
        state().lesson.gLessonActive || state().lesson.gLessonBatchQuizActive || state().lesson.gLessonStartActive ||
        state().ui.gOutboxScreen || state().stats.gStatsActive || state().study.gStudyMenuActive ||
        state().study.gCriticalListActive)
        return;
#ifdef KANIKEY_DEV
    if (state().dev.gShowDebug) showDebug();
#endif
    // The boot screen is redrawn on state changes through state().display.gDirty; only the
    // debug screen needs a periodic heap/battery refresh.
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
        if (wake) {
            state().ui.gLastInputAt = now;
            wakeScreen();
        } else {
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
    const uint32_t dimMs = static_cast<uint32_t>(kk::settingsDimSeconds()) * 1000u;
    const uint32_t blankMs = static_cast<uint32_t>(kk::settingsBlankSeconds()) * 1000u;
    const uint32_t sleepMs = static_cast<uint32_t>(kk::settingsSleepSeconds()) * 1000u;
    if (!state().setup.gPortalActive && !state().wifi.gWifiActive && dimMs && !state().ui.gDimmed &&
        now - state().ui.gLastInputAt > dimMs) {
        tgc::board::setBrightness(20);
        state().ui.gDimmed = true;
    }
    if (!state().setup.gPortalActive && !state().wifi.gWifiActive && blankMs && !state().ui.gScreenOff &&
        now - state().ui.gLastInputAt > blankMs) {
        tgc::board::setDisplayOn(false);
        state().ui.gScreenOff = true;
    }
    if (!state().setup.gPortalActive && !state().wifi.gWifiActive && !state().ui.gNoticeActive &&
        !state().review.gReviewActive && !state().review.gReviewSummary &&
        !state().review.gFeedback && !state().lesson.gLessonActive &&
        !state().lesson.gLessonListActive && !state().lesson.gLessonEmptyActive &&
        !state().lesson.gLessonQuizActive && !state().lesson.gLessonBatchQuizActive &&
        !state().lesson.gLessonStartActive && !state().ui.gPauseActive &&
        !state().ui.gSettingsActive && !state().ui.gAboutActive &&
        !state().ui.gOutboxScreen && !state().stats.gStatsActive &&
        !state().study.gStudyMenuActive && !state().study.gCriticalListActive &&
        !state().ota.gFirmwareActive && state().ota.gFirmwareStep == FirmwareStep::Idle &&
        // A deep-sleep wake is a reset, so sleeping while the new image is
        // pending would roll it back; the 10-minute window bounds the wait.
        !state().ota.gOtaPending && !state().ui.gLowBatteryPrompt &&
        !state().storage.gAssetServerActive && sleepMs &&
        now - state().ui.gLastInputAt > sleepMs &&
        (devForceBattery() || !tgc::board::externalPower())) {
        Serial.println("[power] idle sleep");
        saveCacheSnapshot();
        firmwareBeforeSleep();
        tgc::board::deepSleep();
        return true;
    }
    return false;
}

}  // namespace kk::app

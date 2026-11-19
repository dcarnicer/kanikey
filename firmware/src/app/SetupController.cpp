// First-boot setup portal and the boot retry path.
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

bool portalValidateToken(const String& token, String* username, int* level, String* error);
bool portalRecheckPack();
String portalPackStatus();
void portalSwitchToStation();
void portalFinish();

bool portalValidateToken(const String& token, String* username, int* level, String* error) {
    if (!kk::DevWifi::connected()) {
        if (error) *error = "Wi-Fi is not connected yet.";
        return false;
    }
    const bool tokenChanged = kk::DevToken::load() != token;
    kk::wkClearAuthBlock();
    state().ui.gFreeCanvasForRequest = true;
    const kk::WkProbeResult r = kk::probeWaniKaniUser(token);
    state().ui.gFreeCanvasForRequest = false;
    if (r.parsed) {
        if (tokenChanged) {
            // A new token must not inherit the previous account's level as the
            // baseline for a level-up notice.
            clearLevelUpNotice();
            state().session.gHaveUser = false;
        }
        state().sync.gDeferredAuthIssue = SyncIssue::None;
        state().sync.gSyncIssue = SyncIssue::None;
        state().sync.gOutboxAuthIssue = false;
        if (username) *username = r.user.username.c_str();
        if (level) *level = r.user.level;
        return true;
    }
    if (r.httpCode == 401 || r.httpCode == 403)
        setSyncIssue(r.httpCode, r.connected, r.tlsError, "Setup");
    if (error) {
        if (r.httpCode == 401 || r.httpCode == 403)
            *error = "WaniKani rejected the token (HTTP " + String(r.httpCode) + ").";
        else if (r.httpCode)
            *error = "WaniKani answered HTTP " + String(r.httpCode) + ".";
        else if (!r.connected)
            *error = "Network/TLS error " + String(r.tlsError) + ".";
        else
            *error = "WaniKani returned an invalid response.";
    }
    return false;
}

bool portalRecheckPack() {
    // A ready pack is only reported: re-opening it here was the FIX-25 repro
    // (the new descriptor died when the HTTP client closed).
    if (state().storage.gPack.ready()) return true;
    if (!state().storage.gSdOk) state().storage.gSdOk = mountSd();
    if (!state().storage.gSdOk) return false;
    // Opening inside the handler is unsafe and the CRC takes ~3.6 s. loop()
    // does it with the CRC after the response, so the next page load shows the
    // new status.
    state().storage.gPackRecheckPending = true;
    return false;
}

String portalPackStatus() { return String(state().storage.gPack.statusLine()); }

void portalSwitchToStation() {
    const String ssid = kk::DevWifi::savedSsid();
    state().setup.gSetupPortal.end(false);
    state().setup.gPortalActive = false;
    drawLoading("Connecting Wi-Fi...", ssid.c_str());
    if (!kk::DevWifi::connect(30000)) {
        const String notice = "Could not join \"" + ssid + "\". Check the password and try again.";
        state().setup.gPortalActive = state().setup.gSetupPortal.beginAp(state().setup.gSetupHooks, notice);
        state().display.gDirty = true;
        return;
    }
    const bool ok = state().setup.gSetupPortal.beginStation(
        state().setup.gSetupHooks, "Wi-Fi OK. Add the token, then Finish.");
    state().setup.gPortalActive = ok;
    if (!ok) snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Setup portal failed");
    state().display.gDirty = true;
}

void portalFinish() {
    state().setup.gSetupPortal.end(true);
    state().setup.gPortalActive = false;
    state().session.gShowHome = false;
    drawLoading("Setup complete", "Syncing...");
    startSync();
    state().display.gDirty = true;
}

}  // namespace

bool startSetupPortal() {
    if (state().setup.gPortalActive) return true;
    if (state().review.gReviewActive || state().lesson.gLessonActive || state().lesson.gLessonListActive ||
        state().lesson.gLessonEmptyActive || state().lesson.gLessonQuizActive ||
        state().lesson.gLessonBatchQuizActive || state().lesson.gLessonStartActive)
        return false;
    kk::wkClearAuthBlock();
    tgc::board::setDisplayOn(true);
    tgc::board::setBrightness(kk::settingsBrightness());
    state().ui.gDimmed = false;
    state().ui.gScreenOff = false;
    state().setup.gSetupHooks.validateToken = portalValidateToken;
    state().setup.gSetupHooks.recheckPack = portalRecheckPack;
    state().setup.gSetupHooks.packStatus = portalPackStatus;
    state().setup.gSetupHooks.onSwitchToStation = portalSwitchToStation;
    state().setup.gSetupHooks.onFinish = portalFinish;
    state().session.gShowHome = false;
    state().review.gReviewSummary = false;
    state().review.gFeedback = false;
    state().ui.gPauseActive = false;
    state().ui.gSettingsActive = false;
    state().ui.gAboutActive = false;
    state().ui.gSignOutConfirm = false;
    state().lesson.gLessonEmptyActive = false;
    state().lesson.gLessonListActive = false;
    state().ui.gLowBatteryPrompt = false;
    state().ui.gLowBatteryConfirmed = false;

    // With a saved network, go straight to the station page so the user can
    // change the token without re-entering Wi-Fi. If it cannot join, fall back
    // to the setup access point.
    if (kk::DevWifi::hasSaved()) {
        if (!kk::DevWifi::connected()) {
            drawLoading("Connecting Wi-Fi...", kk::DevWifi::savedSsid().c_str());
            kk::DevWifi::connect(30000);
        }
        if (kk::DevWifi::connected() &&
            state().setup.gSetupPortal.beginStation(state().setup.gSetupHooks,
                                      "Enter the token, then press Finish.")) {
            state().setup.gPortalActive = true;
            state().ui.gLastInputAt = millis();
            state().display.gDirty = true;
            return true;
        }
    }

    drawLoading("Starting setup...", "Scanning Wi-Fi...");
    kk::DevWifi::disconnect();
    state().setup.gPortalActive = state().setup.gSetupPortal.beginAp(
        state().setup.gSetupHooks, "Connect your phone to this setup network to configure KaniKey.");
    state().ui.gLastInputAt = millis();
    state().display.gDirty = true;
    if (!state().setup.gPortalActive) {
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Setup: AP failed");
        Serial.println("[setup] could not start the access point");
    }
    return state().setup.gPortalActive;
}

void retryBoot() {
    if (state().session.gHaveSummary) {
        state().session.gShowHome = true;
        state().display.gDirty = true;
        return;
    }
    if (!state().storage.gSdOk) {
        state().storage.gSdOk = mountSd();
        if (state().storage.gSdOk) {
            state().storage.gPack.open(SD, "/kanikey/glyphs.kgp");
            Serial.printf("[pack] %s\n", state().storage.gPack.statusLine());
        }
    }
    if (state().storage.gSdOk && !state().storage.gPack.ready()) {
        state().storage.gPack.open(SD, "/kanikey/glyphs.kgp");
        Serial.printf("[pack] %s\n", state().storage.gPack.statusLine());
    }
    if (state().storage.gSdOk && state().storage.gPack.ready() && !state().session.gHaveSummary && kk::DevToken::has() && loadCache()) {
        state().display.gDirty = true;
        return;
    }
    if (state().sync.gSyncIssue == SyncIssue::Auth || state().sync.gSyncIssue == SyncIssue::Forbidden) {
        startSetupPortal();
        return;
    }
    if (!kk::DevToken::has() || !kk::DevWifi::hasSaved()) {
        startSetupPortal();
        return;
    }
    if (!state().storage.gSdOk) {
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "SD: not mounted");
    } else if (!state().storage.gPack.ready()) {
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Pack: %s", state().storage.gPack.statusLine());
    } else if (!kk::DevToken::has()) {
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Token: not configured");
    } else if (!kk::DevWifi::hasSaved()) {
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Wi-Fi: not configured");
    } else {
        startSync();
        return;
    }
    state().display.gDirty = true;
}

void drawSetupPortal() {
    if (!state().display.gDisp) return;

    SetupView view;
    view.batteryPercent = tgc::board::batteryPercent();
    view.apMode = state().setup.gSetupPortal.mode() == kk::SetupPortal::Mode::Ap;
    view.ssid = state().setup.gSetupPortal.ssid();
    view.apPassword = state().setup.gSetupPortal.apPassword();
    view.url = state().setup.gSetupPortal.url();
    view.sessionCode = state().setup.gSetupPortal.sessionCode();
    view.tokenValidated = state().setup.gSetupPortal.tokenValidated();
    view.tokenUser = state().setup.gSetupPortal.tokenUser();
    view.tokenLevel = state().setup.gSetupPortal.tokenLevel();
    view.tokenConfigured = kk::DevToken::has();
    view.message = state().setup.gSetupPortal.message();
    view.packStatus = state().setup.gSetupPortal.packStatus().c_str();

    auto& d = target();
    d.startWrite();
    kk::app::drawSetupScreen(d, view);
    d.endWrite();
    present();
}

// Shared boot tail: load the cache when storage is available, then start the
// setup portal or a sync. Used by setup() and after the storage prompt.
void continueBootAfterStorage() {
    const bool cacheLoaded = state().storage.gSdOk && state().storage.gPack.ready() && loadCache();
    Serial.printf("[wifi] saved credentials: %s\n", kk::DevWifi::hasSaved() ? "yes" : "no");
    if (!state().storage.gSdOk) {
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "SD: not mounted");
    } else if (!state().storage.gPack.ready()) {
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Pack: %s", state().storage.gPack.statusLine());
    } else if (!kk::DevToken::has() || !kk::DevWifi::hasSaved()) {
        if (cacheLoaded)
            snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "NET: offline (cached)");
        else
            startSetupPortal();
    } else {
        startSync();
    }
    state().display.gDirty = true;
    drawStatus();
    state().display.gLastStatusMs = millis();
}

}  // namespace kk::app

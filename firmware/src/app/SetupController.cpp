// First-boot setup portal and the boot retry path.
#include <Arduino.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>

#include "app/AppUi.h"
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
#include "app/WifiSetupController.h"
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
#include "wk/SyncPlan.h"
#include "wk/Time.h"
#include "ui/Theme.h"

namespace kk::app {
namespace {

using tgc::Key;
using tgc::KeyEvent;
using kk::Screen;
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
    // Always defer a real check, even when both packs are already ready: the
    // page's Check pack again is the explicit full-CRC path. Opening inside
    // the HTTP handler was the FIX-25 repro; loop() does it after the response.
    if (!state().storage.gSdOk) state().storage.gSdOk = mountSd();
    if (!state().storage.gSdOk) return false;
    // Opening inside the handler is unsafe and the CRC takes ~3.6 s. loop()
    // does it after the response, so the next page load shows the new status.
    // A working font pack keeps the immediate "ready" answer; the radical
    // pack is optional and may still arrive on the next page load.
    state().storage.gPackRecheckPending = true;
    state().storage.gPackRecheckSkipCrc = false;
    return state().storage.gFontPack.ready();
}

String portalFontPackStatus() { return String(state().storage.gFontPack.statusLine()); }
String portalRadicalPackStatus() { return String(state().storage.gRadicalPack.statusLine()); }
bool portalFontPackReady() { return state().storage.gFontPack.ready(); }

// ONB-02: pack transfer helpers. Both paths draw the same loading frame and
// watch Esc on every chunk; no pack body is kept in RAM.
constexpr uint64_t kPackSpaceMargin = 64u * 1024u;
constexpr const char* kFontTmpPath = "/kanikey/glyphs.kgp.tmp";
constexpr const char* kRadicalTmpPath = "/kanikey/radicals.kgp.tmp";

int gPackProgress = -1;
File gUploadFile;
size_t gUploadBytes = 0;
int gUploadProgress = -1;

// A transfer hook draws its own loading frame, then the portal page becomes
// the message. Mark the display dirty on every return path so loop() repaints
// the setup screen once the HTTP request and the deferred pack reopen finish.
struct PackRepaintOnExit {
    ~PackRepaintOnExit() { state().display.gDirty = true; }
};

void formatPackSize(uint64_t bytes, char* out, size_t n) {
    if (bytes >= (1024ull * 1024ull * 1024ull)) {
        const uint64_t tenths = (bytes * 10ull) / (1024ull * 1024ull * 1024ull);
        snprintf(out, n, "%u.%u GB", static_cast<unsigned>(tenths / 10),
                 static_cast<unsigned>(tenths % 10));
    } else if (bytes >= (1024ull * 1024ull)) {
        const uint64_t tenths = (bytes * 10ull) / (1024ull * 1024ull);
        snprintf(out, n, "%u.%u MB", static_cast<unsigned>(tenths / 10),
                 static_cast<unsigned>(tenths % 10));
    } else {
        snprintf(out, n, "%u KB", static_cast<unsigned>(bytes / 1024ull));
    }
}

uint64_t sdFreeBytes() {
    const uint64_t total = SD.totalBytes();
    const uint64_t used = SD.usedBytes();
    return total > used ? total - used : 0;
}

bool packCancelRequested() {
    tgc::board::update();
    bool cancel = false;
    KeyEvent event;
    while (tgc::board::nextKeyEvent(&event)) {
        if (event.down && event.key == Key::Esc) cancel = true;
    }
    return cancel;
}

void drawPackProgress(const char* title, int percent) {
    char line[24];
    snprintf(line, sizeof line, "Pack %d%%", percent);
    drawLoading(title, line);
}

bool portalPackProgress(size_t done, size_t total, void*) {
    state().ui.gLastInputAt = millis();
    if (state().ui.gScreenOff) wakeScreen();
    if (packCancelRequested()) {
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Pack download cancelled");
        return false;
    }
    if (!total) return true;
    int percent = static_cast<int>((done * 100ull) / total);
    if (percent > 100) percent = 100;
    if (percent == gPackProgress) return true;
    gPackProgress = percent;
    drawPackProgress("Downloading pack", percent);
    return true;
}

kk::PortalPackResult portalDownloadPack(String* message) {
    PackRepaintOnExit repaint;
    if (message) message->clear();
    if (state().storage.gFontPack.ready()) {
        if (message) *message = "The font pack is already installed.";
        return kk::PortalPackResult::Failed;
    }
    if (!kk::DevWifi::connected()) {
        if (message) *message = "Wi-Fi is not connected.";
        return kk::PortalPackResult::Failed;
    }
    if (!state().storage.gSdOk) {
        if (message) *message = "No SD card.";
        return kk::PortalPackResult::Failed;
    }

    releaseCanvas();
    drawLoading("Checking pack", "release manifest");
    if (packCancelRequested()) {
        ensureCanvas();
        if (message) *message = "Pack download cancelled. Card unchanged.";
        return kk::PortalPackResult::Cancelled;
    }

    const String manifestUrl = kk::ota::manifestUrl();
    const std::string_view manifestView(manifestUrl.c_str(), manifestUrl.length());
    std::string text;
    const kk::ota::FetchResult fetched =
        kk::ota::fetchText(manifestView, kk::ota::kManifestMaxBytes, &text);
    char detail[120];
    if (fetched.status != kk::ota::FetchStatus::Ok) {
        snprintf(detail, sizeof detail, "Manifest: %s (HTTP %d).",
                 kk::ota::fetchStatusString(fetched.status), fetched.httpCode);
        ensureCanvas();
        drawLoading("Pack failed", detail);
        if (message) *message = detail;
        return kk::PortalPackResult::Failed;
    }
    const kk::ota::ManifestParseResult parsed =
        kk::ota::parseManifest(text, manifestView);
    if (parsed.error != kk::ota::ManifestError::Ok) {
        snprintf(detail, sizeof detail, "Manifest: %s.",
                 kk::ota::manifestErrorString(parsed.error));
        ensureCanvas();
        drawLoading("Pack failed", detail);
        if (message) *message = detail;
        return kk::PortalPackResult::Failed;
    }
    const kk::ota::PackImage* pack = parsed.manifest.findPack("font");
    if (!pack) {
        ensureCanvas();
        drawLoading("Pack failed", "no font pack in the manifest");
        if (message) *message = "The release manifest has no font pack.";
        return kk::PortalPackResult::Failed;
    }
    const uint64_t freeBytes = sdFreeBytes();
    if (freeBytes < static_cast<uint64_t>(pack->bytes) + kPackSpaceMargin) {
        char need[24];
        char freeText[24];
        formatPackSize(pack->bytes, need, sizeof need);
        formatPackSize(freeBytes, freeText, sizeof freeText);
        snprintf(detail, sizeof detail, "Not enough card space: need %s, free %s.", need,
                 freeText);
        ensureCanvas();
        drawLoading("Pack failed", detail);
        if (message) *message = detail;
        return kk::PortalPackResult::Failed;
    }

    gPackProgress = -1;
    if (packCancelRequested()) {
        ensureCanvas();
        drawLoading("Pack cancelled", "card unchanged");
        if (message) *message = "Pack download cancelled. Card unchanged.";
        return kk::PortalPackResult::Cancelled;
    }
    drawPackProgress("Downloading pack", 0);
    const kk::ota::FetchResult result = kk::ota::downloadToFile(
        String(pack->url.c_str(), static_cast<unsigned>(pack->url.size())), pack->bytes,
        pack->sha256, kFontTmpPath, portalPackProgress, nullptr);
    if (result.status != kk::ota::FetchStatus::Ok) {
        if (result.status == kk::ota::FetchStatus::Cancelled) {
            ensureCanvas();
            drawLoading("Pack cancelled", "card unchanged");
            if (message) *message = "Pack download cancelled. Card unchanged.";
            return kk::PortalPackResult::Cancelled;
        }
        snprintf(detail, sizeof detail, "Pack download failed: %s (HTTP %d).",
                 kk::ota::fetchStatusString(result.status), result.httpCode);
        ensureCanvas();
        drawLoading("Pack failed", detail);
        if (message) *message = detail;
        return kk::PortalPackResult::Failed;
    }

    // The manifest says "font"; prove that the downloaded file opens that way
    // before the app keeps it. The SHA-256 already covered transport damage.
    // This handle is only for the check: the app re-opens the pack from loop()
    // after the HTTP request has returned (FIX-25).
    kk::GlyphPack check;
    const bool opened = check.open(SD, "/kanikey/glyphs.kgp", kk::PackRole::Font, false);
    const bool rightKind = opened && check.glyphCount() > 0 &&
                           (check.kind() == kk::PackKind::Font ||
                            check.kind() == kk::PackKind::Combined);
    const uint16_t glyphs = check.glyphCount();
    char line[48];
    snprintf(line, sizeof line, "%s", check.statusLine());
    check.close();
    if (!rightKind) {
        SD.remove("/kanikey/glyphs.kgp");
        snprintf(detail, sizeof detail, "Downloaded pack is not a font pack: %s", line);
        ensureCanvas();
        drawLoading("Pack rejected", line);
        if (message) *message = detail;
        return kk::PortalPackResult::Failed;
    }
    state().storage.gPackRecheckPending = true;
    state().storage.gPackRecheckSkipCrc = true;
    ensureCanvas();
    drawLoading("Pack ready", line);
    if (message) {
        snprintf(detail, sizeof detail, "Font pack installed: %u glyphs.",
                 static_cast<unsigned>(glyphs));
        *message = detail;
    }
    return kk::PortalPackResult::Ok;
}

kk::PortalPackResult portalUploadRadicalPack(const uint8_t* data, size_t length,
                                             kk::PackUploadPhase phase, size_t total,
                                             String* message) {
    PackRepaintOnExit repaint;
    if (phase == kk::PackUploadPhase::Start) {
        gUploadBytes = 0;
        gUploadProgress = -1;
        if (gUploadFile) gUploadFile.close();
        if (!state().storage.gSdOk) {
            if (message) *message = "No SD card.";
            return kk::PortalPackResult::Failed;
        }
        const uint64_t freeBytes = sdFreeBytes();
        if (freeBytes < static_cast<uint64_t>(total) + kPackSpaceMargin) {
            char need[24];
            char freeText[24];
            formatPackSize(total, need, sizeof need);
            formatPackSize(freeBytes, freeText, sizeof freeText);
            if (message) *message = String("Not enough card space: need ") + need + ", free " +
                                    freeText + ".";
            return kk::PortalPackResult::Failed;
        }
        if (SD.exists(kRadicalTmpPath)) SD.remove(kRadicalTmpPath);
        if (!SD.exists("/kanikey")) SD.mkdir("/kanikey");
        gUploadFile = SD.open(kRadicalTmpPath, FILE_WRITE);
        if (!gUploadFile) {
            if (message) *message = "Could not write to the SD card.";
            return kk::PortalPackResult::Failed;
        }
        state().ui.gLastInputAt = millis();
        drawPackProgress("Uploading radical pack", 0);
        Serial.printf("[pack] upload start (%u bytes declared)\n",
                      static_cast<unsigned>(total));
        return kk::PortalPackResult::Ok;
    }

    if (phase == kk::PackUploadPhase::Chunk) {
        state().ui.gLastInputAt = millis();
        if (state().ui.gScreenOff) wakeScreen();
        if (!gUploadFile) {
            if (message) *message = "The upload file is not open.";
            return kk::PortalPackResult::Failed;
        }
        if (packCancelRequested()) {
            gUploadFile.close();
            SD.remove(kRadicalTmpPath);
            if (message) *message = "Upload cancelled. Card unchanged.";
            return kk::PortalPackResult::Cancelled;
        }
        if (total == 0 || gUploadBytes + length > total) {
            gUploadFile.close();
            SD.remove(kRadicalTmpPath);
            if (message) *message = "The upload is larger than the browser declared.";
            return kk::PortalPackResult::Failed;
        }
        if (length && gUploadFile.write(data, length) != length) {
            gUploadFile.close();
            SD.remove(kRadicalTmpPath);
            if (message) *message = "Could not write the upload to the card.";
            return kk::PortalPackResult::Failed;
        }
        gUploadBytes += length;
        int percent = total ? static_cast<int>((gUploadBytes * 100ull) / total) : 0;
        if (percent > 99) percent = 99;
        if (percent != gUploadProgress) {
            gUploadProgress = percent;
            drawPackProgress("Uploading radical pack", percent);
        }
        return kk::PortalPackResult::Ok;
    }

    if (phase == kk::PackUploadPhase::Finish) {
        if (!gUploadFile) {
            if (message) *message = "The upload file is not open.";
            return kk::PortalPackResult::Failed;
        }
        gUploadFile.flush();
        gUploadFile.close();
        drawLoading("Checking radical pack", "structure and kind");
        // The gate is the header/table and the v2 kind. The CRC is not part of
        // this check (GlyphPack::open only runs it when asked).
        kk::GlyphPack check;
        const bool opened = check.open(SD, kRadicalTmpPath, kk::PackRole::Radical, false);
        const bool rightKind = opened && check.kind() == kk::PackKind::Radical &&
                               check.radicalCount() > 0;
        const uint16_t radicals = check.radicalCount();
        char line[48];
        snprintf(line, sizeof line, "%s", check.statusLine());
        check.close();
        if (!rightKind) {
            SD.remove(kRadicalTmpPath);
            drawLoading("Radical pack rejected", line);
            Serial.printf("[pack] upload rejected: %s\n", line);
            if (message) *message = String("Not a radical pack: ") + line + ".";
            return kk::PortalPackResult::Failed;
        }
        // The old handle must not survive the swap. The app re-opens both packs
        // from loop() after the HTTP request (FIX-25).
        state().storage.gRadicalPack.close();
        if (!kk::ota::replaceFile(kRadicalTmpPath, "/kanikey/radicals.kgp")) {
            SD.remove(kRadicalTmpPath);
            state().storage.gPackRecheckPending = true;
            state().storage.gPackRecheckSkipCrc = true;
            if (message) *message = "Could not replace the radical pack.";
            return kk::PortalPackResult::Failed;
        }
        state().storage.gPackRecheckPending = true;
        state().storage.gPackRecheckSkipCrc = true;
        char count[32];
        snprintf(count, sizeof count, "%u radicals", static_cast<unsigned>(radicals));
        drawLoading("Radical pack ready", count);
        Serial.printf("[pack] upload: %s, heap %u\n", count,
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
        if (message) *message = String("Radical pack installed: ") + count +
                                ". Reopening on the device.";
        return kk::PortalPackResult::Ok;
    }

    if (phase == kk::PackUploadPhase::Abort) {
        if (gUploadFile) gUploadFile.close();
        if (SD.exists(kRadicalTmpPath)) SD.remove(kRadicalTmpPath);
        if (message && message->length() == 0)
            *message = "Upload cancelled. Card unchanged.";
        return kk::PortalPackResult::Cancelled;
    }
    return kk::PortalPackResult::Failed;
}

void portalSwitchToStation() {
    const String ssid = kk::DevWifi::savedSsid();
    state().setup.gSetupPortal.end(false);
    drawLoading("Connecting Wi-Fi...", ssid.c_str());
    if (!kk::DevWifi::connect(30000)) {
        const String notice = "Could not join \"" + ssid + "\". Check the password and try again.";
        const bool ok = state().setup.gSetupPortal.beginAp(state().setup.gSetupHooks, notice);
        if (!ok) kk::screenReset(&state().screen);
        state().display.gDirty = true;
        return;
    }
    const bool ok = state().setup.gSetupPortal.beginStation(
        state().setup.gSetupHooks, "Wi-Fi OK. Add the token, then Finish.");
    if (!ok) {
        kk::screenReset(&state().screen);
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Setup portal failed");
    }
    state().display.gDirty = true;
}

void portalFinish() {
    state().setup.gSetupPortal.end(true);
    kk::screenReset(&state().screen);
    drawLoading("Setup complete", "Syncing...");
    startSync();
    state().display.gDirty = true;
}

}  // namespace

bool startSetupPortal() {
    if (kk::screenBase(state().screen) == Screen::Setup) return true;
    const Screen base = kk::screenBase(state().screen);
    if (base == Screen::Review || base == Screen::LessonCard ||
        base == Screen::LessonList || base == Screen::LessonEmpty ||
        base == Screen::LessonQuiz)
        return false;
    kk::wkClearAuthBlock();
    tgc::board::setDisplayOn(true);
    tgc::board::setBrightness(kk::settingsBrightness());
    state().ui.gDimmed = false;
    state().ui.gScreenOff = false;
    state().setup.gSetupHooks.validateToken = portalValidateToken;
    state().setup.gSetupHooks.recheckPack = portalRecheckPack;
    state().setup.gSetupHooks.fontPackStatus = portalFontPackStatus;
    state().setup.gSetupHooks.radicalPackStatus = portalRadicalPackStatus;
    state().setup.gSetupHooks.fontPackReady = portalFontPackReady;
    state().setup.gSetupHooks.downloadPack = portalDownloadPack;
    state().setup.gSetupHooks.uploadRadicalPack = portalUploadRadicalPack;
    state().setup.gSetupHooks.onSwitchToStation = portalSwitchToStation;
    state().setup.gSetupHooks.onFinish = portalFinish;
    kk::screenReset(&state().screen);
    state().ui.gLowBatteryAction = 0;
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
            state().screen.screen = Screen::Setup;
            state().ui.gLastInputAt = millis();
            state().display.gDirty = true;
            return true;
        }
    }

    drawLoading("Starting setup...", "Scanning Wi-Fi...");
    kk::DevWifi::disconnect();
    if (state().setup.gSetupPortal.beginAp(
            state().setup.gSetupHooks, "Connect your phone to this setup network to configure KaniKey.")) {
        state().screen.screen = Screen::Setup;
    }
    state().ui.gLastInputAt = millis();
    state().display.gDirty = true;
    if (kk::screenBase(state().screen) != Screen::Setup) {
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Setup: AP failed");
        Serial.println("[setup] could not start the access point");
    }
    return kk::screenBase(state().screen) == Screen::Setup;
}

void retryBoot() {
    if (state().session.gHaveSummary) {
        state().screen.screen = Screen::Home;
        state().display.gDirty = true;
        return;
    }
    if (!state().storage.gSdOk) {
        state().storage.gSdOk = mountSd();
        if (state().storage.gSdOk) openGlyphPacks();
    }
    if (state().storage.gSdOk && !state().storage.gFontPack.ready()) openGlyphPacks();
    if (state().storage.gSdOk && state().storage.gFontPack.ready() && !state().session.gHaveSummary && kk::DevToken::has() && loadCache()) {
        state().display.gDirty = true;
        return;
    }
    // ONB-02: with a mounted card but no font pack, the setup portal is the way
    // out (Download pack). Without a saved network the same call starts the
    // setup access point, so the user can save Wi-Fi first.
    if (state().storage.gSdOk && !state().storage.gFontPack.ready()) {
        startSetupPortal();
        state().display.gDirty = true;
        return;
    }
    if (state().sync.gSyncIssue == SyncIssue::Auth || state().sync.gSyncIssue == SyncIssue::Forbidden) {
        startSetupPortal();
        return;
    }
    if (!kk::DevToken::has()) {
        // The token is still entered in the phone portal (SETUP-09 keeps that
        // path); a missing Wi-Fi with a token goes to the on-device scan list.
        startSetupPortal();
        return;
    }
    if (!kk::DevWifi::hasSaved()) {
        // PD-03: the on-device scan list needs a password; the portal does not.
        if (tgc::board::buttonsOnly())
            startSetupPortal();
        else
            openWifiSetup(WifiSetupOrigin::Boot);
        return;
    }
    if (!state().storage.gSdOk) {
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "SD: not mounted");
    } else if (!state().storage.gFontPack.ready()) {
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Pack: %s",
                 state().storage.gFontPack.statusLine());
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
    view.fontPackStatus = state().setup.gSetupPortal.fontPackStatus().c_str();
    view.radicalPackStatus = state().setup.gSetupPortal.radicalPackStatus().c_str();

    auto& d = target();
    d.startWrite();
    kk::app::drawSetupScreen(d, view);
    d.endWrite();
    present();
}

// Shared boot tail: load the cache when storage is available, then start the
// setup portal or a sync. Used by setup() and after the storage prompt.
void continueBootAfterStorage() {
    const bool cacheLoaded = state().storage.gSdOk && state().storage.gFontPack.ready() && loadCache();
    Serial.printf("[wifi] saved credentials: %s\n", kk::DevWifi::hasSaved() ? "yes" : "no");
    if (!state().storage.gSdOk) {
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "SD: not mounted");
    } else if (!state().storage.gFontPack.ready()) {
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Pack: %s",
                 state().storage.gFontPack.statusLine());
    } else if (!kk::DevToken::has() || !kk::DevWifi::hasSaved()) {
        if (cacheLoaded)
            snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "NET: offline (cached)");
        else
            startSetupPortal();
    } else {
        // SPEED-02: a wake from deep sleep runs Quick, a cold boot runs Full
        // when the last full sync is due. Off skips the automatic sync; a
        // manual Sync now still runs a full sync.
        const bool coldBoot = !tgc::board::wokeFromSleep();
        const int mode = kk::settingsAutoSyncMode();
        kk::SyncKind requested = kk::SyncKind::Quick;
        if (mode == kk::kAutoSyncOff) requested = kk::SyncKind::Skip;
        else if (mode == kk::kAutoSyncFull) requested = kk::SyncKind::Full;
        // The plan compares real epoch times; effectiveNow() would fall back
        // to the cache time and hide an untrusted clock.
        const kk::SyncTimes times{static_cast<uint32_t>(time(nullptr)),
                                  state().cache.gLastQuickAt, state().cache.gLastFullAt,
                                  state().cache.gLastUserAt};
        const kk::SyncKind kind = kk::planSync(
            requested, coldBoot, times,
            static_cast<uint32_t>(kk::settingsSyncFullEverySeconds()));
        if (kind == kk::SyncKind::Skip) {
            Serial.println("[sync] cache is recent; automatic sync skipped");
        } else {
            Serial.printf("[sync] automatic %s (cold boot %d)\n",
                          kind == kk::SyncKind::Full ? "full" : "quick", coldBoot ? 1 : 0);
            startSyncAuto(kind, coldBoot);
        }
    }
    state().display.gDirty = true;
    drawStatus();
    state().display.gLastStatusMs = millis();
}

}  // namespace kk::app

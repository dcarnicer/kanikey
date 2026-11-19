// Settings, pause, low battery, About and the firmware update UI.
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

constexpr int kSettingsRows = 19;

void setSettingsNote(const char* text, uint16_t color);
int nextPreset(const int* values, int count, int current);
String secondsLabel(int seconds);
void settingsAction();
void formatBytes(uint32_t bytes, char* out, size_t n);
void firmwareStatus(const char* text, uint16_t color);
void firmwareNoteImpl(const char* text, uint16_t color);
const char* offerLabel(kk::ota::OfferVerdict verdict);
// A pending image is valid after the first 2xx API response or 10 minutes of
// loop, whichever comes first. The time fallback keeps an offline device from
// staying in the verification window forever.
bool otaVerificationDue(uint32_t now);
void otaMarkValidIfDue(uint32_t now, const char* when);
void buildFirmwareView(kk::app::FirmwareView* view);
bool gFirmwareStateIdle();
bool firmwareProgress(size_t done, size_t total, void*);

void setSettingsNote(const char* text, uint16_t color = kFaint) {
    snprintf(state().ui.gSettingsNote, sizeof state().ui.gSettingsNote, "%s", text ? text : "");
    state().ui.gSettingsNoteColor = color;
}

int nextPreset(const int* values, int count, int current) {
    for (int i = 0; i < count; ++i)
        if (values[i] == current) return values[(i + 1) % count];
    for (int i = 0; i < count; ++i)
        if (values[i] > current) return values[i];
    return values[0];
}

String secondsLabel(int seconds) {
    if (seconds <= 0) return String("off");
    if (seconds < 60) return String(seconds) + "s";
    return String(seconds / 60) + "m";
}

void settingsAction() {
    switch (state().ui.gSettingsIndex) {
        case 0: {
            static const int kLevels[] = {64, 128, 192, 255};
            const int level = nextPreset(kLevels, 4, kk::settingsBrightness());
            kk::settingsSetBrightness(level);
            tgc::board::setBrightness(level);
            char note[32];
            snprintf(note, sizeof note, "Brightness %d", level);
            setSettingsNote(note, kOk);
            break;
        }
        case 1: {
            static const int kDim[] = {0, 15, 30, 60};
            const int value = nextPreset(kDim, 4, kk::settingsDimSeconds());
            kk::settingsSetDimSeconds(value);
            setSettingsNote((String("Dim after ") + secondsLabel(value)).c_str(), kOk);
            break;
        }
        case 2: {
            static const int kBlank[] = {0, 30, 60, 120};
            const int value = nextPreset(kBlank, 4, kk::settingsBlankSeconds());
            kk::settingsSetBlankSeconds(value);
            setSettingsNote((String("Blank after ") + secondsLabel(value)).c_str(), kOk);
            break;
        }
        case 3: {
            static const int kSleep[] = {0, 120, 300, 600};
            const int value = nextPreset(kSleep, 4, kk::settingsSleepSeconds());
            kk::settingsSetSleepSeconds(value);
            setSettingsNote((String("Sleep after ") + secondsLabel(value)).c_str(), kOk);
            break;
        }
        case 4: {
            static const int kSizes[] = {10, 20, 30};
            const int value = nextPreset(kSizes, 3, kk::settingsReviewBatchSize());
            kk::settingsSetReviewBatchSize(value);
            setSettingsNote((String("Review batch ") + String(value)).c_str(), kOk);
            break;
        }
        case 5: {
            const bool meaning = !kk::settingsMeaningFirst();
            kk::settingsSetMeaningFirst(meaning);
            setSettingsNote(meaning ? "Meaning first" : "Reading first", kOk);
            break;
        }
        case 6: {
            static const int kLessons[] = {3, 5, 10};
            const int value = nextPreset(kLessons, 3, kk::settingsLessonBatchSize());
            kk::settingsSetLessonBatchSize(value);
            setSettingsNote((String("Lesson batch ") + String(value)).c_str(), kOk);
            break;
        }
        case 7: {
            const bool value = !kk::settingsCriticalFirst();
            kk::settingsSetCriticalFirst(value);
            setSettingsNote(value ? "Critical items first" : "Normal review order", kOk);
            break;
        }
        case 8: {
            static const int kPercents[] = {60, 70, 75, 80, 90};
            const int value = nextPreset(kPercents, 5, kk::settingsCriticalPercent());
            kk::settingsSetCriticalPercent(value);
            setSettingsNote((String("Critical below ") + String(value) + "%").c_str(), kOk);
            break;
        }
        case 9: {
            static const int kAnswers[] = {3, 5, 10};
            const int value = nextPreset(kAnswers, 3, kk::settingsCriticalMinAnswers());
            kk::settingsSetCriticalMinAnswers(value);
            setSettingsNote((String("Critical min ") + String(value)).c_str(), kOk);
            break;
        }
        case 10: {
            static const int kWindows[] = {86400, 259200, 604800};
            const int value =
                nextPreset(kWindows, 3, kk::settingsPracticeMistakesWindow());
            kk::settingsSetPracticeMistakesWindow(value);
            setSettingsNote(value == 86400 ? "Mistakes: 24 h"
                                           : value == 259200 ? "Mistakes: 3 days"
                                                             : "Mistakes: 7 days",
                            kOk);
            break;
        }
        case 11: {
            const int value = (kk::settingsPracticeLessonsMode() + 1) % 4;
            kk::settingsSetPracticeLessonsMode(value);
            static const char* kLabels[] = {"Apprentice", "24 hours", "3 days", "7 days"};
            setSettingsNote((String("Lessons: ") + kLabels[value]).c_str(), kOk);
            break;
        }
        case 12: {
            const int value = kk::settingsPracticeSize() == 10 ? 20 : 10;
            kk::settingsSetPracticeSize(value);
            setSettingsNote((String("Practice size ") + String(value)).c_str(), kOk);
            break;
        }
        case 13:
            if (state().review.gReviewActive) {
                setSettingsNote("End the review session first", kWarn);
                break;
            }
            state().ui.gSettingsActive = false;
            state().session.gShowHome = false;
            startSetupPortal();
            break;
        case 14:
            if (!state().storage.gSdOk || !state().storage.gPack.ready() || !kk::DevToken::has() || !kk::DevWifi::hasSaved()) {
                setSettingsNote("Configure Wi-Fi, token and pack", kWarn);
                break;
            }
            state().ui.gSettingsActive = false;
            state().session.gShowHome = false;
            startSync();
            break;
        case 15:
            openFirmware();
            break;
        case 16:
            state().ui.gAboutActive = true;
            state().ui.gSettingsNote[0] = 0;
            break;
        case 17:
            if (state().storage.gFsOk) kk::cacheClearSnapshot();
            if (state().storage.gSdOk) {
                kk::cacheClearSubjects(SD);
                kk::audioCacheClear(SD);
            }
            state().cache.gCacheSavedAt = 0;
            setSettingsNote("Cache cleared; next boot is cold", kOk);
            break;
        case 18:
            state().ui.gSignOutConfirm = true;
            state().ui.gSettingsNote[0] = 0;
            break;
    }
    state().display.gDirty = true;
}

void formatBytes(uint32_t bytes, char* out, size_t n) {
    if (bytes >= 1024u * 1024u)
        snprintf(out, n, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    else
        snprintf(out, n, "%u KB", static_cast<unsigned>((bytes + 1023u) / 1024u));
}

void firmwareStatus(const char* text, uint16_t color) {
    snprintf(state().ota.gFirmwareStatus, sizeof state().ota.gFirmwareStatus, "%s", text ? text : "");
    state().ota.gFirmwareStatusColor = color;
}

void firmwareNoteImpl(const char* text, uint16_t color) {
    snprintf(state().ota.gFirmwareNote, sizeof state().ota.gFirmwareNote, "%s", text ? text : "");
    state().ota.gFirmwareNoteColor = color;
}

const char* offerLabel(kk::ota::OfferVerdict verdict) {
    switch (verdict) {
        case kk::ota::OfferVerdict::UpToDate: return "Up to date";
        case kk::ota::OfferVerdict::NoImage: return "No image for this build";
        case kk::ota::OfferVerdict::NoSlot: return "No update slot";
        case kk::ota::OfferVerdict::TooLarge: return "Image too large";
        case kk::ota::OfferVerdict::RolledBack: return "Previous update failed";
        case kk::ota::OfferVerdict::DevBuild: return "Development build";
        case kk::ota::OfferVerdict::BadManifest: return "Bad release manifest";
        case kk::ota::OfferVerdict::Offer: return "Update available";
    }
    return "Update check failed";
}

// A pending image is valid after the first 2xx API response or 10 minutes of
// loop, whichever comes first. The time fallback keeps an offline device from
// staying in the verification window forever.
bool otaVerificationDue(uint32_t now) {
    if (!state().ota.gOtaLoopAt) state().ota.gOtaLoopAt = now ? now : 1;
    return kk::wkHadApiSuccess() || now - state().ota.gOtaLoopAt >= 600000;
}

void otaMarkValidIfDue(uint32_t now, const char* when) {
    if (!state().ota.gOtaPending || !otaVerificationDue(now)) return;
    const bool apiOk = kk::wkHadApiSuccess();
    if (!kk::ota::markValid()) return;
    state().ota.gOtaPending = false;
    Serial.printf("[ota] %s marked valid %s (%s)\n", kk::kFirmwareVersion, when,
                  apiOk ? "API success" : "10 min loop");
    // A good image clears the two-strike record of its own version only.
    const kk::ota::RollbackRecord record = kk::ota::loadRollbackRecord();
    if (record.version[0] && std::strcmp(record.version, kk::kFirmwareVersion) == 0)
        kk::ota::clearRollbackRecord();
}

void buildFirmwareView(kk::app::FirmwareView* view) {
    view->version = kk::kFirmwareVersion;
    view->env = kk::ota::buildEnv();
    view->slot = kk::ota::runningSlot();
    view->status = state().ota.gFirmwareStatus;
    view->statusColor = state().ota.gFirmwareStatusColor;
    view->note = state().ota.gFirmwareNote;
    view->noteColor = state().ota.gFirmwareNoteColor;
    view->progress = state().ota.gFirmwareProgress;
    view->busy = state().ota.gFirmwareStep != FirmwareStep::Idle;
    view->selectedIndex = state().ota.gFirmwareIndex;
    if (state().ota.gFirmwareStep != FirmwareStep::Idle) return;

    const bool canInstall = state().ota.gFirmwareOffer.verdict == kk::ota::OfferVerdict::Offer &&
                            state().ota.gFirmwareOffer.app != nullptr;
    if (canInstall) {
        kk::app::FirmwareRowView install;
        install.label = "Install update";
        install.value = state().ota.gFirmwareManifest.version;
        view->rows.push_back(std::move(install));
    }
    kk::app::FirmwareRowView check;
    check.label = canInstall ? "Check again" : "Check now";
    check.value = "GitHub";
    view->rows.push_back(std::move(check));
}

bool gFirmwareStateIdle() { return state().ota.gFirmwareStep == FirmwareStep::Idle; }

bool firmwareProgress(size_t done, size_t total, void*) {
    if (!total) return true;
    state().ui.gLastInputAt = millis();
    if (state().ui.gScreenOff) wakeScreen();
    const int pct = static_cast<int>(done * 100 / total);
    if (pct == state().ota.gFirmwareProgress) return true;
    state().ota.gFirmwareProgress = pct;
    snprintf(state().ota.gFirmwareStatus, sizeof state().ota.gFirmwareStatus, "Downloading update %d%%", pct);
    // No std::string/vector allocations while the TLS download owns the heap:
    // draw a small progress frame directly.
    if (!state().display.gDisp) return true;
    auto& d = *state().display.gDisp;
    d.startWrite();
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextFont(2);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("firmware update", 7, 22);
    char line[64];
    snprintf(line, sizeof line, "%s", state().ota.gFirmwareManifest.version.c_str());
    d.setTextFont(0);
    d.setTextColor(ui::kText, ui::kBg);
    d.drawString(line, 7, 44);
    d.drawRect(7, 66, 226, 10, ui::kLine);
    d.fillRect(9, 68, 222 * pct / 100, 6, ui::kAccentHi);
    snprintf(line, sizeof line, "%d%%", pct);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString(line, 7, 82);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("Do not power off", 7, 123);
    d.endWrite();
    return true;
}

}  // namespace

void firmwareNote(const char* text, uint16_t color) { firmwareNoteImpl(text, color); }

bool lowBatteryNow() {
    const int battery = tgc::board::batteryPercent();
    return battery >= 0 && battery < 20 && !tgc::board::externalPower();
}

void requestLowBatteryConfirm(int action) {
    state().ui.gLowBatteryPrompt = true;
    state().ui.gLowBatteryAction = action;
    state().ui.gLowBatteryConfirmed = false;
    state().display.gDirty = true;
}

void openSettings(SettingsOrigin origin) {
    state().ui.gSettingsActive = true;
    state().ui.gAboutActive = false;
    state().ota.gFirmwareActive = false;
    state().ui.gSignOutConfirm = false;
    state().ui.gPauseActive = false;
    state().ui.gSettingsOrigin = origin;
    state().ui.gSettingsIndex = 0;
    state().ui.gSettingsNote[0] = 0;
    state().session.gShowHome = false;
    state().display.gDirty = true;
}

void closeSettings() {
    state().ui.gSettingsActive = false;
    state().ui.gAboutActive = false;
    state().ota.gFirmwareActive = false;
    state().ui.gSignOutConfirm = false;
    state().ota.gFirmwareConfirm = false;
    state().ui.gSettingsNote[0] = 0;
    if (state().ui.gSettingsOrigin == SettingsOrigin::Pause)
        state().ui.gPauseActive = true;
    else
        state().session.gShowHome = true;
    state().display.gDirty = true;
}

void openPause() {
    if (!state().review.gReviewActive) return;
    state().ui.gPauseActive = true;
    state().ui.gPauseIndex = 0;
    state().ui.gSettingsActive = false;
    state().ui.gAboutActive = false;
    state().ota.gFirmwareActive = false;
    state().ui.gSignOutConfirm = false;
    state().display.gDirty = true;
}

void settingsKey(const KeyEvent& e) {
    if (!e.down) return;
    if (state().ui.gSignOutConfirm) {
        if (e.key == Key::Esc) {
            state().ui.gSignOutConfirm = false;
            state().display.gDirty = true;
        } else if (e.key == Key::Enter) {
            kk::DevToken::clear();
            kk::wkClearAuthBlock();
            state().sync.gDeferredAuthIssue = SyncIssue::None;
            if (state().storage.gFsOk) kk::cacheClearSnapshot();
            if (state().storage.gSdOk) {
                kk::cacheClearSubjects(SD);
                kk::catalogClear(SD);
                kk::audioCacheClear(SD);
            }
            state().outbox.gOutbox.clear();
            saveOutbox();
            kk::journalClear();
            kk::criticalClear();
            kk::practicePoolClear();
            state().study.gPracticePool.clear();
            state().study.gPracticePoolLoaded = false;
            state().study.gCritical.clear();
            state().study.gCriticalLoaded = false;
            state().stats.gStatsHistoryLoaded = false;
            state().review.gPendingJournalHandle = 0;
            state().session.gUser = kk::User{};
            state().session.gSummary = kk::Summary{};
            state().session.gLevelProgress = kk::LevelProgress{};
            clearLevelUpNotice();
            state().session.gSrs = kk::SrsCounts{};
            state().session.gHaveUser = false;
            state().session.gHaveSummary = false;
            state().cache.gBatch = kk::ReviewBatch{};
            state().cache.gBatchEvicted = false;
            state().cache.gCachedBatchCount = 0;
            state().review.gQueue = kk::ReviewQueue{};
            state().session.gDueReviews = 0;
            state().session.gLessons = 0;
            state().cache.gCacheSavedAt = 0;
            state().cache.gOffline = false;
            state().sync.gSyncing = false;
            state().sync.gSyncAttempted = false;
            state().sync.gSyncIssue = SyncIssue::None;
            state().sync.gOutboxAuthIssue = false;
            state().review.gReviewActive = false;
            state().review.gReviewSummary = false;
            state().lesson.gLessonQuizActive = false;
            state().review.gFeedback = false;
            state().review.gNeedsSubmit = false;
            state().ui.gPauseActive = false;
            state().ui.gSettingsActive = false;
            state().ui.gAboutActive = false;
            state().ota.gFirmwareActive = false;
            state().ui.gSignOutConfirm = false;
            state().ui.gLowBatteryPrompt = false;
            state().ui.gLowBatteryConfirmed = false;
            state().session.gShowHome = false;
            snprintf(state().session.gSyncClock, sizeof state().session.gSyncClock, "--:--");
            snprintf(state().session.gNextReview, sizeof state().session.gNextReview, "--:--");
            snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Signed out");
            Serial.println("[setup] signed out; token, cache and outbox cleared");
            state().display.gDirty = true;
        }
        return;
    }
    if (e.key == Key::Esc) {
        if (state().ui.gAboutActive) {
            state().ui.gAboutActive = false;
            state().ui.gSettingsNote[0] = 0;
        } else {
            closeSettings();
        }
        state().display.gDirty = true;
        return;
    }
    if (state().ui.gAboutActive) return;
    if (e.key == Key::Up) {
        state().ui.gSettingsIndex = (state().ui.gSettingsIndex + kSettingsRows - 1) % kSettingsRows;
        state().ui.gSettingsNote[0] = 0;
        state().display.gDirty = true;
    } else if (e.key == Key::Down) {
        state().ui.gSettingsIndex = (state().ui.gSettingsIndex + 1) % kSettingsRows;
        state().ui.gSettingsNote[0] = 0;
        state().display.gDirty = true;
    } else if (e.key == Key::Enter) {
        settingsAction();
    }
}

void pauseKey(const KeyEvent& e) {
    if (!e.down) return;
    if (e.key == Key::Esc) {
        state().ui.gPauseActive = false;
        state().display.gDirty = true;
        return;
    }
    if (e.key == Key::Up) {
        state().ui.gPauseIndex = (state().ui.gPauseIndex + 2) % 3;
        state().display.gDirty = true;
    } else if (e.key == Key::Down) {
        state().ui.gPauseIndex = (state().ui.gPauseIndex + 1) % 3;
        state().display.gDirty = true;
    } else if (e.key == Key::Enter) {
        if (state().ui.gPauseIndex == 0) {
            state().ui.gPauseActive = false;
        } else if (state().ui.gPauseIndex == 1) {
            state().ui.gPauseActive = false;
            endReview();
        } else {
            state().ui.gPauseActive = false;
            openSettings(SettingsOrigin::Pause);
        }
        state().display.gDirty = true;
    }
}

void drawPause() {
    if (!state().display.gDisp) return;

    const int total = static_cast<int>(state().review.gQueue.completedCount() + state().review.gQueue.remaining());
    PauseView view;
    view.selectedIndex = state().ui.gPauseIndex;
    view.completed = static_cast<unsigned>(state().review.gQueue.completedCount());
    view.total = static_cast<unsigned>(total);

    auto& d = target();
    d.startWrite();
    kk::app::drawPauseScreen(d, view);
    d.endWrite();
    present();
}

void drawSettings() {
    if (!state().display.gDisp) return;
    auto& d = target();

    static const char* kLabels[] = {"Brightness", "Dim after", "Blank after", "Sleep after",
                                    "Review batch", "Order", "Lesson batch", "Critical first",
                                    "Critical below", "Critical min", "Practice mistakes",
                                    "Practice lessons", "Practice size", "Setup portal", "Sync now",
                                    "Firmware", "About", "Clear cache", "Sign out"};
    String values[kSettingsRows];
    values[0] = String(kk::settingsBrightness());
    values[1] = secondsLabel(kk::settingsDimSeconds());
    values[2] = secondsLabel(kk::settingsBlankSeconds());
    values[3] = secondsLabel(kk::settingsSleepSeconds());
    values[4] = String(kk::settingsReviewBatchSize());
    values[5] = kk::settingsMeaningFirst() ? "Meaning" : "Reading";
    values[6] = String(kk::settingsLessonBatchSize());
    values[7] = kk::settingsCriticalFirst() ? "On" : "Off";
    values[8] = String(kk::settingsCriticalPercent()) + "%";
    values[9] = String(kk::settingsCriticalMinAnswers());
    const int mistakes = kk::settingsPracticeMistakesWindow();
    values[10] = mistakes == 86400 ? "24h" : mistakes == 259200 ? "3d" : "7d";
    static const char* kLessonLabels[] = {"Apprentice", "24h", "3d", "7d"};
    values[11] = kLessonLabels[kk::settingsPracticeLessonsMode()];
    values[12] = String(kk::settingsPracticeSize());
    values[13] = "open";
    values[14] = "run";
    values[15] = state().ota.gOtaState.badgeVersion.empty() ? kk::kFirmwareVersion
                                                : state().ota.gOtaState.badgeVersion.c_str();
    values[16] = "info";
    values[17] = "clear";
    values[18] = "out";

    SettingsView view;
    view.selectedIndex = state().ui.gSettingsIndex;
    view.note = state().ui.gSettingsNote;
    view.noteColor = state().ui.gSettingsNoteColor;
    view.rows.reserve(kSettingsRows);
    for (int i = 0; i < kSettingsRows; ++i) {
        SettingsRowView row;
        row.label = kLabels[i];
        row.value = values[i].c_str();
        view.rows.push_back(std::move(row));
    }

    d.startWrite();
    drawSettingsScreen(d, view);
    d.endWrite();
    present();
}

void drawLowBattery() {
    if (!state().display.gDisp) return;

    LowBatteryView view;
    view.batteryPercent = tgc::board::batteryPercent();
    view.lessonAction = state().ui.gLowBatteryAction == 2;

    auto& d = target();
    d.startWrite();
    kk::app::drawLowBatteryScreen(d, view);
    d.endWrite();
    present();
}

void drawSignOutConfirm() {
    if (!state().display.gDisp) return;
    auto& d = target();
    d.startWrite();
    kk::app::drawSignOutScreen(d);
    d.endWrite();
    present();
}

void firmwareBoot() {
    state().ota.gOtaState = kk::ota::loadSavedState();
    const bool layout = kk::ota::layoutIsOurs();
    if (!layout) {
        Serial.printf("[ota] no OTA layout (running %s): USB updates only\n",
                      kk::ota::runningSlot());
    } else {
        Serial.printf("[ota] %s (%s) in %s; update slot %s, %u bytes\n", kk::kFirmwareVersion,
                      kk::ota::buildEnv(), kk::ota::runningSlot(), kk::ota::nextSlot(),
                      static_cast<unsigned>(kk::ota::slotBytes()));
    }
    if (!state().ota.gOtaState.installing.empty()) {
        kk::ota::FirmwareVersion installed, running;
        bool same = state().ota.gOtaState.installing == kk::kFirmwareVersion;
        if (!same && kk::ota::parseFirmwareVersion(state().ota.gOtaState.installing, &installed) &&
            kk::ota::parseFirmwareVersion(kk::kFirmwareVersion, &running))
            same = kk::ota::compareFirmwareVersions(installed, running) == 0;
        if (same) {
            Serial.printf("[ota] %s installed over the air in %s\n",
                          state().ota.gOtaState.installing.c_str(), kk::ota::runningSlot());
        } else {
            Serial.printf("[ota] %s did not start in %s: back on %s\n",
                          state().ota.gOtaState.installing.c_str(), state().ota.gOtaState.installSlot.c_str(),
                          kk::kFirmwareVersion);
            // Two-strike rule: one rollback may be a power cut, so the version
            // stays on offer; the second one blacklists it.
            const kk::ota::RollbackRecord record = kk::ota::loadRollbackRecord();
            const kk::ota::RollbackDecision d =
                kk::ota::rollbackDecision(record, state().ota.gOtaState.installing);
            kk::ota::saveRollbackRecord(d.record);
            if (d.blacklist) {
                state().ota.gOtaState.failedVersion = state().ota.gOtaState.installing;
                kk::ota::saveFailedVersion(state().ota.gOtaState.failedVersion);
                firmwareNoteImpl("Update failed twice; not offered", kWarn);
            } else {
                firmwareNoteImpl("Last update rolled back", kWarn);
            }
        }
        state().ota.gOtaState.installing.clear();
        state().ota.gOtaState.installSlot.clear();
        kk::ota::clearInstallRecord();
    }
    state().ota.gOtaPending = layout && kk::ota::pendingVerify();
    if (state().ota.gOtaPending) {
        state().ota.gOtaLoopAt = millis();
        Serial.printf("[ota] first boot of %s: valid after an API success or 10 minutes\n",
                      kk::kFirmwareVersion);
    }
    // A badge for the running version (or for a failed one) is stale.
    if (!state().ota.gOtaState.badgeVersion.empty()) {
        kk::ota::FirmwareVersion running, badge;
        const bool parsed = kk::ota::parseFirmwareVersion(kk::kFirmwareVersion, &running) &&
                            kk::ota::parseFirmwareVersion(state().ota.gOtaState.badgeVersion, &badge);
        if ((parsed && kk::ota::compareFirmwareVersions(badge, running) <= 0) ||
            state().ota.gOtaState.badgeVersion == state().ota.gOtaState.failedVersion) {
            state().ota.gOtaState.badgeVersion.clear();
            state().ota.gOtaState.badgeBytes = 0;
            kk::ota::saveBadge("", 0);
        }
    }
    if (!state().ota.gOtaState.badgeVersion.empty())
        Serial.printf("[ota] cached offer %s (%u bytes)\n", state().ota.gOtaState.badgeVersion.c_str(),
                      static_cast<unsigned>(state().ota.gOtaState.badgeBytes));
    if (!state().ota.gOtaState.failedVersion.empty())
        Serial.printf("[ota] not offering %s again (rolled back)\n",
                      state().ota.gOtaState.failedVersion.c_str());
}

void firmwareTick(uint32_t now) { otaMarkValidIfDue(now, "after boot"); }

void firmwareBeforeSleep() { otaMarkValidIfDue(millis(), "before sleep"); }

void drawFirmware() {
    if (!state().display.gDisp) return;
    kk::app::FirmwareView view;
    buildFirmwareView(&view);
    auto& d = target();
    d.startWrite();
    kk::app::drawFirmwareScreen(d, view);
    d.endWrite();
    present();
}

void openFirmware() {
    state().ota.gFirmwareActive = true;
    state().ui.gSettingsActive = false;
    state().ui.gAboutActive = false;
    state().ui.gSignOutConfirm = false;
    state().session.gShowHome = false;
    state().ui.gSettingsNote[0] = 0;
    state().ota.gFirmwareIndex = 0;
    state().ota.gFirmwareConfirm = false;
    state().ota.gFirmwareProgress = -1;
    state().ota.gFirmwareStep = FirmwareStep::Idle;
    if (!state().ota.gFirmwareChecked) {
        if (!state().ota.gOtaState.badgeVersion.empty()) {
            char status[96];
            snprintf(status, sizeof status, "%s available; check to install",
                     state().ota.gOtaState.badgeVersion.c_str());
            firmwareStatus(status, kAccentHi);
            firmwareNoteImpl("Check again before installing", kFaint);
            // The cached offer has no AppImage; a Check now is still required
            // before installing after a reboot.
            state().ota.gFirmwareOffer = kk::ota::Offer{};
        } else if (state().ota.gFirmwareStatus[0] == 0) {
            firmwareStatus("Not checked", kDim);
            if (state().ota.gFirmwareNote[0] == 0)
                firmwareNoteImpl("Check GitHub for a new release", kFaint);
        }
    }
    state().display.gDirty = true;
}

void firmwareCheck(bool manual) {
    if (state().ota.gFirmwareStep != FirmwareStep::Idle) return;
    if (!kk::ota::layoutIsOurs()) {
        firmwareStatus("No OTA slot on this build", kWarn);
        firmwareNoteImpl("Updates over USB only", kFaint);
        if (state().ota.gFirmwareActive) drawFirmware();
        return;
    }
    if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
        firmwareStatus("No Wi-Fi connection", kWarn);
        firmwareNoteImpl("Join the saved network first", kFaint);
        if (state().ota.gFirmwareActive) drawFirmware();
        return;
    }

    state().ota.gFirmwareStep = FirmwareStep::Checking;
    state().ota.gFirmwareProgress = -1;
    firmwareStatus("Checking GitHub...", kDim);
    firmwareNoteImpl(manual ? "This can take a few seconds" : "", kFaint);
    if (state().ota.gFirmwareActive) drawFirmware();

    const String url = kk::ota::manifestUrl();
    std::string text;
    releaseCanvas();
    const kk::ota::FetchResult r = kk::ota::fetchText(
        std::string_view(url.c_str(), url.length()), kk::ota::kManifestMaxBytes, &text);
    ensureCanvas();

    if (r.status != kk::ota::FetchStatus::Ok) {
        char status[96];
        snprintf(status, sizeof status, "Check failed: %s",
                 kk::ota::fetchStatusString(r.status));
        firmwareStatus(status, kWarn);
        if (r.httpCode)
            snprintf(state().ota.gFirmwareNote, sizeof state().ota.gFirmwareNote, "HTTP %d  %s", r.httpCode, url.c_str());
        else
            snprintf(state().ota.gFirmwareNote, sizeof state().ota.gFirmwareNote, "%s", url.c_str());
        state().ota.gFirmwareNoteColor = kFaint;
    } else {
        const kk::ota::ManifestParseResult parsed =
            kk::ota::parseManifest(text, std::string_view(url.c_str(), url.length()));
        if (parsed.error != kk::ota::ManifestError::Ok) {
            char status[96];
            snprintf(status, sizeof status, "Bad manifest: %s",
                     kk::ota::manifestErrorString(parsed.error));
            firmwareStatus(status, kWarn);
            if (parsed.line)
                snprintf(state().ota.gFirmwareNote, sizeof state().ota.gFirmwareNote, "Line %u  %s", parsed.line,
                         url.c_str());
            else
                snprintf(state().ota.gFirmwareNote, sizeof state().ota.gFirmwareNote, "%s", url.c_str());
            state().ota.gFirmwareNoteColor = kFaint;
        } else {
            state().ota.gFirmwareManifest = parsed.manifest;
            kk::ota::OfferInput in;
            in.running = kk::kFirmwareVersion;
            in.env = kk::ota::buildEnv();
            in.slotBytes = kk::ota::slotBytes();
            in.rolledBack = state().ota.gOtaState.failedVersion;
#ifdef KANIKEY_DEV
            in.allowAnyDevVersion = true;
#endif
            state().ota.gFirmwareOffer = kk::ota::offerFor(state().ota.gFirmwareManifest, in);
            state().ota.gFirmwareChecked = true;
            if (state().ota.gFirmwareOffer.verdict == kk::ota::OfferVerdict::Offer) {
                char status[96];
                char size[16];
                formatBytes(state().ota.gFirmwareOffer.app->bytes, size, sizeof size);
                snprintf(status, sizeof status, "%s available (%s)",
                         state().ota.gFirmwareManifest.version.c_str(), size);
                firmwareStatus(status, kAccentHi);
                firmwareNoteImpl("Enter: install  Esc: back", kFaint);
                state().ota.gOtaState.badgeVersion = state().ota.gFirmwareManifest.version;
                state().ota.gOtaState.badgeBytes = state().ota.gFirmwareOffer.app->bytes;
                kk::ota::saveBadge(state().ota.gOtaState.badgeVersion, state().ota.gOtaState.badgeBytes);
            } else {
                char status[96];
                snprintf(status, sizeof status, "%s: %s",
                         offerLabel(state().ota.gFirmwareOffer.verdict), state().ota.gFirmwareManifest.version.c_str());
                firmwareStatus(status,
                               state().ota.gFirmwareOffer.verdict == kk::ota::OfferVerdict::UpToDate ? kOk
                                                                                         : kWarn);
                firmwareNoteImpl(parsed.manifest.version.c_str(), kFaint);
                if (!state().ota.gOtaState.badgeVersion.empty()) {
                    state().ota.gOtaState.badgeVersion.clear();
                    state().ota.gOtaState.badgeBytes = 0;
                    kk::ota::saveBadge("", 0);
                }
            }
        }
    }
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    if (now >= 1700000000u) {
        state().ota.gOtaState.lastCheckAt = now;
        kk::ota::saveLastCheck(now);
    }
    state().ota.gFirmwareStep = FirmwareStep::Idle;
    state().ota.gFirmwareIndex = 0;
    state().ota.gFirmwareConfirm = false;
    Serial.printf("[ota] check: %s (%s, http %d, %u bytes)\n",
                  kk::ota::fetchStatusString(r.status), state().ota.gFirmwareStatus, r.httpCode,
                  static_cast<unsigned>(r.bytes));
    state().display.gDirty = true;
    if (state().ota.gFirmwareActive) drawFirmware();
}

// After a sync the device is already online: a silent check once a day keeps
// the Settings row and About current without a visible delay.
void firmwareAfterSync() {
    if (!kk::ota::layoutIsOurs() || state().ota.gOtaPending || !gFirmwareStateIdle()) return;
    if (!kk::DevWifi::connected()) return;
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    if (now < 1700000000u) return;
    if (state().ota.gOtaState.lastCheckAt && now - state().ota.gOtaState.lastCheckAt < 24u * 60u * 60u) return;
    firmwareStatus("Checking for firmware...", kFaint);
    firmwareCheck(false);
}

void startFirmwareInstall() {
    if (state().ota.gFirmwareStep != FirmwareStep::Idle) return;
    if (state().review.gReviewActive || state().lesson.gLessonActive || state().lesson.gLessonListActive || state().lesson.gLessonQuizActive) {
        firmwareNoteImpl("Finish the review/lesson first", kWarn);
        state().display.gDirty = true;
        return;
    }
    if (state().ota.gFirmwareOffer.verdict != kk::ota::OfferVerdict::Offer || !state().ota.gFirmwareOffer.app) {
        firmwareNoteImpl("Check for an update first", kWarn);
        state().display.gDirty = true;
        return;
    }
    if (lowBatteryNow()) {
        firmwareNoteImpl("Battery below 20%; connect power", kWarn);
        state().display.gDirty = true;
        return;
    }
    const kk::ota::AppImage app = *state().ota.gFirmwareOffer.app;
    const std::string version = state().ota.gFirmwareManifest.version;

    // Cache and outbox first: the restart after a good install must not lose a
    // completed review or the last snapshot.
    saveCacheSnapshot();
    saveOutbox();
    // The update slot is the one a pending image would roll back to; this
    // image has carried the user here, so it is good now.
    if (state().ota.gOtaPending && kk::ota::markValid()) state().ota.gOtaPending = false;

    state().ota.gFirmwareStep = FirmwareStep::Installing;
    state().ota.gFirmwareConfirm = false;
    state().ota.gFirmwareProgress = 0;
    state().ui.gLastInputAt = millis();
    snprintf(state().ota.gFirmwareStatus, sizeof state().ota.gFirmwareStatus, "Downloading update 0%%");
    firmwareNoteImpl("Do not power off", kFaint);
    if (state().ota.gFirmwareActive) drawFirmware();
    releaseCanvas();

    const kk::ota::FetchResult r = kk::ota::install(app, firmwareProgress, nullptr);
    if (r.status == kk::ota::FetchStatus::Ok) {
        state().ota.gFirmwareProgress = 100;
        firmwareStatus("Verifying update...", kDim);
        firmwareNoteImpl("Do not power off", kFaint);
        if (state().ota.gFirmwareActive) drawFirmware();
        kk::ota::saveInstallRecord(version, kk::ota::nextSlot());
        if (kk::ota::activate()) {
            state().ota.gFirmwareStep = FirmwareStep::Restarting;
            state().ota.gFirmwareProgress = 100;
            snprintf(state().ota.gFirmwareStatus, sizeof state().ota.gFirmwareStatus, "Restarting into %s", version.c_str());
            firmwareNoteImpl("Installed; rebooting", kOk);
            if (state().ota.gFirmwareActive) drawFirmware();
            Serial.printf("[ota] restarting into %s\n", version.c_str());
            Serial.flush();
            delay(1500);
            ESP.restart();
        }
        kk::ota::clearInstallRecord();
        kk::ota::abandon();
        firmwareStatus("Update image rejected", kWarn);
        firmwareNoteImpl("Stays on the current firmware", kFaint);
    } else {
        kk::ota::clearInstallRecord();
        char status[96];
        snprintf(status, sizeof status, "Install failed: %s",
                 kk::ota::fetchStatusString(r.status));
        firmwareStatus(status, kWarn);
        snprintf(state().ota.gFirmwareNote, sizeof state().ota.gFirmwareNote, "Stays on %s", kk::kFirmwareVersion);
        state().ota.gFirmwareNoteColor = kFaint;
        Serial.printf("[ota] install failed: %s (http %d)\n",
                      kk::ota::fetchStatusString(r.status), r.httpCode);
    }
    ensureCanvas();
    state().ota.gFirmwareStep = FirmwareStep::Idle;
    state().ota.gFirmwareProgress = -1;
    state().ota.gFirmwareIndex = 0;
    state().ota.gFirmwareChecked = false;
    state().ota.gOtaState.badgeVersion.clear();
    state().ota.gOtaState.badgeBytes = 0;
    kk::ota::saveBadge("", 0);
    state().display.gDirty = true;
    if (state().ota.gFirmwareActive) drawFirmware();
}

void firmwareKey(const KeyEvent& e) {
    if (!e.down) return;
    if (state().ota.gFirmwareStep != FirmwareStep::Idle) return;
    if (e.key == Key::Esc) {
        if (state().ota.gFirmwareConfirm) {
            state().ota.gFirmwareConfirm = false;
            firmwareNoteImpl("Install cancelled", kFaint);
        } else {
            state().ota.gFirmwareActive = false;
            state().ui.gSettingsActive = true;
            state().ui.gSettingsIndex = 15;
        }
        state().display.gDirty = true;
        return;
    }
    const bool canInstall = state().ota.gFirmwareOffer.verdict == kk::ota::OfferVerdict::Offer &&
                            state().ota.gFirmwareOffer.app != nullptr;
    const int rows = canInstall ? 2 : 1;
    if (e.key == Key::Up) {
        state().ota.gFirmwareIndex = (state().ota.gFirmwareIndex + rows - 1) % rows;
        state().ota.gFirmwareConfirm = false;
        firmwareNoteImpl("", kFaint);
        state().display.gDirty = true;
    } else if (e.key == Key::Down) {
        state().ota.gFirmwareIndex = (state().ota.gFirmwareIndex + 1) % rows;
        state().ota.gFirmwareConfirm = false;
        firmwareNoteImpl("", kFaint);
        state().display.gDirty = true;
    } else if (e.key == Key::Enter) {
        if (canInstall && state().ota.gFirmwareIndex == 0) {
            if (!state().ota.gFirmwareConfirm) {
                state().ota.gFirmwareConfirm = true;
                firmwareNoteImpl("Enter again to install, Esc cancels", kWarn);
                state().display.gDirty = true;
            } else {
                startFirmwareInstall();
            }
        } else {
            firmwareCheck(true);
        }
    }
}

void drawAbout() {
    if (!state().display.gDisp) return;
    kk::app::AboutView view;
    view.version = kk::kFirmwareVersion;
    if (kk::ota::layoutIsOurs()) {
        view.version += " (";
        view.version += kk::ota::runningSlot();
        view.version += ")";
    }

    char build[48];
    if (!state().ota.gOtaState.badgeVersion.empty())
        snprintf(build, sizeof build, "Build %s  upd %s", __DATE__,
                 state().ota.gOtaState.badgeVersion.c_str());
    else
        snprintf(build, sizeof build, "Build %s %s", __DATE__, __TIME__);
    view.lines.push_back({build, state().ota.gOtaState.badgeVersion.empty() ? kFaint : kAccentHi, 52});
    view.lines.push_back({std::string("Pack: ") + state().storage.gPack.statusLine(),
                          state().storage.gPack.ready() ? kOk : kWarn, 63});
    char cache[64];
    snprintf(cache, sizeof cache, "Cache: %u batch, %u due",
             static_cast<unsigned>(state().cache.gCachedBatchCount),
             static_cast<unsigned>(state().session.gDueReviews));
    view.lines.push_back({cache, kDim, 74});
    view.lines.push_back({std::string("Last sync: ") + state().session.gSyncClock +
                              (state().cache.gOffline ? " (offline)" : ""),
                          state().cache.gOffline ? kWarn : kDim, 85});
    std::string wifi = kk::DevWifi::hasSaved() ? kk::DevWifi::savedSsid().c_str()
                                                : "not configured";
    if (kk::DevWifi::connected()) {
        wifi += " ";
        wifi += kk::DevWifi::ip().c_str();
    }
    view.lines.push_back({std::string("Wi-Fi: ") + wifi, kDim, 96});
    view.lines.push_back({std::string("Token: ") + (kk::DevToken::has() ? "configured" : "missing"),
                          kk::DevToken::has() ? kDim : kWarn, 107});
    std::string zone = kk::settingsTimeZoneName().c_str();
    if (zone.empty()) zone = kk::settingsTimeZoneRule().c_str();
    view.lines.push_back({std::string("Zone: ") + zone, kFaint, 118});
    char heap[48];
    snprintf(heap, sizeof heap, "Heap: %u free / %u largest",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                    MALLOC_CAP_8BIT)));
    view.lines.push_back({heap, kFaint, 126});

    auto& d = target();
    d.startWrite();
    kk::app::drawAboutScreen(d, view);
    d.endWrite();
    present();
}

}  // namespace kk::app

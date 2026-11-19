// KaniKey entry point: boot, the main loop and the draw/onKey routers.
//
// The application state lives in AppState; the cache, sync, outbox, review,
// lesson, stats, study, settings, setup, power and dev-console flows live in
// their controllers. The individual frames are drawn by the app/*Screen views.

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <LittleFS.h>
#include <esp_system.h>
#include <time.h>

#include <algorithm>
#include <cstring>

#include "app/AboutScreen.h"
#include "app/App.h"
#include "app/AppState.h"
#include "app/AppUi.h"
#include "app/CacheController.h"
#include "app/DevConsole.h"
#include "app/LessonController.h"
#include "app/OutboxController.h"
#include "app/PowerController.h"
#include "app/ReviewController.h"
#include "app/SettingsController.h"
#include "app/SetupController.h"
#include "app/StatsController.h"
#include "app/StudyController.h"
#include "app/SyncController.h"
#include "app/WifiSetupController.h"
#include "app/BootScreen.h"
#include "app/DebugScreens.h"
#include "app/FirmwareScreen.h"
#include "app/HomeScreen.h"
#include "app/LessonScreens.h"
#include "app/LoadingScreen.h"
#include "app/ModalScreens.h"
#include "app/OutboxScreen.h"
#include "app/ReviewScreens.h"
#include "app/SettingsScreen.h"
#include "app/SetupScreen.h"
#include "app/StatsScreen.h"
#include "app/StudyScreens.h"
#include "app/SyncScreen.h"
#include "board/Board.h"
#include "Version.h"
#include "platform/CacheStore.h"
#include "platform/DeviceSettings.h"
#include "platform/DevToken.h"
#include "platform/DevWifi.h"
#include "platform/AudioCache.h"
#include "platform/CriticalStore.h"
#include "platform/GlyphPack.h"
#include "platform/JournalStore.h"
#include "platform/PracticeStore.h"
#include "platform/SetupPortal.h"
#include "platform/SubjectCatalog.h"
#include "platform/WkProbe.h"
#include "platform/WkReviewStatistics.h"
#include "platform/OutboxStore.h"
#include "platform/Ota.h"
#include "platform/WkClient.h"
#include "platform/WkLessons.h"
#include "platform/WkLevelProgress.h"
#include "platform/WkReview.h"
#include "platform/WkSrsCounts.h"
#include "wk/AnswerChecker.h"
#include "wk/HelpTable.h"
#include "wk/IdSample.h"
#include "wk/LessonOrder.h"
#include "wk/LessonQuiz.h"
#include "wk/Outbox.h"
#include "wk/ReviewQueue.h"
#include "wk/ReviewResponse.h"
#include "wk/RomajiIme.h"
#include "wk/SessionStats.h"
#include "wk/Stats.h"
#include "wk/Time.h"
#include "ui/Theme.h"

namespace kk::app {
namespace {

using tgc::Key;
using tgc::KeyEvent;
using namespace kk::ui;

// CFG-04: which help table the current frame offers. The order follows draw()
// so the overlay never outlives the screen it describes; the blocking frames
// that own the keyboard (portal, Wi-Fi setup, low battery, sign-out) and the
// screens without a table return None.
kk::HelpScreen helpScreenForState() {
    if (state().ui.gNoticeActive) return kk::HelpScreen::None;
    if (state().setup.gPortalActive) return kk::HelpScreen::None;
    if (state().wifi.gWifiActive) return kk::HelpScreen::None;
    if (state().ui.gLowBatteryPrompt) return kk::HelpScreen::None;
    if (state().ui.gSignOutConfirm) return kk::HelpScreen::None;
    if (state().ui.gAboutActive) return kk::HelpScreen::None;
    if (state().ui.gOutboxScreen) return kk::HelpScreen::Outbox;
    if (state().stats.gStatsActive) return kk::HelpScreen::None;
    if (state().study.gStudyMenuActive || state().study.gCriticalListActive)
        return kk::HelpScreen::None;
    if (state().ota.gFirmwareActive) return kk::HelpScreen::Firmware;
    if (state().ui.gSettingsActive) return kk::HelpScreen::Settings;
    if (state().ui.gPauseActive) return kk::HelpScreen::None;
    if (state().sync.gSyncScreen) return kk::HelpScreen::None;
#ifdef KANIKEY_DEV
    if (state().dev.gShowRadicalTest) return kk::HelpScreen::None;
#endif
    if (state().review.gReviewDetail) return kk::HelpScreen::None;
    if (state().review.gReviewActive)
        return state().review.gFeedback ? kk::HelpScreen::ReviewsFeedback
                                        : kk::HelpScreen::ReviewsQuestion;
    if (state().review.gReviewSummary) return kk::HelpScreen::None;
    if (state().lesson.gLessonEmptyActive || state().lesson.gLessonStartActive ||
        state().lesson.gLessonListActive)
        return kk::HelpScreen::None;
    if (state().lesson.gLessonBatchQuizActive || state().lesson.gLessonQuizActive)
        return kk::HelpScreen::LessonQuiz;
    if (state().lesson.gLessonActive) return kk::HelpScreen::LessonCard;
#ifdef KANIKEY_DEV
    if (state().dev.gShowDebug) return kk::HelpScreen::None;
#endif
    if (state().session.gShowHome) return kk::HelpScreen::Home;
    return kk::HelpScreen::None;
}

void onKeyImpl(const KeyEvent& e) {
#ifdef KANIKEY_DEV
    if (state().lesson.gDevKeyTest) {
        Serial.printf("[key] test %s %s\n", e.key == Key::None ? "marker" : "event",
                      e.down ? "down" : "up");
        if (e.key == Key::None) state().lesson.gDevKeyTest = false;
    }
#endif
    updateModifier(e.x, e.y, e.down);
    if (state().setup.gPortalActive) {
        if (e.down && e.key == Key::Esc) {
            state().setup.gSetupPortal.end(false);
            state().setup.gPortalActive = false;
            state().session.gShowHome = false;
            snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Setup cancelled");
            state().display.gDirty = true;
        }
        return;
    }
    // CFG-04: the help overlay owns the keyboard while it is open. Esc closes
    // it; every other key (down or up) is swallowed, so nothing reaches the
    // screen underneath.
    if (state().ui.gHelpActive) {
        if (e.down && e.key == Key::Esc) {
            state().ui.gHelpActive = false;
            state().ui.gHelpScreen = kk::HelpScreen::None;
            state().display.gDirty = true;
        }
        return;
    }
    // Shift + / is the Cardputer grid position 12,3. It arrives as Key::Right
    // with gShift set; plain / must keep acting as Right, so match the chord,
    // not Key::Other.
    if (e.down && state().ui.gShift && e.x == 12 && e.y == 3) {
        const kk::HelpScreen screen = helpScreenForState();
        if (screen != kk::HelpScreen::None) {
            state().ui.gHelpActive = true;
            state().ui.gHelpScreen = screen;
            // Like any other key on correct feedback, opening help cancels the
            // auto-advance timer; closing returns to the same feedback.
            state().review.gFeedbackCorrectAt = 0;
            state().display.gDirty = true;
            return;
        }
    }
    if (state().wifi.gWifiActive) {
        wifiSetupKey(e);
        return;
    }
    if (state().ui.gNoticeActive) {
        noticeKey(e);
        return;
    }
    if (state().ui.gLowBatteryPrompt) {
        if (e.down && e.key == Key::Enter) {
            const int action = state().ui.gLowBatteryAction;
            state().ui.gLowBatteryPrompt = false;
            state().ui.gLowBatteryAction = 0;
            state().ui.gLowBatteryConfirmed = true;
            if (action == 1)
                startReview();
            else if (action == 2)
                startLessons();
            state().display.gDirty = true;
        } else if (e.down && e.key == Key::Esc) {
            state().ui.gLowBatteryPrompt = false;
            state().ui.gLowBatteryAction = 0;
            state().ui.gLowBatteryConfirmed = false;
            state().session.gShowHome = true;
            state().display.gDirty = true;
        }
        return;
    }
    if (state().sync.gSyncScreen) {
        syncScreenKey(e);
        return;
    }
    if (state().ui.gOutboxScreen) {
        outboxKey(e);
        return;
    }
    if (state().stats.gStatsActive) {
        statsKey(e);
        return;
    }
    if (state().review.gReviewDetail) {
        reviewDetailKey(e);
        return;
    }
    if (state().study.gStudyMenuActive) {
        studyMenuKey(e);
        return;
    }
    if (state().study.gCriticalListActive) {
        criticalListKey(e);
        return;
    }
    if (state().ota.gFirmwareActive) {
        firmwareKey(e);
        return;
    }
    if (state().ui.gSettingsActive) {
        settingsKey(e);
        return;
    }
    if (state().ui.gPauseActive) {
        pauseKey(e);
        return;
    }
#ifdef KANIKEY_DEV
    if (state().dev.gShowRadicalTest) {
        if (e.down && e.key == Key::Esc) {
            state().dev.gShowRadicalTest = false;
            state().display.gDirty = true;
        }
        return;
    }
#endif
    if (state().lesson.gLessonEmptyActive) {
        if (e.down && (e.key == Key::Enter || e.key == Key::Esc)) {
            state().lesson.gLessonEmptyActive = false;
            state().session.gShowHome = true;
            state().display.gDirty = true;
        }
        return;
    }
    if (state().lesson.gLessonStartActive) {
        lessonStartKey(e);
        return;
    }
    if (state().lesson.gLessonListActive) {
        lessonListKey(e);
        return;
    }
    if (state().lesson.gLessonBatchQuizActive) {
        lessonBatchQuizKey(e);
        return;
    }
    if (state().lesson.gLessonQuizActive) {
        lessonQuizKey(e);
        return;
    }
    if (state().lesson.gLessonActive) {
        lessonKey(e);
        return;
    }
    if (state().review.gReviewActive) {
        reviewKey(e);
        return;
    }
    if (state().review.gReviewSummary) {
        if (e.down && (e.key == Key::Enter || e.key == Key::Esc)) endReview();
        return;
    }
#ifdef KANIKEY_DEV
    if (e.down && e.key == Key::Tab) {
        state().dev.gShowDebug = !state().dev.gShowDebug;
        state().display.gDirty = true;
        return;
    }
#endif
    if (e.down && state().session.gShowHome && e.key == Key::Esc) {
        openSettings(SettingsOrigin::Home);
        return;
    }
    if (e.down && e.key == Key::Enter) {
        if (state().session.gShowHome && state().session.gHaveSummary) {
            if (state().session.gSummary.dueReviews(effectiveNow()) == 0) {
                // Nothing due: no network call, just the next time. Refresh it
                // first: answering the last due item leaves state().session.gNextReview at "now".
                applySummary(effectiveNow());
                snprintf(state().ui.gNoticeLine, sizeof state().ui.gNoticeLine, "Next review  %s", state().session.gNextReview);
                state().ui.gNoticeLine2[0] = 0;
                state().ui.gNoticeAction = NoticeAction::ReviewEmpty;
                state().ui.gNoticeActive = true;
                state().session.gShowHome = false;
                state().display.gDirty = true;
            } else {
                if (state().sync.gSyncJob.active && !state().sync.gSyncJob.modal) {
                    // Enter during an automatic sync: try the cached batch at
                    // the next request boundary instead of starting a second
                    // network fetch (SPEED-03).
                    state().sync.gSyncJob.interrupted = true;
                    state().sync.gSyncJob.startReviewPending = true;
                    state().display.gDirty = true;
                } else {
                    startReview();
                }
            }
        } else {
            retryBoot();
        }
        return;
    }
    // The boot "Wi-Fi needed" state keeps the phone portal as the fallback
    // (SETUP-09): Enter opens the on-device scan list, P opens the portal.
    if (e.down && !state().session.gShowHome && e.key == Key::Other &&
        !state().sync.gSyncing && state().storage.gSdOk &&
        state().storage.gFontPack.ready() && !kk::DevWifi::hasSaved()) {
        const char c = tgc::cardputerCharAt(e.x, e.y, state().ui.gShift);
        if (c == 'p' || c == 'P') {
            startSetupPortal();
            return;
        }
    }
    if (e.down && state().session.gShowHome && e.key == Key::Other &&
        (tgc::cardputerCharAt(e.x, e.y, state().ui.gShift) == 'o' ||
         tgc::cardputerCharAt(e.x, e.y, state().ui.gShift) == 'O')) {
        openOutbox();
        return;
    }
    if (e.down && state().session.gShowHome && e.key == Key::Other &&
        (tgc::cardputerCharAt(e.x, e.y, state().ui.gShift) == 'l' ||
         tgc::cardputerCharAt(e.x, e.y, state().ui.gShift) == 'L')) {
        startLessons();
        return;
    }
    if (e.down && state().session.gShowHome && e.key == Key::Other &&
        (tgc::cardputerCharAt(e.x, e.y, state().ui.gShift) == 't' ||
         tgc::cardputerCharAt(e.x, e.y, state().ui.gShift) == 'T')) {
        openStats();
        return;
    }
    if (e.down && state().session.gShowHome && e.key == Key::Other &&
        (tgc::cardputerCharAt(e.x, e.y, state().ui.gShift) == 'p' ||
         tgc::cardputerCharAt(e.x, e.y, state().ui.gShift) == 'P')) {
        openStudyMenu();
        return;
    }
    if (e.down && state().session.gShowHome && !state().sync.gSyncing && e.key == Key::Other &&
        (tgc::cardputerCharAt(e.x, e.y, state().ui.gShift) == 's' ||
         tgc::cardputerCharAt(e.x, e.y, state().ui.gShift) == 'S')) {
        startSync();
        return;
    }
    const char c = tgc::cardputerCharAt(e.x, e.y, state().ui.gShift);

    char line[44];
    if (c) {
        snprintf(line, sizeof line, "%s '%c'  code %u  %u,%u", e.down ? "v" : "^", c,
                 static_cast<unsigned>(e.code), static_cast<unsigned>(e.x),
                 static_cast<unsigned>(e.y));
    } else {
        snprintf(line, sizeof line, "%s %-9s code %u  %u,%u", e.down ? "v" : "^",
                 keyName(e.key), static_cast<unsigned>(e.code),
                 static_cast<unsigned>(e.x), static_cast<unsigned>(e.y));
    }
    pushLog(line);
    state().display.gDirty = true;

    char cs[2] = {c, 0};
    Serial.printf("[key] %s code=%u grid=%u,%u key=%s char=%s shift=%d fn=%d ctrl=%d opt=%d "
                  "alt=%d\n",
                  e.down ? "down" : "up", static_cast<unsigned>(e.code),
                  static_cast<unsigned>(e.x), static_cast<unsigned>(e.y), keyName(e.key),
                  c ? cs : "-", state().ui.gShift, state().ui.gFn, state().ui.gCtrl, state().ui.gOpt, state().ui.gAlt);
}

kk::app::HomeView buildHomeView() {
    kk::app::HomeView view;
    view.batteryPercent = tgc::board::batteryPercent();
    view.signedIn = state().session.gHaveUser;
    view.level = state().session.gHaveUser ? state().session.gUser.level : 0;
    view.username = state().session.gHaveUser ? state().session.gUser.username.c_str() : "";
    view.dueReviews = state().session.gDueReviews;
    view.lessons = state().session.gLessons;
    view.nextReview = state().session.gNextReview;
    view.syncClock = state().session.gSyncClock;
    const SyncJobState& job = state().sync.gSyncJob;
    if (job.active && !job.modal) {
        view.syncTotal = kk::stepCount(job.kind, job.needsUser);
        view.syncStep = kk::stepNumber(job.step, job.kind, job.needsUser);
    }
    view.offline = state().cache.gOffline;
    view.srs = state().session.gSrs;
    view.outboxAttention = static_cast<uint32_t>(kk::outboxAttentionCount(state().outbox.gOutbox));
    view.outboxPending = static_cast<uint32_t>(kk::outboxPendingCount(state().outbox.gOutbox));
    // "Nearly full" starts at 96 of 128 slots; every state occupies a slot.
    view.outboxNearlyFull = state().outbox.gOutbox.size() >= (kk::kOutboxMax * 3u) / 4u;

    // Status priority: API/auth, outbox, offline cache, then notices.
    if (state().sync.gSyncIssue == SyncIssue::Auth) {
        snprintf(view.statusText, sizeof view.statusText, "Auth: token rejected");
        view.statusColor = kErr;
    } else if (state().sync.gSyncIssue == SyncIssue::Forbidden) {
        snprintf(view.statusText, sizeof view.statusText, "Auth: missing permissions");
        view.statusColor = kErr;
    } else if (state().sync.gSyncIssue == SyncIssue::RateLimit) {
        view.statusColor = kWarn;
        const uint32_t wait = kk::wkRateLimitWaitSeconds();
        if (wait)
            snprintf(view.statusText, sizeof view.statusText, "Rate limited; retry in %us",
                     static_cast<unsigned>(wait));
        else
            snprintf(view.statusText, sizeof view.statusText, "Rate limited; retry later");
    } else if (state().sync.gSyncIssue == SyncIssue::Server || state().sync.gSyncIssue == SyncIssue::Other) {
        snprintf(view.statusText, sizeof view.statusText, "WaniKani error; retry");
        view.statusColor = kWarn;
    } else if (view.outboxAttention) {
        snprintf(view.statusText, sizeof view.statusText, "Outbox: %u need attention",
                 static_cast<unsigned>(view.outboxAttention));
        view.statusColor = kErr;
    } else if (view.outboxPending) {
        snprintf(view.statusText, sizeof view.statusText, "Outbox: %u pending",
                 static_cast<unsigned>(view.outboxPending));
        view.statusColor = kWarn;
    } else if (!state().storage.gFsOk) {
        snprintf(view.statusText, sizeof view.statusText, "Storage unavailable");
        view.statusColor = kErr;
    } else if (state().cache.gOffline) {
        cacheStatusLine(view.statusText, sizeof view.statusText);
        view.statusColor = cacheIsStale() ? kErr : kWarn;
    } else if (state().session.gLevelUpNotice) {
        snprintf(view.statusText, sizeof view.statusText, "Level up! L%u -> L%u",
                 static_cast<unsigned>(state().session.gLevelUpFrom), static_cast<unsigned>(state().session.gLevelUpTo));
        view.statusColor = kOk;
        view.levelUp = true;
    } else if (state().session.gHaveUser && state().session.gUser.vacationStartedAt) {
        snprintf(view.statusText, sizeof view.statusText, "Vacation mode");
        view.statusColor = kWarn;
    } else if (state().session.gLevelProgress.valid) {
        formatLevelProgressLine(view.levelLine, sizeof view.levelLine);
    }
    return view;
}

void drawHome() {
    if (!state().display.gDisp) return;
    static bool logged = false;
    if (!logged) {
        logged = true;
        Serial.println("[kk] home drawn");
    }
    const kk::app::HomeView view = buildHomeView();
    auto& d = target();
    d.startWrite();
    kk::app::drawHomeScreen(d, view);
    d.endWrite();
    present();
}

enum class BootState : uint8_t {
    NoSd,
    NoPack,
    NoToken,
    NoWifi,
    NotSynced,
    Syncing,
    AuthError,
    Forbidden,
    RateLimited,
    ServerError,
    Failed,
    Ready
};

BootState bootState() {
    if (!state().storage.gSdOk) return BootState::NoSd;
    if (!state().storage.gFontPack.ready()) return BootState::NoPack;
    if (!kk::DevToken::has()) return BootState::NoToken;
    if (!kk::DevWifi::hasSaved()) return BootState::NoWifi;
    if (state().sync.gSyncing) return BootState::Syncing;
    if (state().sync.gSyncIssue == SyncIssue::Auth) return BootState::AuthError;
    if (state().sync.gSyncIssue == SyncIssue::Forbidden) return BootState::Forbidden;
    if (state().sync.gSyncIssue == SyncIssue::RateLimit) return BootState::RateLimited;
    if (state().sync.gSyncIssue == SyncIssue::Server || state().sync.gSyncIssue == SyncIssue::Other)
        return BootState::ServerError;
    if (!state().session.gHaveSummary) return state().sync.gSyncAttempted ? BootState::Failed : BootState::NotSynced;
    return BootState::Ready;
}

void drawBoot() {
    if (!state().display.gDisp) return;

    const BootState st = bootState();
    const char* title = "";
    const char* line1 = "";
    const char* line2 = "";
    const char* action = "";
    char retryLine[32] = "";
    switch (st) {
        case BootState::NoSd:
            title = "No SD card";
            line1 = "Insert a FAT32 card with";
            line2 = "/kanikey/glyphs.kgp";
            action = "Enter: retry";
            break;
        case BootState::NoPack:
            title = "Pack missing";
            line1 = "Press Enter to download it";
            line2 = "from the setup page";
            action = "Enter: setup";
            break;
        case BootState::NoToken:
            title = "Token needed";
            line1 = "Press Enter for setup";
            line2 = "Wi-Fi and token from your phone";
            action = "Enter: setup";
            break;
        case BootState::NoWifi:
            title = "Wi-Fi needed";
            line1 = "Set up a 2.4 GHz network here";
            line2 = "or press P for the phone portal";
            action = "Enter: on-device  P: phone";
            break;
        case BootState::NotSynced:
            title = "Not synced";
            line1 = state().ui.gNetLine;
            line2 = "Press Enter to sync";
            action = "Enter: sync";
            break;
        case BootState::Syncing:
            title = "Syncing...";
            line1 = state().ui.gNetLine;
            line2 = "First sync can take a few seconds";
            action = "";
            break;
        case BootState::AuthError:
            title = "Token rejected";
            line1 = "The WaniKani token is invalid";
            line2 = "Press Enter to open setup";
            action = "Enter: setup";
            break;
        case BootState::Forbidden:
            title = "Missing permissions";
            line1 = "Edit the token in WaniKani";
            line2 = "Press Enter to open setup";
            action = "Enter: setup";
            break;
        case BootState::RateLimited: {
            const uint32_t wait = kk::wkRateLimitWaitSeconds();
            if (wait)
                snprintf(retryLine, sizeof retryLine, "Retry in %us",
                         static_cast<unsigned>(wait));
            else
                snprintf(retryLine, sizeof retryLine, "Press Enter to retry");
            title = "Rate limited";
            line1 = "WaniKani asked KaniKey to wait";
            line2 = retryLine;
            action = "Enter: retry";
            break;
        }
        case BootState::ServerError:
            title = "WaniKani error";
            line1 = state().ui.gNetLine;
            line2 = "Press Enter to retry";
            action = "Enter: retry";
            break;
        case BootState::Failed:
            title = "Sync failed";
            line1 = state().ui.gNetLine;
            line2 = "Check Wi-Fi and token";
            action = "Enter: retry";
            break;
        case BootState::Ready:
            title = "Ready";
            line1 = "Press Enter to open Home";
            action = "Enter: home";
            break;
    }

    kk::app::BootView view;
    view.batteryPercent = tgc::board::batteryPercent();
    view.title = title;
    view.line1 = line1;
    view.line2 = line2;
    view.action = action;
    view.warn = st == BootState::NoSd || st == BootState::NoPack ||
                st == BootState::AuthError || st == BootState::Forbidden ||
                st == BootState::RateLimited || st == BootState::ServerError ||
                st == BootState::Failed;

    auto& d = target();
    d.startWrite();
    kk::app::drawBootScreen(d, view);
    d.endWrite();
    present();
}

void drawScreen() {
    if (state().ui.gNoticeActive) {
        drawNotice();
        return;
    }
    if (state().setup.gPortalActive) {
        drawSetupPortal();
        return;
    }
    if (state().wifi.gWifiActive) {
        drawWifiSetup();
        return;
    }
    if (state().ui.gLowBatteryPrompt) {
        drawLowBattery();
        return;
    }
    if (state().ui.gSignOutConfirm) {
        drawSignOutConfirm();
        return;
    }
    if (state().ui.gAboutActive) {
        drawAbout();
        return;
    }
    if (state().ui.gOutboxScreen) {
        drawOutbox();
        return;
    }
    if (state().stats.gStatsActive) {
        drawStats();
        return;
    }
    if (state().study.gStudyMenuActive) {
        drawStudyMenu();
        return;
    }
    if (state().study.gCriticalListActive) {
        drawCriticalList();
        return;
    }
    if (state().ota.gFirmwareActive) {
        drawFirmware();
        return;
    }
    if (state().ui.gSettingsActive) {
        drawSettings();
        return;
    }
    if (state().ui.gPauseActive) {
        drawPause();
        return;
    }
    if (state().sync.gSyncScreen) {
        drawSync();
        return;
    }
#ifdef KANIKEY_DEV
    if (state().dev.gShowRadicalTest) {
        showRadicalTest();
        return;
    }
#endif
    if (state().review.gReviewDetail) {
        showReviewDetail();
        return;
    }
    if (state().review.gReviewActive) {
        showReview();
        return;
    }
    if (state().review.gReviewSummary) {
        showReviewSummary();
        return;
    }
    if (state().lesson.gLessonEmptyActive) {
        showLessonEmpty();
        return;
    }
    if (state().lesson.gLessonStartActive) {
        drawLessonStart();
        return;
    }
    if (state().lesson.gLessonListActive) {
        showLessonList();
        return;
    }
    if (state().lesson.gLessonBatchQuizActive) {
        showLessonBatchQuiz();
        return;
    }
    if (state().lesson.gLessonQuizActive) {
        showLessonQuiz();
        return;
    }
    if (state().lesson.gLessonActive) {
        showLesson();
        return;
    }
#ifdef KANIKEY_DEV
    if (state().dev.gShowDebug) {
        showDebug();
        return;
    }
#endif
    if (state().session.gShowHome) {
        drawHome();
        return;
    }
    drawBoot();
}

// CFG-04: draw the static help table over the frame drawScreen() just put on
// the canvas. The caller owns the transfer window; the underlying frame stays
// untouched and comes back when Esc closes the overlay.
void drawHelpOverlay() {
    if (!state().display.gDisp) return;
    const kk::HelpTable table = kk::helpFor(state().ui.gHelpScreen);
    if (!table.rows || !table.count) return;
    kk::app::HelpView view;
    view.title = kk::helpScreenTitle(state().ui.gHelpScreen);
    view.rows = table.rows;
    view.count = table.count;

    auto& d = target();
    d.startWrite();
    kk::app::drawHelpScreen(d, view);
    d.endWrite();
    present();
}

void draw() {
    drawScreen();
    if (state().ui.gHelpActive) drawHelpOverlay();
}

// True only when draw() would actually show Home. The SPEED-07 minute tick
// must not repaint a review, a modal, the portal, the dev asset server or the
// manual sync screen; several of those keep gShowHome set.
bool homeOwnsScreen() {
    if (!state().session.gShowHome) return false;
    if (state().setup.gPortalActive || state().storage.gAssetServerActive) return false;
    if (state().wifi.gWifiActive) return false;
    if (state().ui.gNoticeActive || state().ui.gLowBatteryPrompt ||
        state().ui.gSignOutConfirm || state().ui.gAboutActive ||
        state().ui.gOutboxScreen || state().stats.gStatsActive ||
        state().study.gStudyMenuActive || state().study.gCriticalListActive ||
        state().ota.gFirmwareActive || state().ui.gSettingsActive ||
        state().ui.gPauseActive || state().sync.gSyncScreen ||
        (state().sync.gSyncJob.active && state().sync.gSyncJob.modal) ||
        state().review.gReviewDetail || state().review.gReviewActive ||
        state().review.gReviewSummary)
        return false;
    if (state().lesson.gLessonEmptyActive || state().lesson.gLessonStartActive ||
        state().lesson.gLessonListActive || state().lesson.gLessonBatchQuizActive ||
        state().lesson.gLessonQuizActive || state().lesson.gLessonActive)
        return false;
#ifdef KANIKEY_DEV
    if (state().dev.gShowDebug || state().dev.gShowRadicalTest) return false;
#endif
    return true;
}

}

// ONB-01: the visual packs are two files. Open the font first: the radical
// fallback checks whether glyphs.kgp is a legacy combined pack.
bool openFontPack(bool checkCrc) {
    if (!state().storage.gSdOk) return false;
    const bool ok = state().storage.gFontPack.open(SD, "/kanikey/glyphs.kgp",
                                                   kk::PackRole::Font, checkCrc);
    Serial.printf("[pack] font: %s\n", state().storage.gFontPack.statusLine());
    return ok;
}

bool openRadicalPack(bool checkCrc) {
    if (!state().storage.gSdOk) return false;
    const bool ok = state().storage.gRadicalPack.open(SD, "/kanikey/radicals.kgp",
                                                      kk::PackRole::Radical, checkCrc);
    if (ok) {
        Serial.printf("[pack] radicals: %s\n", state().storage.gRadicalPack.statusLine());
        return true;
    }
    // Before ONB-01 the only file was a combined v1 pack at glyphs.kgp. Keep
    // that card working as both roles; a v2 font pack fails the radical role
    // check and leaves the explicit "pack missing" state for radicals.kgp.
    const kk::GlyphPack& font = state().storage.gFontPack;
    if (font.ready() && font.version() == kk::kGlyphPackVersion1 && font.radicalCount() > 0) {
        const bool legacy = state().storage.gRadicalPack.open(
            SD, "/kanikey/glyphs.kgp", kk::PackRole::Radical, checkCrc);
        Serial.printf("[pack] radicals (v1 combined): %s\n",
                      state().storage.gRadicalPack.statusLine());
        return legacy;
    }
    Serial.printf("[pack] radicals: %s\n", state().storage.gRadicalPack.statusLine());
    return false;
}

void openGlyphPacks(bool checkCrc) {
    openFontPack(checkCrc);
    openRadicalPack(checkCrc);
}

void onKey(const KeyEvent& e) { onKeyImpl(e); }

void setup() {
    Serial.setRxBufferSize(4096);
    Serial.begin(115200);
    delay(250);

    tgc::board::begin();
    state().display.gDisp = &tgc::board::display();
    // The Cardputer ADV panel uses rotation 0 for landscape (offset_rotation
    // is 1 in M5GFX's board data); see board/BoardCardputerAdv.cpp.
    state().display.gDisp->setRotation(0);
    state().display.gDisp->setBrightness(kk::settingsBrightness());
    kk::settingsApplyTimeZone();

    ensureCanvas();
    kk::wkSetRequestHooks(requestBefore, requestAfter);
    firmwareBoot();

    Serial.printf("\n[kk] KaniKey boot\n");
    Serial.printf("[kk] version %s (%s)\n", kk::kFirmwareVersion, kk::ota::buildEnv());
    Serial.printf("[kk] canvas: %s (%u bytes, %d bpp)\n", state().display.gCanvas ? "ok" : "none",
                  static_cast<unsigned>(state().display.gCanvas ? state().display.gCanvas->bufferLength() : 0),
                  static_cast<int>(state().display.gCanvas ? state().display.gCanvas->getColorDepth() : 0));
    Serial.printf("[kk] board: %s\n", tgc::board::name());
    Serial.printf("[kk] flash: %u MB, PSRAM: %u MB\n",
                  static_cast<unsigned>(ESP.getFlashChipSize() / (1024 * 1024)),
                  static_cast<unsigned>(ESP.getPsramSize() / (1024 * 1024)));
    Serial.printf("[kk] internal heap: %u free, %u largest\n",
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                         MALLOC_CAP_8BIT)));
    Serial.printf("[kk] keyboard: %s\n", tgc::board::hasKeyboard() ? "yes" : "no");
    Serial.printf("[kk] idle dim %d s, blank %d s, sleep %d s\n", kk::settingsDimSeconds(),
                  kk::settingsBlankSeconds(), kk::settingsSleepSeconds());

    // Mount without formatting first: a failed mount must not wipe unsent
    // reviews. The format choice is explicit on the storage prompt.
    state().storage.gFsOk = LittleFS.begin(false);
    if (state().storage.gFsOk) {
        kk::outboxLoad(state().outbox.gOutbox);
        Serial.printf("[outbox] pending %u\n",
                      static_cast<unsigned>(kk::outboxPendingCount(state().outbox.gOutbox)));
    } else {
        Serial.println("[kk] LittleFS mount failed");
        state().storage.gFsFormatPrompt = true;
        state().ui.gNoticeAction = NoticeAction::StorageFormat;
        state().ui.gNoticeActive = true;
        snprintf(state().ui.gNoticeLine, sizeof state().ui.gNoticeLine, "Pending reviews cannot be read.");
        state().ui.gNoticeLine2[0] = 0;
    }

    state().storage.gSdOk = mountSd();
    if (state().storage.gSdOk) openGlyphPacks();
    if (state().storage.gFsFormatPrompt) {
        state().ui.gLastInputAt = millis();
        state().display.gDirty = true;
        Serial.println("[kk] waiting for the storage choice");
        draw();
        state().display.gDirty = false;
        state().display.gLastDrawMs = millis();
        return;  // no sync or review before the choice
    }
    state().ui.gLastInputAt = millis();
    state().display.gDirty = true;
    Serial.println("[kk] first draw");
    draw();
    Serial.println("[kk] first draw done");
    state().display.gDirty = false;
    state().display.gLastDrawMs = millis();

    continueBootAfterStorage();
}

void loop() {
    tgc::board::update();
    handleSerial();
    // The portal closes itself after 15 idle minutes; leave state().setup.gPortalActive set
    // until here so the hooks can keep their own messages.
    if (state().setup.gPortalActive) {
        state().setup.gSetupPortal.poll();
        if (state().setup.gPortalActive && !state().setup.gSetupPortal.active()) {
            state().setup.gPortalActive = false;
            snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Setup timed out");
            state().display.gDirty = true;
        } else if (state().setup.gSetupPortal.takeRedrawRequest()) {
            state().display.gDirty = true;
        }
    }
    if (!state().setup.gPortalActive) serviceAssetServer();
    if (state().storage.gPackRecheckPending) {
        // Deferred from portalRecheckPack(): the HTTP request has returned, so
        // the new handle cannot be invalidated by the client closing.
        state().storage.gPackRecheckPending = false;
        const bool checkCrc = !state().storage.gPackRecheckSkipCrc;
        state().storage.gPackRecheckSkipCrc = false;
        if (!state().storage.gSdOk) state().storage.gSdOk = mountSd();
        if (state().storage.gSdOk) openGlyphPacks(checkCrc);
        state().display.gDirty = true;
    }

    const uint32_t now = millis();
    firmwareTick(now);
    if (powerWakeCheck(now)) {
        // The panel is blank. A background sync is not a screen and must not
        // stall until the next key; the modal Sync screen only advances while
        // it is visible (Esc still cancels it after a wake).
        if (kk::syncRunsWhileScreenOff(state().sync.gSyncJob.active, state().sync.gSyncJob.modal))
            syncAdvance();
        return;
    }

    bool firstKey = true;
    KeyEvent e;
    while (tgc::board::nextKeyEvent(&e)) {
        state().ui.gLastInputAt = now;
        if (firstKey) {
            firstKey = false;
            if (state().ui.gDimmed) wakeScreen();
        }
        if (e.key == Key::None) {
#ifdef KANIKEY_DEV
            if (state().lesson.gDevKeyTest) {
                Serial.println("[key] test marker (all released)");
                state().lesson.gDevKeyTest = false;
            }
#endif
            // The TCA8418 lost events: treat every key as released.
            state().ui.gShift = state().ui.gFn = state().ui.gCtrl = state().ui.gOpt = state().ui.gAlt = false;
            pushLog("^ (all keys released)");
            state().display.gDirty = true;
            continue;
        }
        onKey(e);
    }

    // Advance the automatic or manual sync by at most one request, then draw
    // the frame the keys selected.
    syncAdvance();

    if (state().display.gDirty && now - state().display.gLastDrawMs >= 16) {
        draw();
        state().display.gDirty = false;
        state().display.gLastDrawMs = now;
    }
    if (now - state().display.gLastStatusMs >= 1000) {
        drawStatus();
        state().display.gLastStatusMs = now;
        // Refresh the visible 429 countdown once per second, but not while a
        // blocking sync owns the loop. The last tick also clears the display
        // when the gate expires.
        static uint32_t lastRateWait = 0;
        const uint32_t rateWait = kk::wkRateLimitWaitSeconds();
        if (!state().sync.gSyncing && state().sync.gSyncIssue == SyncIssue::RateLimit &&
            (rateWait > 0 || lastRateWait > 0))
            state().display.gDirty = true;
        lastRateWait = rateWait;
    }
    // SPEED-07: once a minute, recompute Home from the cached buckets so a
    // review that became due shows up without a sync, and force a redraw so
    // the battery percentage is re-read too. No network; skipped while another
    // screen owns the display.
    if (now - state().display.gLastHomeRefreshMs >= 60000 && homeOwnsScreen()) {
        state().display.gLastHomeRefreshMs = now;
        applySummary(effectiveNow());
        state().display.gDirty = true;
    }

    // G0: a short press wakes the screen; a long press is the future power
    // action. For M0 it just logs, so the button can be checked too.
    if (tgc::board::wasPressed(tgc::board::Button::Select)) {
        Serial.println("[key] G0 pressed");
    }
    if (tgc::board::wasHeld(tgc::board::Button::Select)) {
        Serial.println("[key] G0 held");
    }

    if (powerTick(now)) return;

    static uint32_t lastBeat = 0;
    if (now - lastBeat > 5000) {
        lastBeat = now;
        Serial.printf("[kk] alive heap=%u\n",
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    }
    delay(4);
}

}  // namespace kk::app

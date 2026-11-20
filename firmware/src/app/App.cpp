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
#include <esp_partition.h>
#include <LittleFS.h>
#include <esp_system.h>
#include <time.h>

#include <algorithm>
#include <cstring>

#include "app/AboutScreen.h"
#include "app/App.h"
#include "app/AppState.h"
#include "app/AppUi.h"
#include "app/AudioController.h"
#include "app/CacheController.h"
#include "app/FeedbackTrace.h"
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
#include "app/RemProbe.h"
#include "app/ReminderController.h"
#include "app/RemindDev.h"
#if defined(TGC_BOARD_PAPERDIY)
#include "app/CadenceController.h"
#include "wk/SyncSteps.h"
#endif
#include "platform/AudioCache.h"
#include "platform/CriticalStore.h"
#include "platform/GlyphPack.h"
#include "platform/JournalStore.h"
#include "platform/PracticeStore.h"
#include "platform/SetupPortal.h"
#include "platform/Sound.h"
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
using kk::Modal;
using kk::Screen;
using kk::ScreenState;
using namespace kk::ui;

// CFG-04: which help table the current frame offers. The order follows draw()
// so the overlay never outlives the screen it describes; the blocking frames
// that own the keyboard (portal, Wi-Fi setup, low battery, sign-out) and the
// screens without a table return None.
kk::HelpScreen helpScreenForState() {
    const ScreenState& s = state().screen;
    const Screen base = kk::screenBase(s);
    const Modal top = kk::screenTop(s);
    if (state().ui.gNoticeActive) return kk::HelpScreen::None;
    if (base == Screen::Setup) return kk::HelpScreen::None;
    if (state().wifi.gWifiActive) return kk::HelpScreen::None;
    if (top == Modal::LowBattery || top == Modal::SignOut || top == Modal::About)
        return kk::HelpScreen::None;
    if (base == Screen::Outbox) return kk::HelpScreen::Outbox;
    if (state().stats.gStatsActive) return kk::HelpScreen::None;
    if (state().study.gStudyMenuActive || state().study.gCriticalListActive)
        return kk::HelpScreen::None;
    if (base == Screen::Firmware) return kk::HelpScreen::Firmware;
    if (top == Modal::Settings) return kk::HelpScreen::Settings;
    if (top == Modal::Pause) return kk::HelpScreen::None;
    if (base == Screen::Sync) return kk::HelpScreen::None;
#ifdef KANIKEY_DEV
    if (base == Screen::RadicalTest) return kk::HelpScreen::None;
#endif
    if (base == Screen::Review) {
        if (state().review.detail) return kk::HelpScreen::None;
        return state().review.feedback ? kk::HelpScreen::ReviewsFeedback
                                        : kk::HelpScreen::ReviewsQuestion;
    }
    if (base == Screen::ReviewSummary) return kk::HelpScreen::None;
    if (base == Screen::LessonEmpty || base == Screen::LessonList)
        return kk::HelpScreen::None;
    if (base == Screen::LessonQuiz) return kk::HelpScreen::LessonQuiz;
    if (base == Screen::LessonCard) return kk::HelpScreen::LessonCard;
#ifdef KANIKEY_DEV
    if (base == Screen::Debug) return kk::HelpScreen::None;
#endif
    if (base == Screen::Home) return kk::HelpScreen::Home;
    return kk::HelpScreen::None;
}

// Low battery is a modal: Enter continues into the action it was opened for,
// Esc returns to the screen underneath.
void lowBatteryKey(const KeyEvent& e) {
    if (!e.down) return;
    if (e.key == Key::Enter) {
        const int action = state().ui.gLowBatteryAction;
        kk::screenPop(&state().screen);
        state().ui.gLowBatteryAction = 0;
        state().ui.gLowBatteryConfirmed = true;
        if (action == 1)
            startReview();
        else if (action == 2)
            startLessons();
        state().display.gDirty = true;
    } else if (e.key == Key::Esc) {
        const Screen base = kk::screenBase(state().screen);
        kk::screenPop(&state().screen);
        state().ui.gLowBatteryAction = 0;
        state().ui.gLowBatteryConfirmed = false;
        // The old Esc path set the Home flag. A retry from the summary kept the
        // summary underneath the prompt, so preserve that one case.
        if (base != Screen::ReviewSummary)
            state().screen.screen = Screen::Home;
        state().display.gDirty = true;
    }
}

// PD-03: Home on a two-button board is a focus ring over its read-only
// destinations. Reviews are not a target (they need a keyboard); their count
// stays on the screen.
void homeFocusOpen() {
    switch (state().ui.gHomeFocus % 4) {
        case 0: startLessons(); break;
        case 1: openStats(); break;
        case 2:
            if (!state().sync.gSyncing) startSync();
            break;
        default: openSettings(); break;
    }
    state().display.gDirty = true;
}

void onKeyImpl(const KeyEvent& e) {
    // REV-10 latency: the previous completion's writes run before this key
    // (usually they already ran after that frame; this is the no-op path).
    flushPendingReviewWrites();
#ifdef KANIKEY_DEV
    if (state().lesson.gDevKeyTest) {
        Serial.printf("[key] test %s %s\n", e.key == Key::None ? "marker" : "event",
                      e.down ? "down" : "up");
        if (e.key == Key::None) state().lesson.gDevKeyTest = false;
    }
#endif
    updateModifier(e);
    // M6: a key that arrives before a pending autoplay has started cancels it.
    if (e.down) audioAutoplayCancel();
    if (kk::screenBase(state().screen) == Screen::Setup) {
        if (e.down && e.key == Key::Esc) {
            state().setup.gSetupPortal.end(false);
            kk::screenReset(&state().screen);
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
    // with gShift set; plain / must keep acting as Right, so match the printed
    // '?' rather than Key::Other. A BLE keyboard sends the same chord as '/'
    // with its Shift bit set, so neither source needs the grid position.
    if (e.down && tgc::keyChar(e, state().ui.gShift) == '?') {
        const kk::HelpScreen screen = helpScreenForState();
        if (screen != kk::HelpScreen::None) {
            audioStop();
            state().ui.gHelpActive = true;
            state().ui.gHelpScreen = screen;
            // Like any other key on correct feedback, opening help cancels the
            // auto-advance timer; closing returns to the same feedback.
            state().review.feedbackCorrectAt = 0;
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

    // One modal dispatch above the base screen. Settings, About and Sign out
    // share settingsKey(), exactly as they did when the flags were tested in
    // that order inside it.
    switch (kk::screenTop(state().screen)) {
        case Modal::LowBattery:
            lowBatteryKey(e);
            return;
        case Modal::SignOut:
        case Modal::About:
        case Modal::Settings:
            settingsKey(e);
            return;
        case Modal::Pause:
            pauseKey(e);
            return;
        case Modal::OutboxConfirm:
            outboxKey(e);
            return;
        case Modal::FirmwareConfirm:
            firmwareKey(e);
            return;
        case Modal::None:
            break;
    }

    if (state().stats.gStatsActive) {
        statsKey(e);
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

    switch (kk::screenBase(state().screen)) {
        case Screen::Review:
            if (state().review.detail)
                reviewDetailKey(e);
            else
                reviewKey(e);
            return;
        case Screen::ReviewSummary:
            reviewSummaryKey(e);
            return;
        case Screen::LessonEmpty:
            if (e.down && (e.key == Key::Enter || e.key == Key::Esc)) {
                state().screen.screen = Screen::Home;
                state().display.gDirty = true;
            }
            return;
        case Screen::LessonList:
            if (state().lesson.startConfirm)
                lessonStartKey(e);
            else
                lessonListKey(e);
            return;
        case Screen::LessonCard:
            lessonKey(e);
            return;
        case Screen::LessonQuiz:
            if (state().lesson.quizBatch)
                lessonBatchQuizKey(e);
            else
                lessonQuizKey(e);
            return;
        case Screen::Outbox:
            outboxKey(e);
            return;
        case Screen::Firmware:
            firmwareKey(e);
            return;
        case Screen::Sync:
            syncScreenKey(e);
            return;
        case Screen::RadicalTest:
#ifdef KANIKEY_DEV
            if (e.down && e.key == Key::Esc) {
                state().screen.screen = state().dev.radicalReturn;
                state().display.gDirty = true;
            }
#endif
            return;
        case Screen::Debug:
        case Screen::Boot:
        case Screen::Home:
        case Screen::Setup:
            break;
    }

#ifdef KANIKEY_DEV
    if (e.down && e.key == Key::Tab) {
        if (kk::screenBase(state().screen) == Screen::Debug) {
            state().screen.screen = state().dev.debugReturn;
        } else {
            state().dev.debugReturn = kk::screenBase(state().screen);
            state().screen.screen = Screen::Debug;
        }
        state().display.gDirty = true;
        return;
    }
#endif
    const Screen base = kk::screenBase(state().screen);
    // The debug screen is an overlay over Home or Boot, exactly like the old
    // debug flag: its keys underneath still work while it is visible.
    Screen underBase = base;
#ifdef KANIKEY_DEV
    if (base == Screen::Debug) underBase = state().dev.debugReturn;
#endif
    if (e.down && underBase == Screen::Home && e.key == Key::Esc) {
        openSettings();
        return;
    }
    if (e.down && underBase == Screen::Home && tgc::board::buttonsOnly()) {
        if (e.key == Key::Down || e.key == Key::Up) {
            state().ui.gHomeFocus = static_cast<uint8_t>(
                e.key == Key::Down ? (state().ui.gHomeFocus + 1) % 4 : (state().ui.gHomeFocus + 3) % 4);
            state().display.gDirty = true;
            return;
        }
        if (e.key == Key::Enter) {
            homeFocusOpen();
            return;
        }
    }
    if (e.down && e.key == Key::Enter) {
        if (underBase == Screen::Home && state().session.gHaveSummary) {
            if (state().session.gSummary.dueReviews(effectiveNow()) == 0) {
                // Nothing due: no network call, just the next time. Refresh it
                // first: answering the last due item leaves state().session.gNextReview at "now".
                applySummary(effectiveNow());
                snprintf(state().ui.gNoticeLine, sizeof state().ui.gNoticeLine, "Next review  %s", state().session.gNextReview);
                state().ui.gNoticeLine2[0] = 0;
                state().ui.gNoticeAction = NoticeAction::ReviewEmpty;
                state().ui.gNoticeActive = true;
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
    // One character lookup for the Boot/Home shortcuts and the log line: for a
    // matrix event keyChar() maps the grid once, for BLE it uses the event's
    // own character and Shift bit.
    const char c = tgc::keyChar(e, state().ui.gShift);
    // The boot "Wi-Fi needed" state keeps the phone portal as the fallback
    // (SETUP-09): Enter opens the on-device scan list, P opens the portal.
    if (e.down && underBase == Screen::Boot && e.key == Key::Other &&
        !state().sync.gSyncing && state().storage.gSdOk &&
        state().storage.gFontPack.ready() && !kk::DevWifi::hasSaved()) {
        if (c == 'p' || c == 'P') {
            startSetupPortal();
            return;
        }
    }
    if (e.down && underBase == Screen::Home && e.key == Key::Other &&
        (c == 'o' || c == 'O')) {
        openOutbox();
        return;
    }
    if (e.down && underBase == Screen::Home && e.key == Key::Other &&
        (c == 'l' || c == 'L')) {
        startLessons();
        return;
    }
    if (e.down && underBase == Screen::Home && e.key == Key::Other &&
        (c == 't' || c == 'T')) {
        openStats();
        return;
    }
    if (e.down && underBase == Screen::Home && e.key == Key::Other &&
        (c == 'p' || c == 'P')) {
        openStudyMenu();
        return;
    }
    if (e.down && underBase == Screen::Home && !state().sync.gSyncing && e.key == Key::Other &&
        (c == 's' || c == 'S')) {
        startSync();
        return;
    }

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

// REM-02: the due count of the banner that is on screen; 0 when none is.
uint32_t gReminderBannerDue = 0;
constexpr uint32_t kReminderBannerMs = 30000;

kk::app::HomeView buildHomeView() {
    kk::app::HomeView view;
    view.batteryPercent = tgc::board::batteryPercent();
    view.signedIn = state().session.gHaveUser;
    view.level = state().session.gHaveUser ? state().session.gUser.level : 0;
    view.username = state().session.gHaveUser ? state().session.gUser.username.c_str() : "";
    view.dueReviews = state().session.gDueReviews;
    view.lessons = state().session.gLessons;
    view.nextReview = state().session.gNextReview;
    if (gReminderBannerDue) {
        view.reminderActive = true;
        snprintf(view.reminder, sizeof view.reminder, "%u %s ready",
                 static_cast<unsigned>(gReminderBannerDue), gReminderBannerDue == 1 ? "review" : "reviews");
    }
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
    view.buttonMenu = tgc::board::buttonsOnly();
    view.focus = static_cast<uint8_t>(state().ui.gHomeFocus % 4);

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

#if defined(TGC_BOARD_PAPERDIY)
// PD-04: the sleeping frame is the Home frame with the sleep state in place of
// the status line and the hint. The battery is the header's percentage and the
// charging state is text. A full refresh is the frame that stays on the dark
// panel; a cadence wake takes the PD-02 policy (fullRefresh false).
void drawSleepingFrame(bool fullRefresh) {
    if (!state().display.gDisp) return;
    kk::app::HomeView view = buildHomeView();
    view.asleep = true;
    // A sync or battery problem keeps its own status line.
    if (view.statusColor != kErr && view.statusColor != kWarn) {
        snprintf(view.statusText, sizeof view.statusText, "%s",
                 tgc::board::isCharging() ? "Asleep: charging" : "Asleep: on battery");
        view.statusColor = kFaint;
        view.levelUp = false;
    }
    tgc::board::setDisplayOn(true);  // the boost for this frame; deepSleep() turns it off
    auto& d = target();
    d.startWrite();
    kk::app::drawHomeScreen(d, view);
    d.endWrite();
    if (fullRefresh)
        presentFullNow();
    else
        presentNow();
    state().display.gDisp->waitDisplay();
}

// PD-04: a button is down, or was pressed since the sync started.
bool buttonsActive() {
    using tgc::board::Button;
    return tgc::board::wasPressed(Button::Select) || tgc::board::wasPressed(Button::Next) ||
           tgc::board::isHeld(Button::Select) || tgc::board::isHeld(Button::Next);
}
#endif

// REM-02: the reminder timer woke the chip. No Wi-Fi and no TLS: the cached
// summary decides, and `now` is the clock rebuilt from the RTC (never time()).
// A reminder due shows the banner for 30 s; a key in that window returns, and
// the normal boot goes on. Otherwise the next wake is armed and the chip sleeps
// again, the panel dark. No cached summary: no banner, the same sleep.
void reminderTimerWake(uint32_t now) {
    const bool haveCache = loadCache();
    const kk::Summary* forecast = haveCache ? &state().session.gSummary : nullptr;
    const uint32_t due = haveCache ? state().session.gSummary.dueReviews(now) : 0;
    const bool banner = haveCache && kk::app::reminderBannerWanted(due, now);
#ifdef KANIKEY_DEV
    Serial.printf("[rem] timer wake: due %u, banner %s\n", static_cast<unsigned>(due),
                  banner ? "yes" : "no");
    kk::app::remindDevWakeDecision(due, banner);
#endif
    if (!banner) {
        firmwareBeforeSleep();
        tgc::board::deepSleep(kk::app::reminderArmAfterWake(now, forecast));
    }
    applySummary(now);
    gReminderBannerDue = due;
    kk::app::reminderNoteFired(now);
    drawHome();
    tgc::board::setBrightness(kk::settingsBrightness());
    // REM-03: the reminder chime (M4) as the banner appears. loop() does not run
    // in this wait, so the banner loop below advances the chime itself.
    kk::sound::soundCue(kk::SoundCue::ReminderChime, 0, state().sync.gSyncing,
                        state().setup.gSetupPortal.active());

    const uint32_t shownAt = millis();
    bool key = false;
    while (!key && millis() - shownAt < kReminderBannerMs) {
        tgc::board::update();
        KeyEvent e;
        while (tgc::board::nextKeyEvent(&e))
            if (e.down && e.key != Key::None) key = true;
        if (tgc::board::wasPressed(tgc::board::Button::Select)) key = true;
        kk::sound::soundTick(millis());
        if (!key) delay(10);
    }
    gReminderBannerDue = 0;
    kk::sound::soundStop();  // a key or the expiry ends the chime before sleep or boot
    if (key) {
#ifdef KANIKEY_DEV
        Serial.println("[rem] key during the banner: normal boot");
        kk::app::remindDevBannerEnd(true);
#endif
        return;
    }
    const uint32_t after = now + static_cast<uint32_t>((millis() - shownAt + 999u) / 1000u);
#ifdef KANIKEY_DEV
    kk::app::remindDevBannerEnd(false);
#endif
    firmwareBeforeSleep();
#if defined(TGC_BOARD_PAPERDIY)
    drawSleepingFrame(true);  // PD-04: the banner gives way to the sleeping frame
#endif
    tgc::board::deepSleep(kk::app::reminderArmAfterWake(after, forecast));
}

#if defined(TGC_BOARD_PAPERDIY)
// PD-04: a sync wake, the cadence's. The Quick job runs in the background: no
// screen and no key. The panel changes only when the numbers did (the PD-02
// policy), then the device sleeps again, unless a button is down or was pressed
// (the normal boot takes that). Returns false when the normal boot must go on;
// true when the job ran and the caller goes on (`sleepWhenDone` false, a reminder
// is due too). With sleepWhenDone it does not return: it sleeps.
constexpr uint32_t kCadenceSyncMaxMs = 150000;

bool cadenceSyncWake(uint32_t now, bool sleepWhenDone) {
    if (!loadCache()) {
#ifdef KANIKEY_DEV
        Serial.println("[pd] cadence wake: no cache; normal boot");
#endif
        return false;
    }
    // The Home numbers as they stand now, on the clock rebuilt from the RTC.
    applySummary(now);
    const uint32_t dueBefore = state().session.gDueReviews;
    const uint32_t lessonsBefore = state().session.gLessons;
    char nextBefore[sizeof state().session.gNextReview];
    std::memcpy(nextBefore, state().session.gNextReview, sizeof nextBefore);

    kk::app::startSyncAuto(kk::SyncKind::Quick, false);
    if (!state().sync.gSyncJob.active) {
#ifdef KANIKEY_DEV
        Serial.println("[pd] cadence wake: sync not started; normal boot");
#endif
        return false;
    }
    bool pressed = false;
    const uint32_t startedMs = millis();
    while (state().sync.gSyncJob.active && millis() - startedMs < kCadenceSyncMaxMs) {
        tgc::board::update();
        pressed = pressed || buttonsActive();
        syncAdvance();
        delay(4);
    }
    const bool ok = !state().sync.gSyncJob.active && state().sync.gSyncJob.step == kk::SyncStep::Done;
    const uint32_t nowAfter = now + (millis() + 999u) / 1000u;
    cadenceNoteSync(ok, nowAfter);
    applySummary(nowAfter);
    const bool redraw = dueBefore != state().session.gDueReviews ||
                        lessonsBefore != state().session.gLessons ||
                        std::strcmp(nextBefore, state().session.gNextReview) != 0;
    if (redraw) drawSleepingFrame(false);
    tgc::board::update();
    pressed = pressed || buttonsActive();
#ifdef KANIKEY_DEV
    Serial.printf("[pd] cadence wake: sync %s, redraw %s, awake %lu ms\n", ok ? "ok" : "fail",
                  redraw ? "yes" : "no", static_cast<unsigned long>(millis()));
#endif
    if (pressed) return false;
    if (!sleepWhenDone) return true;
    firmwareBeforeSleep();
    tgc::board::deepSleep(kk::app::reminderArmAfterWake(
        nowAfter, state().session.gHaveSummary ? &state().session.gSummary : nullptr));
}

// PD-04: a timer wake on the PaperDIY. The one timer may be the sync cadence, the
// reminder or both. A wake that serves neither (an early RTC) keeps the cadence
// target and sleeps again.
void paperTimerWake(uint32_t now) {
    const bool cadence = kk::app::bootCadenceDue(now);
    const bool reminder = kk::app::bootReminderDue(now);
    if (cadence && !cadenceSyncWake(now, !reminder)) return;  // the normal boot
    if (reminder) {
        reminderTimerWake(now);  // the banner, or it sleeps again
        return;
    }
    if (cadence) return;  // not reached: a sync with sleepWhenDone sleeps or returns false
    loadCache();
    firmwareBeforeSleep();
    tgc::board::deepSleep(kk::app::reminderArmAfterWake(
        now, state().session.gHaveSummary ? &state().session.gSummary : nullptr));
}
#endif

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
            action = pickHint("Enter: retry", "hold A: retry");
            break;
        case BootState::NoPack:
            title = "Pack missing";
            line1 = pickHint("Press Enter to download it", "Hold A to download it");
            line2 = "from the setup page";
            action = pickHint("Enter: setup", "hold A: setup");
            break;
        case BootState::NoToken:
            title = "Token needed";
            line1 = pickHint("Press Enter for setup", "Hold A for setup");
            line2 = "Wi-Fi and token from your phone";
            action = pickHint("Enter: setup", "hold A: setup");
            break;
        case BootState::NoWifi:
            title = "Wi-Fi needed";
            line1 = pickHint("Set up a 2.4 GHz network here", "Set up Wi-Fi with a phone");
            line2 = pickHint("or press P for the phone portal", "then open the setup page");
            action = pickHint("Enter: on-device  P: phone", "hold A: setup");
            break;
        case BootState::NotSynced:
            title = "Not synced";
            line1 = state().ui.gNetLine;
            line2 = pickHint("Press Enter to sync", "Hold A to sync");
            action = pickHint("Enter: sync", "hold A: sync");
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
            line2 = pickHint("Press Enter to open setup", "Hold A to open setup");
            action = pickHint("Enter: setup", "hold A: setup");
            break;
        case BootState::Forbidden:
            title = "Missing permissions";
            line1 = "Edit the token in WaniKani";
            line2 = pickHint("Press Enter to open setup", "Hold A to open setup");
            action = pickHint("Enter: setup", "hold A: setup");
            break;
        case BootState::RateLimited: {
            const uint32_t wait = kk::wkRateLimitWaitSeconds();
            if (wait)
                snprintf(retryLine, sizeof retryLine, "Retry in %us",
                         static_cast<unsigned>(wait));
            else
                snprintf(retryLine, sizeof retryLine, "%s",
                         pickHint("Press Enter to retry", "Hold A to retry"));
            title = "Rate limited";
            line1 = "WaniKani asked KaniKey to wait";
            line2 = retryLine;
            action = pickHint("Enter: retry", "hold A: retry");
            break;
        }
        case BootState::ServerError:
            title = "WaniKani error";
            line1 = state().ui.gNetLine;
            line2 = pickHint("Press Enter to retry", "Hold A to retry");
            action = pickHint("Enter: retry", "hold A: retry");
            break;
        case BootState::Failed:
            title = "Sync failed";
            line1 = state().ui.gNetLine;
            line2 = "Check Wi-Fi and token";
            action = pickHint("Enter: retry", "hold A: retry");
            break;
        case BootState::Ready:
            title = "Ready";
            line1 = pickHint("Press Enter to open Home", "Hold A to open Home");
            action = pickHint("Enter: home", "hold A: home");
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

void drawBaseScreen() {
    switch (kk::screenBase(state().screen)) {
        case Screen::Boot:
            drawBoot();
            return;
        case Screen::Home:
            drawHome();
            return;
        case Screen::Review:
            if (state().review.detail)
                showReviewDetail();
            else
                showReview();
            return;
        case Screen::ReviewSummary:
            showReviewSummary();
            return;
        case Screen::LessonEmpty:
            showLessonEmpty();
            return;
        case Screen::LessonList:
            if (state().lesson.startConfirm)
                drawLessonStart();
            else
                showLessonList();
            return;
        case Screen::LessonCard:
            showLesson();
            return;
        case Screen::LessonQuiz:
            if (state().lesson.quizBatch)
                showLessonBatchQuiz();
            else
                showLessonQuiz();
            return;
        case Screen::Outbox:
            drawOutbox();
            return;
        case Screen::Firmware:
            drawFirmware();
            return;
        case Screen::Sync:
            drawSync();
            return;
        case Screen::Setup:
            drawSetupPortal();
            return;
        case Screen::Debug:
#ifdef KANIKEY_DEV
            showDebug();
#else
            drawBoot();
#endif
            return;
        case Screen::RadicalTest:
#ifdef KANIKEY_DEV
            showRadicalTest();
#else
            drawBoot();
#endif
            return;
    }
}

void drawModal() {
    switch (kk::screenTop(state().screen)) {
        case Modal::None:
            return;
        case Modal::Pause:
            drawPause();
            return;
        case Modal::Settings:
            drawSettings();
            return;
        case Modal::About:
            drawAbout();
            return;
        case Modal::SignOut:
            drawSignOutConfirm();
            return;
        case Modal::LowBattery:
            drawLowBattery();
            return;
        case Modal::OutboxConfirm:
        case Modal::FirmwareConfirm:
            // These two confirmations are part of their base frame: the outbox
            // list draws the confirm panel and the firmware view carries the
            // "Enter again" note. Their modal entry only gives keys priority.
            return;
    }
}

void drawScreen() {
    if (state().ui.gNoticeActive) {
        drawNotice();
        return;
    }
    const Screen base = kk::screenBase(state().screen);
    // The setup portal is the outermost screen after the notice frame; it used
    // to be checked before every other flag.
    if (base == Screen::Setup) {
        drawBaseScreen();
        return;
    }
    if (state().wifi.gWifiActive) {
        drawWifiSetup();
        return;
    }

    const Modal top = kk::screenTop(state().screen);

    // Keep the old priority order for the blocking frames: low battery, sign
    // out and About were drawn before the stats/study/outbox screens.
    if (top == Modal::LowBattery || top == Modal::SignOut || top == Modal::About) {
        drawModal();
        return;
    }
    if (base == Screen::Outbox) {
        drawBaseScreen();
        drawModal();
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

    // Pause, Settings, About, Sign out and low battery fill the panel. Draw the
    // top one alone: drawing the base first would push a full frame the user
    // never saw before this refactor. The remaining modals are painted as part
    // of their base (outbox confirm panel, firmware note), so the base is
    // needed there.
    const bool opaqueModal = top == Modal::Pause || top == Modal::Settings;
    if (!opaqueModal) drawBaseScreen();
    drawModal();
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

    // REV-04: C/A only exist while R7 Wrong answer actions is On. AUD-03: R
    // only exists when this frame's subject actually offers audio. The static
    // tables carry the full key set, so filter the rows the screen cannot use.
    bool hideWrong = false;
    if (state().ui.gHelpScreen == kk::HelpScreen::ReviewsFeedback) {
        int wrong = 0;
        hideWrong = !kk::settingsGetInt("rv.wrong", &wrong) || wrong == 0;
    }
    bool hideAudio = true;
    if (state().ui.gHelpScreen == kk::HelpScreen::ReviewsFeedback) {
        uint32_t subjectId = state().review.feedbackSubjectId;
        if (!subjectId && state().review.gQueue.current())
            subjectId = state().review.gQueue.current()->subjectId;
        const kk::Subject* s = subjectById(subjectId);
        hideAudio = !state().review.feedback ||
                    state().review.feedbackPart != kk::ReviewPart::Reading ||
                    state().review.gCheck.status == kk::AnswerStatus::Warning || !s ||
                    audioHintFor(*s) == kk::AudioHint::None;
    } else if (state().ui.gHelpScreen == kk::HelpScreen::LessonCard) {
        const kk::Subject* s = lessonCardSubject();
        hideAudio = !s || audioHintFor(*s) == kk::AudioHint::None;
    }
    if (hideWrong || hideAudio) {
        static kk::HelpEntry filtered[7];
        size_t count = 0;
        for (size_t i = 0; i < table.count && count < 7; ++i) {
            if (hideWrong && (std::strcmp(table.rows[i].keys, "C") == 0 ||
                              std::strcmp(table.rows[i].keys, "A") == 0))
                continue;
            if (hideAudio && std::strcmp(table.rows[i].keys, "R") == 0) continue;
            filtered[count++] = table.rows[i];
        }
        view.rows = filtered;
        view.count = count;
    }

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
// manual sync screen; Home is now the base screen instead of a flag.
bool homeOwnsScreen() {
    if (kk::screenBase(state().screen) != Screen::Home) return false;
    if (kk::screenTop(state().screen) != Modal::None) return false;
    if (state().storage.gAssetServerActive) return false;
    if (state().wifi.gWifiActive) return false;
    if (state().ui.gNoticeActive || state().stats.gStatsActive ||
        state().study.gStudyMenuActive || state().study.gCriticalListActive)
        return false;
    if (state().sync.gSyncJob.active && state().sync.gSyncJob.modal) return false;
    return true;
}

}

#if defined(TGC_BOARD_PAPERDIY)
// PD-04: the idle deep sleep. The sleeping frame is drawn before the timer is
// armed, so the timer and the RTC clock record are taken just before the sleep.
uint64_t paperIdleSleep(const kk::Summary* summary) {
    applySummary(effectiveNow());  // a review may have come due since the last minute tick
    drawSleepingFrame(true);
    return reminderArmIdle(summary);
}
#endif

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

namespace {

// REV-10: the only states that own an editable answer field. Feedback, the
// review detail card, Pause and the other modals keep their own key handling.
bool answerFieldActive() {
    if (state().ui.gHelpActive || kk::screenTop(state().screen) != Modal::None ||
        state().ui.gNoticeActive)
        return false;
    const Screen base = kk::screenBase(state().screen);
    if (base == Screen::Review) return !state().review.detail && !state().review.feedback;
    if (base == Screen::LessonQuiz) {
        if (state().lesson.quizBatch)
            return !state().lesson.gLessonBatchFeedback && !state().lesson.gLessonBatchQueue.empty();
        return !state().lesson.gLessonQuiz.feedback() && !state().lesson.gLessonQuiz.complete();
    }
    return false;
}

// FLASH-03: true when the LittleFS (`spiffs`) partition's first two 4 KB
// blocks (where LittleFS keeps its superblock pair) are fully erased, i.e. the
// partition was never formatted. Read in small chunks to keep the stack low.
bool littleFsPartitionErased() {
    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
    if (!part || part->size < 8192) return false;
    uint8_t buf[256];
    for (uint32_t off = 0; off < 8192; off += sizeof buf) {
        if (esp_partition_read(part, off, buf, sizeof buf) != ESP_OK) return false;
        for (uint8_t b : buf)
            if (b != 0xFF) return false;
    }
    return true;
}

}  // namespace

void setup() {
    Serial.setRxBufferSize(4096);
    Serial.begin(115200);
    delay(250);

    // REM-02: the reminder's own timer wake is read first. Its time comes from
    // the RTC, so nothing here touches time() or the radio.
    uint32_t reminderNow = 0;
    const bool reminderTimer = kk::app::reminderBootWake(&reminderNow);
    const bool timerWake = tgc::board::wokeFromTimer();
#ifdef KANIKEY_DEV
    kk::app::remindDevBoot(timerWake, reminderTimer, reminderNow);
#endif

    tgc::board::begin();
    state().display.gDisp = &tgc::board::display();
    // Landscape is board-specific (Cardputer ADV rotation 0, PaperDIY 1); the
    // board owns it, so a new panel cannot be left portrait here. A timer wake
    // keeps the panel dark until the reminder decides (see below).
    tgc::board::setOrientation(true);
    if (!timerWake) tgc::board::setBrightness(kk::settingsBrightness());
    kk::settingsApplyTimeZone();

    ensureCanvas();
    kk::wkSetRequestHooks(requestBefore, requestAfter);
    firmwareBoot();
#ifdef KANIKEY_DEV
    // REM-01 probe: a repeat run sleeps here, before storage, Wi-Fi and sync.
    remBootProbe();
#endif

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
    if (!state().storage.gFsOk && littleFsPartitionErased()) {
        // A never-used (erased) partition holds no reviews: a fresh install or
        // the FLASH-03 table move. Format it without the "Storage unreadable"
        // prompt, which is only for data that exists but cannot be read.
        const uint32_t t0 = millis();
        state().storage.gFsOk = LittleFS.begin(true);
        Serial.printf("[kk] LittleFS was blank: formatted %s in %lu ms\n",
                      state().storage.gFsOk ? "ok" : "FAILED",
                      static_cast<unsigned long>(millis() - t0));
    }
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
    // Register the speaker buffer-release callback before the first clip; the
    // player opens the account's audio cache lazily on the first hint/play.
    audioBegin();
    if (state().storage.gSdOk) openGlyphPacks();
    if (timerWake) {
        // REM-02: a reminder timer may show the banner or sleep again here,
        // before the first draw and before any Wi-Fi. Returns only when the
        // normal boot goes on (a key during the banner, or no reminder timer).
        // PD-04: on the PaperDIY the timer may also be the sync cadence, which
        // syncs and sleeps the same way (paperTimerWake).
        if (reminderTimer) {
#if defined(TGC_BOARD_PAPERDIY)
            paperTimerWake(reminderNow);
#else
            reminderTimerWake(reminderNow);
#endif
        }
        tgc::board::setBrightness(kk::settingsBrightness());
    }
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
#ifdef KANIKEY_DEV
    remNoteBootReady();
#endif
    state().display.gDirty = false;
    state().display.gLastDrawMs = millis();

    continueBootAfterStorage();
}

void loop() {
    tgc::board::update();
    // REM-03: advances a running sound cue. Before any early return below, so a
    // cue finishes even while the panel is blank.
    kk::sound::soundTick(millis());
    handleSerial();
    // The portal closes itself after 15 idle minutes; leave Screen::Setup set
    // until here so the hooks can keep their own messages.
    if (kk::screenBase(state().screen) == Screen::Setup) {
        state().setup.gSetupPortal.poll();
        if (kk::screenBase(state().screen) == Screen::Setup &&
            !state().setup.gSetupPortal.active()) {
            kk::screenReset(&state().screen);
            snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Setup timed out");
            state().display.gDirty = true;
        } else if (state().setup.gSetupPortal.takeRedrawRequest()) {
            state().display.gDirty = true;
        }
    }
    if (kk::screenBase(state().screen) != Screen::Setup) serviceAssetServer();
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
    // AUD-03: one decode/queue step per loop; it never waits for the speaker.
    audioTick();
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
    static tgc::KeyRepeat keyRepeat;
    while (tgc::board::nextKeyEvent(&e)) {
        state().ui.gLastInputAt = now;
        if (firstKey) {
            firstKey = false;
            if (state().ui.gDimmed) wakeScreen();
        }
        if (e.key == Key::None) {
            keyRepeat.cancel();
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
        if (e.down)
            keyRepeat.down(e, now);
        else
            keyRepeat.up(e);
        onKey(e);
    }

    // REV-10: a held Backspace deletes again after 400 ms, then every 120 ms.
    // Only an editable answer field consumes it; Enter and Esc never repeat.
    const Key repeated = keyRepeat.poll(now);
    if (repeated == Key::Backspace && answerFieldActive()) {
        state().ui.gLastInputAt = now;
        KeyEvent synthetic;
        synthetic.key = Key::Backspace;
        synthetic.down = true;
        onKey(synthetic);
    }

    // Advance the automatic or manual sync by at most one request, then draw
    // the frame the keys selected.
    syncAdvance();

    if (state().display.gDirty && now - state().display.gLastDrawMs >= 16) {
        draw();
        state().display.gDirty = false;
        state().display.gLastDrawMs = now;
    }
    // REV-10 latency: a review's outbox file, journal record and cache snapshot
    // are written here, right after the frame that showed its feedback. This
    // runs before the sync step, the next key and the sleep check below.
    flushPendingReviewWrites();
    feedbackTraceEnd();
#if defined(TGC_BOARD_PAPERDIY)
    // E-paper: push the canvas when the settle/floor/busy policy says so.
    presentTick(now);
#endif
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
#if defined(TGC_BOARD_PAPERDIY)
        // E-paper: a minute tick that changes nothing on Home must not refresh
        // the panel. Redraw only when the numbers Home shows changed; the
        // battery and clock are read on the next real redraw.
        const uint32_t dueBefore = state().session.gDueReviews;
        const uint32_t lessonsBefore = state().session.gLessons;
        char nextBefore[sizeof state().session.gNextReview];
        std::memcpy(nextBefore, state().session.gNextReview, sizeof nextBefore);
        applySummary(effectiveNow());
        if (dueBefore != state().session.gDueReviews || lessonsBefore != state().session.gLessons ||
            std::strcmp(nextBefore, state().session.gNextReview) != 0)
            state().display.gDirty = true;
#else
        applySummary(effectiveNow());
        state().display.gDirty = true;
#endif
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

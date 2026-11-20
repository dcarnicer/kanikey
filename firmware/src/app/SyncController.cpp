// The sync flow: manual (modal) and automatic (background) sync jobs plus the
// request steps they share. Each syncAdvance() call performs at most one
// request, so loop() keeps handling keys and Home stays visible.
#include <Arduino.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <time.h>

#include <algorithm>
#include <cstring>

#include "app/AboutScreen.h"
#include "app/App.h"
#include "app/AudioController.h"
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
#include "wk/SyncSteps.h"
#include "wk/Time.h"
#include "ui/Theme.h"

namespace kk::app {
namespace {

using tgc::Key;
using tgc::KeyEvent;
using kk::Modal;
using kk::Screen;
using namespace kk::ui;

bool runCriticalItems();
void resetSyncScreen();
void syncRowText(int row, const char* text);
void syncRowBegin(int row, const char* text);
void syncRowDone(int row, const char* text);
void syncRowWarn(int row, const char* text);
void syncRowFail(int row, const char* text);
void finishSyncCancelled();

SyncJobState& job() { return state().sync.gSyncJob; }

// One TLS connection for the whole sync job (SPEED-05). It is opened just
// before a step runs and closed whenever the job pauses or ends, so Settings
// and other screens never sit on a live socket or on a released canvas.
bool gSyncSessionOpen = false;
void syncSessionBegin() {
    if (gSyncSessionOpen) return;
    kk::wkSessionBegin();
    gSyncSessionOpen = true;
}
void syncSessionClose() {
    if (!gSyncSessionOpen) return;
    gSyncSessionOpen = false;
    kk::wkSessionEnd();
}

bool runCriticalItems() {
    const uint8_t kMaxPercent = static_cast<uint8_t>(kk::settingsCriticalPercent());
    const uint16_t kMinAnswers = static_cast<uint16_t>(kk::settingsCriticalMinAnswers());
    const String token = kk::DevToken::load();
    kk::CriticalStatistics stats = kk::fetchCriticalStatistics(token, kMaxPercent);
    if (!stats.ok) {
        Serial.printf("[critical] statistics unavailable: %s\n", stats.error.c_str());
        return false;
    }
    std::vector<uint32_t> ids;
    ids.reserve(stats.statistics.size());
    for (const kk::ReviewStatistic& stat : stats.statistics)
        if (stat.totalAnswers() >= kMinAnswers && !stat.hidden)
            ids.push_back(stat.subjectId);

    std::vector<kk::Assignment> assignments;
    if (!ids.empty()) {
        kk::AssignmentBatch batch = kk::fetchAssignmentsForSubjects(token, ids, 30);
        if (!batch.ok) {
            Serial.printf("[critical] assignments unavailable: %s\n", batch.error.c_str());
            return false;
        }
        assignments = std::move(batch.assignments);
    }
    std::vector<kk::CriticalItem> items = kk::selectCriticalItems(
        stats.statistics, assignments, kMaxPercent, kMinAnswers);
    if (!kk::criticalSave(currentAccountKey(), items)) {
        Serial.println("[critical] could not save the list");
        return false;
    }
    state().study.gCritical = std::move(items);
    state().study.gCriticalLoaded = true;
    Serial.printf("[critical] %u items\n", static_cast<unsigned>(state().study.gCritical.size()));
    refreshPracticePool();
    return true;
}

void resetSyncScreen() {
    static const char* kLabels[kSyncRowCount] = {"Wi-Fi", "Outbox", "Account", "Summary",
                                                 "Level", "SRS",    "Critical"};
    for (int i = 0; i < kSyncRowCount; ++i) {
        state().sync.gSyncRows[i].label = kLabels[i];
        state().sync.gSyncRows[i].value[0] = '\0';
        state().sync.gSyncRows[i].state = SyncRowState::Pending;
    }
    state().sync.gSyncCompleted = 0;
    state().sync.gSyncFinished = false;
    state().sync.gSyncCancelRequested = false;
    kk::screenReset(&state().screen);
    state().screen.screen = Screen::Sync;
}

void syncRowText(int row, const char* text) {
    if (row < 0 || row >= kSyncRowCount) return;
    snprintf(state().sync.gSyncRows[row].value, sizeof state().sync.gSyncRows[row].value, "%s", text ? text : "");
}

void syncRowBegin(int row, const char* text) {
    if (row < 0 || row >= kSyncRowCount) return;
    syncRowText(row, text);
    state().sync.gSyncRows[row].state = SyncRowState::Active;
    drawSync();
}

void syncRowDone(int row, const char* text) {
    if (row < 0 || row >= kSyncRowCount) return;
    syncRowText(row, text);
    state().sync.gSyncRows[row].state = SyncRowState::Done;
    if (state().sync.gSyncCompleted < kSyncRowCount) ++state().sync.gSyncCompleted;
    drawSync();
}

void syncRowWarn(int row, const char* text) {
    if (row < 0 || row >= kSyncRowCount) return;
    syncRowText(row, text);
    state().sync.gSyncRows[row].state = SyncRowState::Warn;
    if (state().sync.gSyncCompleted < kSyncRowCount) ++state().sync.gSyncCompleted;
    drawSync();
}

void syncRowFail(int row, const char* text) {
    if (row < 0 || row >= kSyncRowCount) return;
    syncRowText(row, text);
    state().sync.gSyncRows[row].state = SyncRowState::Failed;
    state().sync.gSyncFinished = true;
    state().sync.gSyncing = false;
    state().ui.gLastInputAt = millis();
    state().display.gDirty = true;
    Serial.printf("[sync] %s failed: %s\n", state().sync.gSyncRows[row].label, state().ui.gNetLine);
    drawSync();
}

void finishSyncCancelled() {
    syncSessionClose();
    state().sync.gSyncFinished = true;
    state().sync.gSyncing = false;
    state().sync.gSyncAttempted = false;
    state().sync.gSyncIssue = SyncIssue::None;
    state().sync.gOutboxAuthIssue = false;
    state().sync.gSyncJob = SyncJobState{};
    snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Sync cancelled");
    Serial.println("[sync] cancelled");
    kk::screenReset(&state().screen);
    state().screen.screen = state().session.gHaveSummary ? Screen::Home : Screen::Boot;
    state().ui.gLastInputAt = millis();
    state().display.gDirty = true;
}

// True while another screen owns the display; the automatic job waits and
// resumes when Home returns (SPEED-03). The lesson start confirmation and the
// batch quiz are deliberately not blockers, as they were not in the boolean
// predicate this replaces.
bool syncPausedByOtherScreen() {
    if (kk::screenTop(state().screen) != Modal::None) return true;
    if (state().ui.gNoticeActive) return true;
    if (state().stats.gStatsActive || state().study.gStudyMenuActive ||
        state().study.gCriticalListActive)
        return true;
    switch (kk::screenBase(state().screen)) {
        case Screen::Setup:
        case Screen::Outbox:
        case Screen::Firmware:
        case Screen::Review:
        case Screen::ReviewSummary:
        case Screen::LessonEmpty:
        case Screen::LessonCard:
            return true;
        case Screen::LessonList:
            return !state().lesson.startConfirm;
        case Screen::LessonQuiz:
            return !state().lesson.quizBatch;
        default:
            return false;
    }
}

// The Home header counts Wifi..Prefetch, including the SNTP wait.
void finishSyncJob(bool openReview) {
    syncSessionClose();
    SyncJobState& j = job();
    const bool full = j.kind == SyncKind::Full;
    const bool modal = j.modal;
    j.active = false;
    j.step = SyncStep::Done;
    state().sync.gSyncing = false;
    state().sync.gSyncFinished = true;
    if (modal) {
        // The manual sync screen is a base screen; a successful job returns to
        // Home (or Boot when there is no summary to show).
        kk::screenReset(&state().screen);
        state().screen.screen = state().session.gHaveSummary ? Screen::Home : Screen::Boot;
    }
    // A full sync that reached the end without an auth/rate gate is the new
    // baseline for the "Full sync every" interval.
    if (full && !kk::wkAuthBlocked() && kk::wkRateLimitWaitSeconds() == 0) {
        state().cache.gLastFullAt = effectiveNow();
        saveCacheSnapshot();
    }
    // An empty outbox cannot carry a review auth issue; an interrupted job may
    // not have reached finishOutboxStep().
    if (kk::outboxPendingCount(state().outbox.gOutbox) == 0) clearOutboxAuthIssue();
    applyDeferredAuthIssue();
    Serial.println("[sync] complete");
    state().ui.gLastInputAt = millis();
    if (!openReview) firmwareAfterSync();
    state().display.gDirty = true;
    if (openReview) startReview();
}

void failSyncJob(int row) {
    syncSessionClose();
    SyncJobState& j = job();
    const bool modal = j.modal;
    j.active = false;
    j.step = SyncStep::Failed;
    state().sync.gSyncing = false;
    state().sync.gSyncFinished = true;
    if (modal) {
        syncRowFail(row, state().ui.gNetLine);
    } else {
        if (state().sync.gSyncIssue == SyncIssue::Network) state().cache.gOffline = true;
        Serial.printf("[sync] stopped: %s\n", state().ui.gNetLine);
        state().display.gDirty = true;
    }
    state().ui.gLastInputAt = millis();
}

void formatOutboxCounts(char* out, size_t n) {
    const size_t pending = kk::outboxPendingCount(state().outbox.gOutbox);
    const size_t attention = kk::outboxAttentionCount(state().outbox.gOutbox);
    if (attention && pending)
        snprintf(out, n, "%u attention, %u pending", static_cast<unsigned>(attention),
                 static_cast<unsigned>(pending));
    else if (attention)
        snprintf(out, n, "%u need attention", static_cast<unsigned>(attention));
    else if (pending)
        snprintf(out, n, "%u pending", static_cast<unsigned>(pending));
    else
        snprintf(out, n, "none");
}

void finishOutboxStep() {
    char outbox[40];
    formatOutboxCounts(outbox, sizeof outbox);
    if (job().modal) {
        if (kk::outboxAttentionCount(state().outbox.gOutbox) ||
            kk::outboxPendingCount(state().outbox.gOutbox))
            syncRowWarn(SyncOutbox, outbox);
        else
            syncRowDone(SyncOutbox, outbox);
    }
    if (kk::outboxPendingCount(state().outbox.gOutbox) == 0) clearOutboxAuthIssue();
}

#ifdef KANIKEY_DEV
const char* syncStepName(SyncStep step) {
    switch (step) {
        case SyncStep::Idle: return "Idle";
        case SyncStep::Wifi: return "Wifi";
        case SyncStep::Time: return "Time";
        case SyncStep::Outbox: return "Outbox";
        case SyncStep::Account: return "Account";
        case SyncStep::Summary: return "Summary";
        case SyncStep::Level: return "Level";
        case SyncStep::Srs: return "Srs";
        case SyncStep::Critical: return "Critical";
        case SyncStep::Prefetch: return "Prefetch";
        case SyncStep::Done: return "Done";
        case SyncStep::Failed: return "Failed";
    }
    return "?";
}
#endif

// Moves to the next step in the plan, applying the optional-step rules:
// Account only when /user is due, Prefetch only for an automatic Full sync
// that passes SPEED-04's predicate, and no Prefetch on the manual screen.
void jobNextStep() {
    SyncJobState& j = job();
    if (!j.active) return;
    SyncStep next = kk::nextStep(j.kind, j.step, j.interrupted);
    if (next == SyncStep::Account && j.kind == SyncKind::Quick) {
        // The SNTP step has run by now: decide /user freshness with the best
        // clock available instead of the one at boot.
        const kk::SyncTimes times{static_cast<uint32_t>(time(nullptr)),
                                  state().cache.gLastQuickAt, state().cache.gLastFullAt,
                                  state().cache.gLastUserAt};
        j.needsUser = kk::quickNeedsUser(times, j.coldBoot);
    }
    if (next == SyncStep::Account && !j.needsUser)
        next = kk::nextStep(j.kind, next, false);
    if (next == SyncStep::Prefetch) {
        const bool want =
            !j.modal && kk::settingsPrefetchReviews() &&
            kk::shouldPrefetch(state().session.gDueReviews, state().cache.gCachedBatchCount,
                               static_cast<size_t>(kk::settingsReviewBatchSize()),
                               lowBatteryNow());
        if (!want) next = kk::nextStep(j.kind, next, false);
    }
    if (next == SyncStep::Done) {
        finishSyncJob(false);
        return;
    }
#ifdef KANIKEY_DEV
    Serial.printf("[sync] step %s -> %s (sub 0)\n", syncStepName(j.step), syncStepName(next));
#endif
    j.step = next;
    j.sub = 0;
    state().display.gDirty = true;
}

void syncWifiStep() {
    SyncJobState& j = job();
    if (!kk::DevWifi::connected()) {
        const String ssid = kk::DevWifi::savedSsid();
        if (j.modal) syncRowBegin(SyncWifi, ssid.c_str());
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "NET: joining %s", ssid.c_str());
        if (!kk::DevWifi::connect(30000)) {
            state().sync.gSyncIssue = SyncIssue::Network;
            snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "NET: join failed");
            failSyncJob(SyncWifi);
            return;
        }
    }
    snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "NET: %s", kk::DevWifi::ip().c_str());
    if (j.modal) syncRowDone(SyncWifi, kk::DevWifi::ip().c_str());
    jobNextStep();
}

// Starts the SNTP client once per boot through the ESP-IDF wrappers. They post
// to the lwIP task instead of taking the core lock on the caller, so loop()
// cannot stall here. The old Arduino configTzTime() called lwIP directly from
// the sync step and blocked the non-modal job before it could record any
// progress (stuck at SyncStep::Time, sub 0).
bool gSntpStarted = false;
void startSntpOnce() {
    if (gSntpStarted) return;
    gSntpStarted = true;
    const String rule = kk::settingsTimeZoneRule();
    setenv("TZ", rule.c_str(), 1);
    tzset();
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.cloudflare.com");
    esp_sntp_init();
    Serial.println("[time] SNTP started");
}

// Time is a bounded wait, never a blocking SNTP setup (SPEED-03). A trusted
// clock (the board RTC keeps epoch time) advances on the first pass.
void syncTimeStep() {
    SyncJobState& j = job();
    if (j.sub == 0) {
        j.timeStartedAt = millis();
        j.sub = 1;
        if (time(nullptr) < 1700000000) startSntpOnce();
    }
    const uint32_t utc = static_cast<uint32_t>(time(nullptr));
    const uint32_t waited = millis() - j.timeStartedAt;
    if (!kk::syncTimeReady(utc, waited, 5000)) return;
    Serial.printf("[time] %s: %u\n", utc >= 1700000000 ? "synced" : "not synced",
                  static_cast<unsigned>(utc));
    jobNextStep();
}

void syncOutboxStep() {
    SyncJobState& j = job();
    if (j.sub == 0) {
        char outbox[40];
        formatOutboxCounts(outbox, sizeof outbox);
        if (j.modal) syncRowBegin(SyncOutbox, outbox);
        j.outboxLastSeq = 0;
        j.sub = 1;
        return;
    }
    uint32_t seq = 0;
    for (const kk::OutboxItem& item : state().outbox.gOutbox) {
        if (item.seq <= j.outboxLastSeq) continue;
        if (item.state == kk::OutboxState::Rejected || item.state == kk::OutboxState::Stuck)
            continue;
        seq = item.seq;
        break;
    }
    if (!seq) {
        finishOutboxStep();
        jobNextStep();
        return;
    }
    j.outboxLastSeq = seq;
    sendQueuedReview(seq);
    const kk::OutboxItem* item = kk::outboxFind(state().outbox.gOutbox, seq);
    if (item && item->state != kk::OutboxState::Rejected &&
        item->state != kk::OutboxState::Stuck) {
        // The send failed on a retryable item. Like flushOutbox(), stop the
        // outbox part here and let the rest of the sync continue.
        finishOutboxStep();
        jobNextStep();
    }
}

void syncAccountStep() {
    SyncJobState& j = job();
    if (j.sub == 0) {
        if (j.modal) syncRowBegin(SyncAccount, "checking");
        j.sub = 1;
        return;
    }
    runProbe();
    if (state().sync.gSyncIssue != SyncIssue::None) {
        failSyncJob(SyncAccount);
        return;
    }
    if (j.modal) {
        char account[40];
        snprintf(account, sizeof account, "%s L%d", state().session.gUser.username.c_str(),
                 state().session.gUser.level);
        syncRowDone(SyncAccount, account);
    }
    jobNextStep();
}

void syncSummaryStep() {
    SyncJobState& j = job();
    if (j.sub == 0) {
        if (j.modal) syncRowBegin(SyncSummary, "fetching");
        j.sub = 1;
    }
    runSummary();
    if (state().sync.gSyncIssue != SyncIssue::None) {
        failSyncJob(SyncSummary);
        return;
    }
    if (j.modal) {
        char summary[40];
        snprintf(summary, sizeof summary, "%u due, %u lessons",
                 static_cast<unsigned>(state().session.gDueReviews),
                 static_cast<unsigned>(state().session.gLessons));
        syncRowDone(SyncSummary, summary);
    }
    jobNextStep();
}

// Level is three requests: the progression, then the two kanji counts.
void syncLevelStep() {
    SyncJobState& j = job();
    if (!state().session.gHaveUser) {
        if (j.modal) syncRowWarn(SyncLevel, "unavailable");
        jobNextStep();
        return;
    }
    const String token = kk::DevToken::load();
    if (j.sub == 0) {
        if (j.modal) syncRowBegin(SyncLevel, "fetching");
        const kk::LevelProgress progress =
            kk::fetchLevelProgressions(token, state().session.gUser.level);
        if (!progress.valid) {
            if (kk::wkAuthBlocked())
                setSyncIssue(401, true, 0, "Level");
            else if (kk::wkRateLimitWaitSeconds() > 0)
                setSyncIssue(429, true, 0, "Level");
            if (j.modal)
                syncRowWarn(SyncLevel, kk::wkAuthBlocked()   ? "token rejected"
                                    : kk::wkRateLimitWaitSeconds() > 0 ? "rate limited"
                                                                       : "unavailable");
            jobNextStep();
            return;
        }
        state().session.gLevelProgress = progress;
        saveCacheSnapshot();
        j.sub = 1;
        return;
    }
    if (j.sub == 1) {
        const bool ok = kk::fetchLevelKanjiCount(token, state().session.gLevelProgress.level,
                                                 false, &j.levelTotal);
        j.sub = 2;
        if (!ok) {
            Serial.println("[level] kanji counts unavailable; keeping timestamps only");
            if (j.modal) syncRowWarn(SyncLevel, "unavailable");
            jobNextStep();
        }
        return;
    }
    const bool ok = kk::fetchLevelKanjiCount(token, state().session.gLevelProgress.level, true,
                                             &j.levelPassed);
    if (ok) {
        state().session.gLevelProgress.kanjiTotal = j.levelTotal;
        state().session.gLevelProgress.kanjiPassed = j.levelPassed;
        saveCacheSnapshot();
    } else {
        Serial.println("[level] kanji counts unavailable; keeping timestamps only");
    }
    if (j.modal) {
        if (ok) {
            char level[40];
            formatLevelProgressLine(level, sizeof level);
            syncRowDone(SyncLevel, level);
        } else {
            syncRowWarn(SyncLevel, "unavailable");
        }
    }
    jobNextStep();
}

// Srs is five requests, one bucket each.
void syncSrsStep() {
    SyncJobState& j = job();
    if (j.sub == 0) {
        if (j.modal) syncRowBegin(SyncSrs, "fetching");
        j.sub = 1;
        return;
    }
    const uint8_t bucket = static_cast<uint8_t>(j.sub - 1);
    uint32_t value = 0;
    if (!kk::fetchSrsBucket(kk::DevToken::load(), bucket, &value)) {
        if (kk::wkAuthBlocked())
            setSyncIssue(401, true, 0, "SRS");
        else if (kk::wkRateLimitWaitSeconds() > 0)
            setSyncIssue(429, true, 0, "SRS");
        if (j.modal)
            syncRowWarn(SyncSrs, kk::wkAuthBlocked()   ? "token rejected"
                                : kk::wkRateLimitWaitSeconds() > 0 ? "rate limited"
                                                                   : "unavailable");
        jobNextStep();
        return;
    }
    j.srs[bucket] = value;
    if (j.sub < 5) {
        ++j.sub;
        return;
    }
    kk::SrsCounts counts;
    counts.apprentice = j.srs[0];
    counts.guru = j.srs[1];
    counts.master = j.srs[2];
    counts.enlightened = j.srs[3];
    counts.burned = j.srs[4];
    counts.valid = true;
    state().session.gSrs = counts;
    saveCacheSnapshot();
    Serial.printf("[srs] A %u, G %u, M %u, E %u, B %u\n",
                  static_cast<unsigned>(counts.apprentice), static_cast<unsigned>(counts.guru),
                  static_cast<unsigned>(counts.master),
                  static_cast<unsigned>(counts.enlightened),
                  static_cast<unsigned>(counts.burned));
    if (j.modal) {
        char srs[40];
        snprintf(srs, sizeof srs, "A%u G%u M%u E%u B%u", static_cast<unsigned>(counts.apprentice),
                 static_cast<unsigned>(counts.guru), static_cast<unsigned>(counts.master),
                 static_cast<unsigned>(counts.enlightened),
                 static_cast<unsigned>(counts.burned));
        syncRowDone(SyncSrs, srs);
    }
    jobNextStep();
}

void syncCriticalStep() {
    SyncJobState& j = job();
    if (j.sub == 0) {
        if (j.modal) syncRowBegin(SyncCritical, "fetching");
        j.sub = 1;
    }
    const bool ok = runCriticalItems();
    if (kk::wkAuthBlocked())
        setSyncIssue(401, true, 0, "Critical");
    else if (kk::wkRateLimitWaitSeconds() > 0)
        setSyncIssue(429, true, 0, "Critical");
    if (j.modal) {
        if (kk::wkAuthBlocked())
            syncRowWarn(SyncCritical, "token rejected");
        else if (kk::wkRateLimitWaitSeconds() > 0)
            syncRowWarn(SyncCritical, "rate limited");
        else if (ok) {
            char critical[40];
            if (state().study.gCritical.empty())
                snprintf(critical, sizeof critical, "none");
            else
                snprintf(critical, sizeof critical, "%u critical items",
                         static_cast<unsigned>(state().study.gCritical.size()));
            syncRowDone(SyncCritical, critical);
        } else {
            syncRowWarn(SyncCritical, "unavailable");
        }
    }
    jobNextStep();
}

void syncPrefetchStep() {
    SyncJobState& j = job();
    if (j.sub != 0) {
        jobNextStep();
        return;
    }
    j.sub = 1;
    // No loading screen: Home stays up and the header keeps the last step.
    Serial.println("[review] prefetching the review batch");
    if (runBatch()) {
        // The assignments and subjects are on disk; keep them out of RAM on
        // Home (SPEED-04, acceptance: heap back to the pre-sync level).
        evictCachedBatch(true);
    }
    jobNextStep();
}

// Shared entry point: manual (modal) and automatic (background). A manual Sync
// now always runs Full, whatever Y1 says.
void startSyncJob(SyncKind kind, bool modal, bool coldBoot) {
    if (kind == SyncKind::Skip) return;
    if (state().storage.gFsFormatPrompt) return;  // wait for the storage choice
    if (!state().storage.gSdOk || !state().storage.gFontPack.ready() || !kk::DevToken::has() || !kk::DevWifi::hasSaved()) {
        state().display.gDirty = true;
        return;
    }
    // A manual Sync now replaces a paused automatic job.
    syncSessionClose();
    SyncJobState& j = job();
    j = SyncJobState{};
    j.kind = kind;
    j.modal = modal;
    j.coldBoot = coldBoot;
    j.active = true;
    j.step = SyncStep::Wifi;
    j.startedAt = millis();
    const kk::SyncTimes times{static_cast<uint32_t>(time(nullptr)),
                              state().cache.gLastQuickAt, state().cache.gLastFullAt,
                              state().cache.gLastUserAt};
    j.needsUser = kind == SyncKind::Full || kk::quickNeedsUser(times, coldBoot);
    state().sync.gSyncing = true;
    state().sync.gSyncAttempted = true;
    state().sync.gSyncIssue = SyncIssue::None;
    state().sync.gOutboxAuthIssue = false;
    state().sync.gSyncCancelRequested = false;
    state().sync.gSyncFinished = false;
    if (modal) {
        resetSyncScreen();
        drawSync();
    } else {
        state().display.gDirty = true;
    }
}

}  // namespace

void setSyncIssue(int httpCode, bool connected, int tlsError, const char* what) {
    state().sync.gOutboxAuthIssue = false;
    if (!connected) {
        state().sync.gSyncIssue = SyncIssue::Network;
        if (tlsError)
            snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "%s: network/TLS %d", what, tlsError);
        else
            snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "%s: network unreachable", what);
        return;
    }
    if (httpCode == 401) {
        state().sync.gSyncIssue = SyncIssue::Auth;
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "%s: token rejected (401)", what);
    } else if (httpCode == 403) {
        state().sync.gSyncIssue = SyncIssue::Forbidden;
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "%s: missing permissions (403)", what);
    } else if (httpCode == 429) {
        state().sync.gSyncIssue = SyncIssue::RateLimit;
        const uint32_t wait = kk::wkRateLimitWaitSeconds();
        if (wait)
            snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "%s: rate limited, %us", what,
                     static_cast<unsigned>(wait));
        else
            snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "%s: rate limited (429)", what);
    } else if (httpCode >= 500) {
        state().sync.gSyncIssue = SyncIssue::Server;
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "%s: WaniKani error (%d)", what, httpCode);
    } else if (httpCode == 200) {
        state().sync.gSyncIssue = SyncIssue::Other;
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "%s: invalid response", what);
    } else if (httpCode > 0) {
        state().sync.gSyncIssue = SyncIssue::Other;
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "%s: HTTP %d", what, httpCode);
    } else {
        state().sync.gSyncIssue = SyncIssue::Other;
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "%s: invalid response", what);
    }
}

void applyDeferredAuthIssue() {
    if (state().sync.gDeferredAuthIssue == SyncIssue::Auth) {
        setSyncIssue(401, true, 0, "Reviews");
        state().sync.gOutboxAuthIssue = true;
    } else if (state().sync.gDeferredAuthIssue == SyncIssue::Forbidden &&
               state().sync.gSyncIssue != SyncIssue::Auth) {
        setSyncIssue(403, true, 0, "Reviews");
        state().sync.gOutboxAuthIssue = true;
    }
}

void clearOutboxAuthIssue() {
    state().sync.gDeferredAuthIssue = SyncIssue::None;
    if (state().sync.gOutboxAuthIssue &&
        (state().sync.gSyncIssue == SyncIssue::Auth || state().sync.gSyncIssue == SyncIssue::Forbidden))
        state().sync.gSyncIssue = SyncIssue::None;
    state().sync.gOutboxAuthIssue = false;
}

void runProbe() {
    const String token = kk::DevToken::load();
    const uint8_t previousLevel = state().session.gHaveUser ? state().session.gUser.level : 0;
    snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "API: probing...");
    state().display.gDirty = true;
    const kk::WkProbeResult r = kk::probeWaniKaniUser(token);
    if (r.parsed) {
        state().session.gUser = r.user;
        state().session.gHaveUser = true;
        state().cache.gLastUserAt = effectiveNow();
        if (previousLevel && r.user.level > previousLevel) {
            state().session.gLevelUpFrom = previousLevel;
            state().session.gLevelUpTo = static_cast<uint8_t>(r.user.level);
            state().session.gLevelUpAt = effectiveNow();
            state().session.gLevelUpNotice = true;
        } else if (!previousLevel || r.user.level < previousLevel) {
            clearLevelUpNotice();
        }
        state().sync.gSyncIssue = SyncIssue::None;
        state().sync.gOutboxAuthIssue = false;
    } else {
        setSyncIssue(r.httpCode, r.connected, r.tlsError, "API");
    }
    if (r.connected) {
        if (r.httpCode == 200 && r.parsed)
            snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "API: 200 %s L%d", r.user.username.c_str(),
                     r.user.level);
        else if (!r.parsed)
            Serial.printf("[api] HTTP %d, parsed=%d\n", r.httpCode, r.parsed ? 1 : 0);
    }
    state().display.gDirty = true;
}

void runSummary() {
    const String token = kk::DevToken::load();
    const kk::SummaryProbeResult r = kk::probeWaniKaniSummary(token);
    const uint32_t now = effectiveNow();
    if (r.parsed) {
        state().session.gSummary = r.summary;
        state().session.gHaveSummary = true;
        state().sync.gSyncIssue = SyncIssue::None;
        state().sync.gOutboxAuthIssue = false;
        state().session.gSyncedAt = now >= 1700000000u ? now : 0;
        state().cache.gLastQuickAt = now;
        if (state().session.gSyncedAt) localHm(state().session.gSyncedAt, state().session.gSyncClock, sizeof state().session.gSyncClock);
        pruneBatchToSummary(now);
        applySummary(now);
        state().cache.gOffline = false;
        // A background sync may reveal Home while it is still running; the
        // modal sync screen keeps its own base and finishSyncJob() sets Home.
        const Screen base = kk::screenBase(state().screen);
        if (base == Screen::Boot || base == Screen::Home)
            state().screen.screen = Screen::Home;
        saveCacheSnapshot();
    }
    if (r.parsed)
        Serial.printf("[summary] due %u of %u reviews, %u lessons, next %s\n",
                      static_cast<unsigned>(r.summary.dueReviews(now)),
                      static_cast<unsigned>(r.summary.totalReviews()),
                      static_cast<unsigned>(r.summary.totalLessons()),
                      r.summary.nextReviewsAt.c_str());
    if (r.parsed)
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Summary: %ur %ul",
                 static_cast<unsigned>(r.summary.totalReviews()),
                 static_cast<unsigned>(r.summary.totalLessons()));
    else
        setSyncIssue(r.httpCode, r.connected, r.tlsError, "Summary");
    state().display.gDirty = true;
}

void formatLevelProgressLine(char* out, size_t n) {
    if (!out || n == 0) return;
    out[0] = 0;
    if (!state().session.gLevelProgress.valid) return;
    char date[16];
    localDate(state().session.gLevelProgress.unlockedAt ? state().session.gLevelProgress.unlockedAt : state().session.gLevelProgress.startedAt,
              date, sizeof date);
    const uint8_t percent =
        kk::levelKanjiPercent(state().session.gLevelProgress.kanjiPassed, state().session.gLevelProgress.kanjiTotal);
    if (state().session.gLevelProgress.kanjiTotal) {
        if (date[0])
            snprintf(out, n, "L%u %u%% since %s", static_cast<unsigned>(state().session.gLevelProgress.level),
                     static_cast<unsigned>(percent), date);
        else
            snprintf(out, n, "L%u %u%%", static_cast<unsigned>(state().session.gLevelProgress.level),
                     static_cast<unsigned>(percent));
    } else if (date[0]) {
        snprintf(out, n, "L%u since %s", static_cast<unsigned>(state().session.gLevelProgress.level), date);
    } else {
        snprintf(out, n, "Level %u", static_cast<unsigned>(state().session.gLevelProgress.level));
    }
}

namespace {

// Today's account preference: a fresh sample per session for Shuffled, else
// the summary's oldest-first order (lower_levels_first is applied by build()).
std::vector<uint32_t> accountOrderIds(const std::vector<uint32_t>& due, size_t limit) {
    if (state().session.gHaveUser &&
        state().session.gUser.reviewsOrder == kk::ReviewOrder::Shuffled)
        return kk::sampleIds(due, limit, esp_random());
    std::vector<uint32_t> ids;
    ids.assign(due.begin(), due.begin() + std::min(due.size(), limit));
    return ids;
}

// subject_id -> level from the ARCH-03 subject catalog (one 26-byte read per
// subject, no SD write), with the in-memory review batch as a fallback.
std::vector<kk::ReviewLevel> dueSubjectLevels(
    const std::vector<kk::Assignment>& assignments) {
    std::vector<kk::ReviewLevel> levels;
    levels.reserve(assignments.size());
    const bool catalogOk = state().storage.gSdOk && kk::catalogOpen(SD, currentAccountKey());
    size_t known = 0;
    for (const kk::Assignment& a : assignments) {
        uint8_t level = 0;
        if (catalogOk) kk::catalogLevel(SD, a.subjectId, &level);
        if (!level) {
            for (const kk::Subject& s : state().cache.gBatch.subjects) {
                if (s.id == a.subjectId) {
                    level = s.level;
                    break;
                }
            }
        }
        if (level) ++known;
        levels.push_back({a.subjectId, level});
    }
    Serial.printf("[review] levels known %u/%u\n", static_cast<unsigned>(known),
                  static_cast<unsigned>(assignments.size()));
    return levels;
}

// One keep-alive session across the due-metadata fetch and the batch fetch
// (SPEED-05). Account passes false so its request shape stays unchanged.
struct OrderFetchSession {
    explicit OrderFetchSession(bool enabled) : enabled_(enabled) {
        if (enabled_) kk::wkSessionBegin();
    }
    ~OrderFetchSession() {
        if (enabled_) kk::wkSessionEnd();
    }
    OrderFetchSession(const OrderFetchSession&) = delete;
    OrderFetchSession& operator=(const OrderFetchSession&) = delete;

private:
    bool enabled_;
};

}  // namespace

bool runBatch() {
    if (!state().session.gHaveSummary) {
        Serial.println("[review] no summary yet");
        return false;
    }
    // An automatic sync may have skipped the Wi-Fi join with a recent cache;
    // the first network action (Enter with no cached batch) joins on demand.
    if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Batch: no Wi-Fi");
        Serial.println("[review] no Wi-Fi");
        return false;
    }
    const size_t limit = static_cast<size_t>(kk::settingsReviewBatchSize());
    const uint32_t now = effectiveNow();
    const std::vector<uint32_t> due = state().session.gSummary.dueSubjectIds(now, 1000);
    // The summary keeps up to 1000 ids per bucket, so the batch can be chosen
    // from every due item. Account keeps today's summary/sample order; every
    // other R2 strategy fetches the due assignments' metadata first and orders
    // the frozen due set before the limit is applied.
    const kk::ReviewOrderStrategy strategy = reviewOrderStrategy();
    const bool typeOrder = reviewTypeOrderEnabled();
    const bool explicitOrder = strategy != kk::ReviewOrderStrategy::Account;
    std::vector<uint32_t> ids;
    const bool hadBatch = !state().cache.gBatch.assignments.empty();
    kk::ReviewBatch batch;
    {
        OrderFetchSession orderSession(explicitOrder);
        state().ui.gFreeCanvasForRequest = true;
        if (!explicitOrder) {
            ids = accountOrderIds(due, limit);
            Serial.printf("[review] %u due, %u chosen (account %s)\n",
                          static_cast<unsigned>(due.size()), static_cast<unsigned>(ids.size()),
                          state().session.gHaveUser &&
                                  state().session.gUser.reviewsOrder ==
                                      kk::ReviewOrder::LowerLevelsFirst
                              ? "lower_levels_first"
                              : "shuffled");
        } else {
            const uint32_t heapBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            const uint32_t largestBefore = heap_caps_get_largest_free_block(
                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            Serial.printf("[review] order %s: due fetch for %u ids, heap %u free/%u largest\n",
                          kk::reviewOrderStrategyName(strategy),
                          static_cast<unsigned>(due.size()), static_cast<unsigned>(heapBefore),
                          static_cast<unsigned>(largestBefore));
            bool ordered = false;
            {
                kk::DueAssignments found =
                    kk::fetchDueAssignments(kk::DevToken::load(), due, now);
                // A locally completed review is still in the outbox until the
                // POST succeeds; never choose it again in this session.
                if (found.ok) {
                    found.assignments.erase(
                        std::remove_if(found.assignments.begin(), found.assignments.end(),
                                       [](const kk::Assignment& a) {
                                           return outboxHasAssignment(a.id);
                                       }),
                        found.assignments.end());
                }
                Serial.printf(
                    "[review] due fetch: %u requests (page %u), %u bytes, %u assignments\n",
                    static_cast<unsigned>(found.requests), static_cast<unsigned>(found.pageSize),
                    static_cast<unsigned>(found.bytes),
                    static_cast<unsigned>(found.assignments.size()));
                if (found.ok && !found.assignments.empty()) {
                    std::vector<kk::ReviewLevel> levels;
                    if (strategy == kk::ReviewOrderStrategy::LowestLevelFirst ||
                        strategy == kk::ReviewOrderStrategy::CurrentLevelFirst)
                        levels = dueSubjectLevels(found.assignments);
                    ids = kk::selectDueSubjectIds(found.assignments, levels, strategy,
                                                  currentReviewLevel(), typeOrder, now, limit,
                                                  esp_random());
                    ordered = !ids.empty();
                } else {
                    Serial.printf("[review] due fetch failed: %s\n",
                                  found.error.length()
                                      ? found.error.c_str()
                                      : "no due assignments outside the outbox");
                }
                Serial.printf("[review] due fetch heap %u -> %u free, %u -> %u largest\n",
                              static_cast<unsigned>(heapBefore),
                              static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                              static_cast<unsigned>(largestBefore),
                              static_cast<unsigned>(heap_caps_get_largest_free_block(
                                  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
            }
            // The metadata vector is gone before the batch's TLS request; only
            // the selected ids (at most the batch size) stay alive.
            Serial.printf("[review] due metadata released, heap %u free/%u largest\n",
                          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                          static_cast<unsigned>(heap_caps_get_largest_free_block(
                              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
            if (!ordered) {
                // A failed or empty due fetch must not block the session: start
                // with the account order instead.
                ids = accountOrderIds(due, limit);
                Serial.printf("[review] order fallback to account: %u chosen\n",
                              static_cast<unsigned>(ids.size()));
            } else {
                Serial.printf("[review] %u due, %u chosen by %s\n",
                              static_cast<unsigned>(due.size()),
                              static_cast<unsigned>(ids.size()),
                              kk::reviewOrderStrategyName(strategy));
            }
        }
        batch = kk::fetchReviewBatch(kk::DevToken::load(), ids, now, limit);
        state().ui.gFreeCanvasForRequest = false;
    }
    if (!batch.ok) {
        Serial.printf("[review] batch failed: %s\n", batch.error.c_str());
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Batch: %s", batch.error.c_str());
        // A failed prefetch must not throw away a good cached batch. Clear the
        // in-memory batch only when there was nothing to keep.
        if (!hadBatch) {
            state().cache.gBatch = kk::ReviewBatch{};
            state().cache.gBatch.httpCode = batch.httpCode;
        }
        state().display.gDirty = true;
        return false;
    }
    audioPrefetchBatch(batch.subjects);
    Serial.printf("[review] %u assignments, %u subjects\n",
                  static_cast<unsigned>(batch.assignments.size()),
                  static_cast<unsigned>(batch.subjects.size()));
    for (const kk::Assignment& a : batch.assignments) {
        const kk::Subject* s = nullptr;
        for (const kk::Subject& candidate : batch.subjects)
            if (candidate.id == a.subjectId) {
                s = &candidate;
                break;
            }
        if (!s) continue;
        std::string meaning;
        for (const kk::Meaning& m : s->meanings)
            if (m.accepted) {
                meaning = m.text;
                break;
            }
        if (meaning.empty() && !s->meanings.empty()) meaning = s->meanings[0].text;
        std::string reading;
        for (const kk::Reading& r : s->readings)
            if (r.accepted && r.primary) {
                reading = r.text;
                break;
            }
        if (reading.empty())
            for (const kk::Reading& r : s->readings)
                if (r.accepted) {
                    reading = r.text;
                    break;
                }
        Serial.printf("[review] sub %u %s m=%s r=%s\n", static_cast<unsigned>(s->id),
                      s->characters.c_str(), meaning.c_str(), reading.c_str());
    }
    snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Batch: %ua %us",
             static_cast<unsigned>(batch.assignments.size()),
             static_cast<unsigned>(batch.subjects.size()));
    state().cache.gBatch = std::move(batch);
    state().cache.gBatchEvicted = false;
    state().cache.gCachedBatchCount = state().cache.gBatch.assignments.size();
    saveCacheAll();
    state().display.gDirty = true;
    return true;
}

void syncScreenKey(const KeyEvent& e) {
    if (!e.down) return;
    if (!state().sync.gSyncFinished) {
        if (e.key == Key::Esc) state().sync.gSyncCancelRequested = true;
        return;
    }
    if (e.key == Key::Enter) {
        if (state().sync.gSyncIssue == SyncIssue::Auth || state().sync.gSyncIssue == SyncIssue::Forbidden) {
            kk::screenReset(&state().screen);
            startSetupPortal();
        } else {
            startSync();
        }
        return;
    }
    if (e.key == Key::Esc) {
        kk::screenReset(&state().screen);
        state().screen.screen = state().session.gHaveSummary ? Screen::Home : Screen::Boot;
        state().display.gDirty = true;
    }
}

void startSync() { startSyncJob(SyncKind::Full, true, false); }

void startSyncAuto(SyncKind kind, bool coldBoot) { startSyncJob(kind, false, coldBoot); }

void syncAdvance() {
    SyncJobState& j = job();
    if (!j.active) return;
    if (!kk::DevToken::has()) {
        // Signed out while the job waited behind Settings, or a manual job
        // lost its token: drop the job and any modal sync screen.
        syncSessionClose();
        j = SyncJobState{};
        state().sync.gSyncing = false;
        state().sync.gSyncFinished = true;
        kk::screenReset(&state().screen);
        state().screen.screen = state().session.gHaveSummary ? Screen::Home : Screen::Boot;
        state().display.gDirty = true;
        return;
    }
    if (syncPausedByOtherScreen()) {
        syncSessionClose();
        return;
    }
    if (j.modal) {
        if (kk::screenBase(state().screen) != Screen::Sync) {
            syncSessionClose();
            return;
        }
        if (state().sync.gSyncCancelRequested) {
            finishSyncCancelled();
            return;
        }
    }
    state().ui.gLastInputAt = millis();

    // Enter during an automatic sync: at this boundary try the cached batch
    // and start the review; if it does not load, the sync continues.
    if (!j.modal && j.startReviewPending) {
        j.startReviewPending = false;
        j.interrupted = false;
        if (loadCachedBatch()) {
            finishSyncJob(true);
            return;
        }
    }

    // One socket for the steps that follow; nested batch/outbox sessions join
    // this one instead of handshaking again.
    syncSessionBegin();

    switch (j.step) {
        case SyncStep::Wifi: syncWifiStep(); break;
        case SyncStep::Time: syncTimeStep(); break;
        case SyncStep::Outbox: syncOutboxStep(); break;
        case SyncStep::Account: syncAccountStep(); break;
        case SyncStep::Summary: syncSummaryStep(); break;
        case SyncStep::Level: syncLevelStep(); break;
        case SyncStep::Srs: syncSrsStep(); break;
        case SyncStep::Critical: syncCriticalStep(); break;
        case SyncStep::Prefetch: syncPrefetchStep(); break;
        case SyncStep::Idle:
        case SyncStep::Done:
        case SyncStep::Failed:
            syncSessionClose();
            j.active = false;
            break;
    }

    // A long Wi-Fi join or request must not let the panel blank mid-job: the
    // old blocking sync kept the device awake for the whole sync. The panel
    // may still be blanked by an idle timeout between jobs.
    if (j.active) state().ui.gLastInputAt = millis();
#ifdef KANIKEY_DEV
    static uint32_t lastJobLog = 0;
    if (j.active && millis() - lastJobLog >= 1000) {
        lastJobLog = millis();
        Serial.printf("[sync] job %s sub %u\n", syncStepName(j.step),
                      static_cast<unsigned>(j.sub));
    }
#endif
}

void drawSync() {
    if (!state().display.gDisp) return;

    SyncView view;
    view.batteryPercent = tgc::board::batteryPercent();
    view.finished = state().sync.gSyncFinished;
    view.completed = state().sync.gSyncCompleted;
    for (int i = 0; i < kSyncRowCount; ++i)
        view.rows[i] = {state().sync.gSyncRows[i].label, state().sync.gSyncRows[i].value, state().sync.gSyncRows[i].state};
    view.errorLine = state().ui.gNetLine;
    switch (state().sync.gSyncIssue) {
        case SyncIssue::None: view.issue = SyncIssueKind::None; break;
        case SyncIssue::RateLimit:
            view.issue = SyncIssueKind::RateLimit;
            view.rateLimitWaitSeconds = kk::wkRateLimitWaitSeconds();
            break;
        case SyncIssue::Auth: view.issue = SyncIssueKind::Auth; break;
        case SyncIssue::Forbidden: view.issue = SyncIssueKind::Forbidden; break;
        default: view.issue = SyncIssueKind::Other; break;
    }

    auto& d = target();
    d.startWrite();
    kk::app::drawSyncScreen(d, view);
    d.endWrite();
    present();
}

}  // namespace kk::app

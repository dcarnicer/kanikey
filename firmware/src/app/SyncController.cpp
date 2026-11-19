// The blocking sync flow and its screen state.
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

void runLevelProgress();
bool runSrsCounts();
bool runCriticalItems();
void resetSyncScreen();
void syncRowText(int row, const char* text);
void syncRowBegin(int row, const char* text);
void syncRowDone(int row, const char* text);
void syncRowWarn(int row, const char* text);
void syncRowFail(int row, const char* text);
// Pump the keyboard between blocking requests; Esc is the only action.
bool syncStopRequested();
void finishSyncCancelled();

void runLevelProgress() {
    if (!state().session.gHaveUser) return;
    const kk::LevelProgress p = kk::fetchLevelProgress(kk::DevToken::load(), state().session.gUser.level);
    if (!p.valid) return;
    state().session.gLevelProgress = p;
    saveCacheSnapshot();
    char date[16];
    localDate(state().session.gLevelProgress.unlockedAt ? state().session.gLevelProgress.unlockedAt : state().session.gLevelProgress.startedAt,
              date, sizeof date);
    Serial.printf("[level] %u %u%% since %s\n", static_cast<unsigned>(state().session.gLevelProgress.level),
                  static_cast<unsigned>(kk::levelKanjiPercent(state().session.gLevelProgress.kanjiPassed,
                                                               state().session.gLevelProgress.kanjiTotal)),
                  date);
}

bool runSrsCounts() {
    kk::SrsCounts counts;
    if (!kk::fetchSrsCounts(kk::DevToken::load(), &counts)) {
        Serial.println("[srs] distribution unavailable; keeping the cached counts");
        return false;
    }
    state().session.gSrs = counts;
    saveCacheSnapshot();
    return true;
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
    state().sync.gSyncScreen = true;
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

// Pump the keyboard between blocking requests; Esc is the only action.
bool syncStopRequested() {
    if (state().sync.gSyncCancelRequested) return true;
    tgc::board::update();
    KeyEvent e;
    while (tgc::board::nextKeyEvent(&e)) {
        state().ui.gLastInputAt = millis();
        updateModifier(e.x, e.y, e.down);
        if (e.down && e.key == Key::Esc) state().sync.gSyncCancelRequested = true;
    }
    return state().sync.gSyncCancelRequested;
}

void finishSyncCancelled() {
    state().sync.gSyncFinished = true;
    state().sync.gSyncScreen = false;
    state().sync.gSyncing = false;
    state().sync.gSyncAttempted = false;
    state().sync.gSyncIssue = SyncIssue::None;
    state().sync.gOutboxAuthIssue = false;
    snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Sync cancelled");
    Serial.println("[sync] cancelled");
    state().session.gShowHome = state().session.gHaveSummary;
    state().ui.gLastInputAt = millis();
    state().display.gDirty = true;
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
        if (state().session.gSyncedAt) localHm(state().session.gSyncedAt, state().session.gSyncClock, sizeof state().session.gSyncClock);
        pruneBatchToSummary(now);
        applySummary(now);
        state().cache.gOffline = false;
        state().session.gShowHome = true;
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

bool runBatch() {
    if (!state().session.gHaveSummary) {
        Serial.println("[review] no summary yet");
        return false;
    }
    const size_t limit = static_cast<size_t>(kk::settingsReviewBatchSize());
    const uint32_t now = effectiveNow();
    const std::vector<uint32_t> due = state().session.gSummary.dueSubjectIds(now, 1000);
    // The summary keeps up to 1000 ids per bucket, so the batch can be chosen
    // from every due item. lower_levels_first keeps the summary order; the
    // shuffled order takes a fresh sample per session.
    std::vector<uint32_t> ids;
    if (state().session.gHaveUser && state().session.gUser.reviewsOrder == kk::ReviewOrder::Shuffled) {
        ids = kk::sampleIds(due, limit, esp_random());
    } else {
        ids.assign(due.begin(), due.begin() + std::min(due.size(), limit));
    }
    Serial.printf("[review] %u due, %u chosen\n", static_cast<unsigned>(due.size()),
                  static_cast<unsigned>(ids.size()));
    state().ui.gFreeCanvasForRequest = true;
    kk::ReviewBatch batch = kk::fetchReviewBatch(kk::DevToken::load(), ids, now, limit);
    state().ui.gFreeCanvasForRequest = false;
    if (!batch.ok) {
        Serial.printf("[review] batch failed: %s\n", batch.error.c_str());
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Batch: %s", batch.error.c_str());
        state().cache.gBatch = kk::ReviewBatch{};
        state().cache.gBatch.httpCode = batch.httpCode;
        state().display.gDirty = true;
        return false;
    }
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
            state().sync.gSyncScreen = false;
            state().session.gShowHome = false;
            startSetupPortal();
        } else {
            startSync();
        }
        return;
    }
    if (e.key == Key::Esc) {
        state().sync.gSyncScreen = false;
        state().session.gShowHome = state().session.gHaveSummary;
        state().display.gDirty = true;
    }
}

void startSync() {
    if (state().storage.gFsFormatPrompt) return;  // wait for the storage choice
    if (!state().storage.gSdOk || !state().storage.gPack.ready() || !kk::DevToken::has() || !kk::DevWifi::hasSaved()) {
        state().sync.gSyncScreen = false;
        state().display.gDirty = true;
        return;
    }
    state().sync.gSyncing = true;
    state().sync.gSyncAttempted = true;
    state().sync.gSyncIssue = SyncIssue::None;
    state().sync.gOutboxAuthIssue = false;
    resetSyncScreen();
    drawSync();

    // Wi-Fi (the only step shown before the first API call).
    if (!kk::DevWifi::connected()) {
        const String ssid = kk::DevWifi::savedSsid();
        syncRowBegin(SyncWifi, ssid.c_str());
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "NET: joining %s", ssid.c_str());
        if (!kk::DevWifi::connect(30000)) {
            state().sync.gSyncIssue = SyncIssue::Network;
            snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "NET: join failed");
            syncRowFail(SyncWifi, "join failed");
            return;
        }
    } else {
        syncRowBegin(SyncWifi, kk::DevWifi::ip().c_str());
    }
    snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "NET: %s", kk::DevWifi::ip().c_str());
    syncRowDone(SyncWifi, kk::DevWifi::ip().c_str());
    if (syncStopRequested()) {
        finishSyncCancelled();
        return;
    }

    // configTzTime applies the POSIX rule and starts SNTP in one step. Plain
    // configTime() would overwrite the rule with UTC right after applying it.
    configTzTime(kk::settingsTimeZoneRule().c_str(), "pool.ntp.org", "time.cloudflare.com");
    const uint32_t ntpStart = millis();
    while (time(nullptr) < 1700000000 && millis() - ntpStart < 5000) delay(100);
    const uint32_t utc = static_cast<uint32_t>(time(nullptr));
    Serial.printf("[time] %s: %u\n", utc >= 1700000000 ? "synced" : "not synced",
                  static_cast<unsigned>(utc));

    // Outbox: keep going even if items remain pending.
    const size_t beforePending = kk::outboxPendingCount(state().outbox.gOutbox);
    const size_t beforeAttention = kk::outboxAttentionCount(state().outbox.gOutbox);
    char outbox[40];
    if (beforeAttention && beforePending)
        snprintf(outbox, sizeof outbox, "%u attention, %u pending",
                 static_cast<unsigned>(beforeAttention), static_cast<unsigned>(beforePending));
    else if (beforeAttention)
        snprintf(outbox, sizeof outbox, "%u need attention",
                 static_cast<unsigned>(beforeAttention));
    else if (beforePending)
        snprintf(outbox, sizeof outbox, "%u pending", static_cast<unsigned>(beforePending));
    else
        snprintf(outbox, sizeof outbox, "none");
    syncRowBegin(SyncOutbox, outbox);
    flushOutbox();
    const size_t afterPending = kk::outboxPendingCount(state().outbox.gOutbox);
    const size_t afterAttention = kk::outboxAttentionCount(state().outbox.gOutbox);
    if (afterAttention && afterPending)
        snprintf(outbox, sizeof outbox, "%u attention, %u pending",
                 static_cast<unsigned>(afterAttention), static_cast<unsigned>(afterPending));
    else if (afterAttention)
        snprintf(outbox, sizeof outbox, "%u need attention",
                 static_cast<unsigned>(afterAttention));
    else if (afterPending)
        snprintf(outbox, sizeof outbox, "%u pending", static_cast<unsigned>(afterPending));
    else
        snprintf(outbox, sizeof outbox, "sent");
    if (afterAttention || afterPending)
        syncRowWarn(SyncOutbox, outbox);
    else
        syncRowDone(SyncOutbox, outbox);
    if (syncStopRequested()) {
        finishSyncCancelled();
        return;
    }

    syncRowBegin(SyncAccount, "checking");
    runProbe();
    if (state().sync.gSyncIssue != SyncIssue::None) {
        syncRowFail(SyncAccount, state().ui.gNetLine);
        return;
    }
    char account[40];
    snprintf(account, sizeof account, "%s L%d", state().session.gUser.username.c_str(), state().session.gUser.level);
    syncRowDone(SyncAccount, account);
    if (syncStopRequested()) {
        finishSyncCancelled();
        return;
    }

    syncRowBegin(SyncSummary, "fetching");
    runSummary();
    if (state().sync.gSyncIssue != SyncIssue::None) {
        syncRowFail(SyncSummary, state().ui.gNetLine);
        return;
    }
    char summary[40];
    snprintf(summary, sizeof summary, "%u due, %u lessons",
             static_cast<unsigned>(state().session.gDueReviews), static_cast<unsigned>(state().session.gLessons));
    syncRowDone(SyncSummary, summary);
    if (syncStopRequested()) {
        finishSyncCancelled();
        return;
    }

    syncRowBegin(SyncLevel, "fetching");
    runLevelProgress();
    if (kk::wkAuthBlocked()) {
        setSyncIssue(401, true, 0, "Level");
        syncRowWarn(SyncLevel, "token rejected");
    } else if (kk::wkRateLimitWaitSeconds() > 0) {
        // Non-critical step: keep syncing, but surface the countdown.
        setSyncIssue(429, true, 0, "Level");
        syncRowWarn(SyncLevel, "rate limited");
    } else if (state().session.gLevelProgress.valid) {
        char level[40];
        formatLevelProgressLine(level, sizeof level);
        syncRowDone(SyncLevel, level);
    } else {
        syncRowWarn(SyncLevel, "unavailable");
    }
    if (syncStopRequested()) {
        finishSyncCancelled();
        return;
    }

    syncRowBegin(SyncSrs, "fetching");
    const bool srsOk = runSrsCounts();
    if (kk::wkAuthBlocked()) {
        setSyncIssue(401, true, 0, "SRS");
        syncRowWarn(SyncSrs, "token rejected");
    } else if (kk::wkRateLimitWaitSeconds() > 0) {
        setSyncIssue(429, true, 0, "SRS");
        syncRowWarn(SyncSrs, "rate limited");
    } else if (srsOk) {
        char srs[40];
        snprintf(srs, sizeof srs, "A%u G%u M%u E%u B%u",
                 static_cast<unsigned>(state().session.gSrs.apprentice), static_cast<unsigned>(state().session.gSrs.guru),
                 static_cast<unsigned>(state().session.gSrs.master), static_cast<unsigned>(state().session.gSrs.enlightened),
                 static_cast<unsigned>(state().session.gSrs.burned));
        syncRowDone(SyncSrs, srs);
    } else {
        syncRowWarn(SyncSrs, "unavailable");
    }
    if (syncStopRequested()) {
        finishSyncCancelled();
        return;
    }

    syncRowBegin(SyncCritical, "fetching");
    const bool criticalOk = runCriticalItems();
    if (kk::wkAuthBlocked()) {
        setSyncIssue(401, true, 0, "Critical");
        syncRowWarn(SyncCritical, "token rejected");
    } else if (kk::wkRateLimitWaitSeconds() > 0) {
        setSyncIssue(429, true, 0, "Critical");
        syncRowWarn(SyncCritical, "rate limited");
    } else if (criticalOk) {
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
    if (syncStopRequested()) {
        finishSyncCancelled();
        return;
    }

    applyDeferredAuthIssue();
    state().sync.gSyncing = false;
    state().sync.gSyncFinished = true;
    state().sync.gSyncScreen = false;
    Serial.println("[sync] complete");
    firmwareAfterSync();
    state().ui.gLastInputAt = millis();
    state().display.gDirty = true;
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

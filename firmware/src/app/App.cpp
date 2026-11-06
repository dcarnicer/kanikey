// KaniKey application state, input handling, services and screen routing.
//
// main.cpp owns only the Arduino entry points. Everything that knows about the
// session, the cache, the outbox, the setup portal or the serial console lives
// here; the individual frames are drawn by the app/*Screen views.

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
#include "platform/CriticalStore.h"
#include "platform/GlyphPack.h"
#include "platform/JournalStore.h"
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

lgfx::LGFX_Device* gDisp = nullptr;
lgfx::LGFX_Sprite* gCanvas = nullptr;
bool gDirty = true;
uint32_t gLastStatusMs = 0;
uint32_t gLastDrawMs = 0;
uint32_t gLastHomeRefreshMs = 0;

bool gSdOk = false;
bool gFsOk = false;
bool gFsFormatPrompt = false;
#ifdef KANIKEY_DEV
// Dev-only simulation: pretend the device is on battery so the idle-sleep
// path can run while USB is attached for the serial log.
bool gDevForceBattery = false;
#endif
bool devForceBattery() {
#ifdef KANIKEY_DEV
    return gDevForceBattery;
#else
    return false;
#endif
}
char gSdLine[48] = "SD: not mounted";
uint32_t gLastInputAt = 0;
bool gDimmed = false;
bool gScreenOff = false;
char gNetLine[48] = "NET: not tested";
String gSerialLine;

kk::User gUser;
kk::Summary gSummary;
kk::LevelProgress gLevelProgress;
bool gLevelUpNotice = false;
uint8_t gLevelUpFrom = 0;
uint8_t gLevelUpTo = 0;
uint32_t gLevelUpAt = 0;
kk::SrsCounts gSrs;
bool gHaveUser = false;
bool gHaveSummary = false;
bool gShowHome = false;
uint32_t gDueReviews = 0;
uint32_t gLessons = 0;
uint32_t gSyncedAt = 0;
char gNextReview[20] = "--:--";
char gSyncClock[12] = "--:--";

kk::ReviewBatch gBatch;
// The cached review batch is loaded on demand: Home and lessons do not need
// its subjects, and keeping ~20 subjects alive during a lesson fetch left the
// TLS handshake with less than 4 KB of free heap. When the batch lives only on
// disk, the snapshot's assignment list is preserved on the next save.
bool gBatchEvicted = false;
size_t gCachedBatchCount = 0;
kk::ReviewQueue gQueue;
bool gReviewActive = false;
bool gReviewSummary = false;
bool gFeedback = false;
bool gNeedsSubmit = false;
uint32_t gPendingSeq = 0;  // outbox record queued at completion, sent on Enter
bool gFeedbackCompleted = false;
bool gReviewDetail = false;
int gReviewDetailPage = 0;
int gReviewDetailPages = 5;
kk::AnswerCheck gCheck;
kk::ReviewPart gFeedbackPart = kk::ReviewPart::None;
uint32_t gFeedbackSubjectId = 0;
std::string gFeedbackInput;
kk::SessionItem gCompletedItem;
struct StageChange {
    bool valid = false;
    uint8_t from = 0;
    uint8_t to = 0;
};
StageChange gStageChange;
uint32_t gPendingStageAssignment = 0;
uint8_t gPendingStageFrom = 0;
uint32_t gStageUps = 0;        // successful upward moves in the current session
uint32_t gReviewStartMs = 0;   // millis() when the current session started
uint32_t gReviewElapsedMs = 0; // latched when the summary is entered
std::string gMeaningInput;
#ifdef KANIKEY_DEV
std::string gDirectReading;  // test/serial reading input, bypasses the romaji IME
#endif
kk::RomajiIme gIme;
std::vector<kk::OutboxItem> gOutbox;
kk::LessonBatch gLessonBatch;
std::vector<kk::Subject> gLessonComponents;
kk::LessonQuiz gLessonQuiz;
std::string gLessonQuizInput;
#ifdef KANIKEY_DEV
std::string gLessonQuizDirectReading;
int gDevLessonStartCode = -1;  // <0 = use the real PUT /start
bool gDevKeyTest = false;      // log the order of the injected key events
#endif
kk::RomajiIme gLessonQuizIme;
kk::GlyphPack gPack;
WiFiServer gAssetServer(8080);
bool gAssetServerActive = false;
// A portal "Check pack" that needs an open is deferred to loop(): a File opened
// inside the HTTP request can lose its descriptor when the client closes.
bool gPackRecheckPending = false;
bool gLessonActive = false;
bool gLessonListActive = false;
bool gLessonEmptyActive = false;
bool gLessonQuizActive = false;
bool gStatsActive = false;
int gStatsPage = 0;
kk::app::StatsHistoryView gStatsHistory;
bool gStatsHistoryLoaded = false;
std::vector<kk::CriticalItem> gCritical;
bool gCriticalLoaded = false;
std::vector<kk::Subject> gPracticeSubjects;
std::vector<kk::Subject> gCriticalSubjects;
bool gPracticeActive = false;
int gPracticeMode = 0;
uint32_t gPracticeSkipped = 0;
bool gStudyMenuActive = false;
int gStudyIndex = 0;
char gStudyNote[48] = "";
char gStudyCounts[4][12] = {};
bool gCriticalListActive = false;
int gCriticalIndex = 0;
std::vector<kk::app::CriticalRowView> gCriticalRows;
uint32_t gDetailSubjectId = 0;
bool gDetailFromCritical = false;
uint32_t gPendingJournalHandle = 0;
size_t gLessonIndex = 0;
size_t gLessonPage = 0;
bool gSyncing = false;
bool gSyncAttempted = false;
bool gOffline = false;
uint32_t gCacheSavedAt = 0;
enum class SyncIssue : uint8_t { None, Network, Auth, Forbidden, RateLimit, Server, Other };
SyncIssue gSyncIssue = SyncIssue::None;
// An outbox 401/403 must survive a successful /user or /summary later in the
// same sync; it is reapplied before the sync screen closes.
SyncIssue gDeferredAuthIssue = SyncIssue::None;
// True when the current Auth/Forbidden gSyncIssue came from the outbox, so a
// later drain can clear it without hiding a failed probe/summary.
bool gOutboxAuthIssue = false;

// Sync screen: one row per real step, updated between blocking requests.
enum SyncRowIndex : uint8_t {
    SyncWifi,
    SyncOutbox,
    SyncAccount,
    SyncSummary,
    SyncLevel,
    SyncSrs,
    SyncCritical,
};
struct SyncRow {
    const char* label = "";
    char value[40] = "";
    SyncRowState state = SyncRowState::Pending;
};
SyncRow gSyncRows[kSyncRowCount];
uint8_t gSyncCompleted = 0;
bool gSyncScreen = false;
bool gSyncFinished = false;
bool gSyncCancelRequested = false;

kk::SetupPortal gSetupPortal;
kk::SetupPortalHooks gSetupHooks;
bool gPortalActive = false;

enum class SettingsOrigin : uint8_t { Home, Pause };
bool gPauseActive = false;
int gPauseIndex = 0;
bool gSettingsActive = false;
int gSettingsIndex = 0;
SettingsOrigin gSettingsOrigin = SettingsOrigin::Home;
bool gAboutActive = false;
bool gSignOutConfirm = false;
bool gOutboxScreen = false;
bool gOutboxConfirm = false;
kk::ota::SavedState gOtaState;
bool gOtaPending = false;
uint32_t gOtaLoopAt = 0;
bool gFirmwareActive = false;
int gFirmwareIndex = 0;
enum class FirmwareStep : uint8_t { Idle, Checking, Installing, Restarting };
FirmwareStep gFirmwareStep = FirmwareStep::Idle;
bool gFirmwareChecked = false;
bool gFirmwareConfirm = false;
kk::ota::Manifest gFirmwareManifest;
kk::ota::Offer gFirmwareOffer;
char gFirmwareStatus[120] = "";
uint16_t gFirmwareStatusColor = kDim;
char gFirmwareNote[96] = "";
uint16_t gFirmwareNoteColor = kFaint;
int gFirmwareProgress = -1;
uint32_t gOutboxSelectedSeq = 0;
char gOutboxNote[40] = "";
bool gLowBatteryPrompt = false;
int gLowBatteryAction = 0;  // 1 = review, 2 = lessons
bool gLowBatteryConfirmed = false;
// One blocking notice frame for the review empty/error states, a full outbox
// and the storage prompt. Drawing lives in drawNotice().
enum class NoticeAction : uint8_t {
    None,
    ReviewEmpty,
    ReviewError,
    LessonError,
    LessonStart,
    OutboxFull,
    StorageFormat,
};
bool gNoticeActive = false;
NoticeAction gNoticeAction = NoticeAction::None;
char gNoticeLine[64] = "";
char gNoticeLine2[64] = "";
char gSettingsNote[48] = "";
uint16_t gSettingsNoteColor = kFaint;
#ifdef KANIKEY_DEV
bool gShowDebug = false;
bool gShowRadicalTest = false;
uint32_t gRadicalTestId = 1;
#endif

void localHm(uint32_t epoch, char* out, size_t n);
void localDate(uint32_t epoch, char* out, size_t n);
void draw();
void drawLoading(const char* title, const char* subtitle);
void showReview();
void showReviewDetail();
void showReviewSummary();
std::string subjectMainMeaning(const kk::Subject& s);
void showLesson();
void showLessonList();
void showLessonEmpty();
void showLessonQuiz();
void lessonQuizKey(const KeyEvent& e);
void drawBoot();
void drawSync();
void retryBoot();
bool startSetupPortal();
bool mountSd();
void startSync();
void syncScreenKey(const KeyEvent& e);
void drawPause();
void drawStats();
void statsKey(const KeyEvent& e);
void openStats();
void openStudyMenu();
void drawStudyMenu();
void studyMenuKey(const KeyEvent& e);
void openCriticalList();
void drawCriticalList();
void criticalListKey(const KeyEvent& e);
void startPractice(int mode);
void drawSettings();
void drawAbout();
void drawSignOutConfirm();
void drawLowBattery();
void drawOutbox();
void drawNotice();
void noticeKey(const KeyEvent& e);
void continueBootAfterStorage();
void openPause();
void wakeScreen();
void openFirmware();
void drawFirmware();
void firmwareKey(const KeyEvent& e);
void firmwareBoot();
void firmwareTick(uint32_t now);
void firmwareBeforeSleep();
void firmwareCheck(bool manual);
void firmwareAfterSync();
bool gFirmwareStateIdle();

void clearLevelUpNotice() {
    gLevelUpNotice = false;
    gLevelUpFrom = 0;
    gLevelUpTo = 0;
    gLevelUpAt = 0;
}

uint32_t currentAccountKey() {
    const String token = kk::DevToken::load();
    return kk::cacheAccountKey(std::string_view(token.c_str(), token.length()));
}

// Without SNTP there is no battery-backed clock. Use the last cache save as
// the reference so cached due counts and available_at checks stay meaningful.
uint32_t effectiveNow() {
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    if (now >= 1700000000u) return now;
    if (gCacheSavedAt) return gCacheSavedAt;
    if (gBatch.fetchedAt) return gBatch.fetchedAt;
    return now;
}

uint32_t localDayKeyForEpoch(uint32_t epoch) {
    if (epoch < 1700000000u) return 0;
    const time_t t = static_cast<time_t>(epoch);
    struct tm local {};
    if (!localtime_r(&t, &local)) return 0;
    return kk::civilDayKey(local.tm_year + 1900, static_cast<unsigned>(local.tm_mon + 1),
                           static_cast<unsigned>(local.tm_mday));
}

struct HistoryCollector {
    std::vector<kk::DayStats> days;
    uint32_t todayKey = 0;
    uint32_t oldestKey = 0;
};

bool collectJournalRecord(const kk::JournalRecord& record, void* user) {
    HistoryCollector* collector = static_cast<HistoryCollector*>(user);
    if (!collector || record.completedAt == 0) return true;
    const uint32_t key = localDayKeyForEpoch(record.completedAt);
    if (!key || key > collector->todayKey || key < collector->oldestKey) return true;
    kk::DayStats* day = nullptr;
    for (kk::DayStats& candidate : collector->days)
        if (candidate.dayKey == key) {
            day = &candidate;
            break;
        }
    if (!day) {
        collector->days.push_back({key, 0, 0, 0});
        day = &collector->days.back();
    }
    if (day->reviews < 0xFFFF) ++day->reviews;
    const uint16_t wrong =
        static_cast<uint16_t>(record.incorrectMeaning) + record.incorrectReading;
    if (wrong == 0) {
        if (day->correct < 0xFFFF) ++day->correct;
    } else if (day->incorrect < 0xFFFF) {
        ++day->incorrect;
    }
    return true;
}

void loadCritical() {
    if (gCriticalLoaded) return;
    gCriticalLoaded = true;
    gCritical.clear();
    if (gFsOk) kk::criticalLoad(currentAccountKey(), &gCritical);
}

void loadStatsHistory() {
    gStatsHistory = kk::app::StatsHistoryView{};
    gStatsHistoryLoaded = true;
    const uint32_t todayKey = localDayKeyForEpoch(effectiveNow());
    if (!todayKey) return;
    if (!kk::journalOpen(currentAccountKey(), false)) return;
    HistoryCollector collector;
    collector.todayKey = todayKey;
    collector.oldestKey = todayKey > 400 ? todayKey - 400 : 0;
    collector.days.reserve(64);
    kk::journalForEach(collectJournalRecord, &collector);

    kk::HistoryStats stats;
    kk::buildHistory(collector.days.data(), collector.days.size(), todayKey, &stats);
    gStatsHistory.any = stats.any;
    gStatsHistory.hasToday = stats.hasToday;
    gStatsHistory.todayAccuracy = stats.todayAccuracy;
    gStatsHistory.currentStreak = stats.currentStreak;
    gStatsHistory.bestStreak = stats.bestStreak;
    for (size_t i = 0; i < kk::kHistoryDays; ++i) {
        gStatsHistory.counts[i] = stats.counts[i];
        const uint32_t key = todayKey >= kk::kHistoryDays - 1
                                 ? todayKey - (kk::kHistoryDays - 1) + i
                                 : i;
        static const char* names[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        snprintf(gStatsHistory.labels[i], sizeof gStatsHistory.labels[i], "%s",
                 names[(key + 4) % 7]);
    }
}

void applySummary(uint32_t now) {
    gDueReviews = gSummary.dueReviews(now);
    gLessons = gSummary.totalLessons();
    char hhmm[8] = "--:--";
    const uint32_t nextAt = gSummary.nextReviewAt(now);
    if (nextAt) localHm(nextAt, hhmm, sizeof hhmm);
    kk::formatNextReview(gSummary, now, hhmm, gNextReview, sizeof gNextReview);
}

bool outboxHasAssignment(uint32_t assignmentId) {
    for (const kk::OutboxItem& it : gOutbox)
        if (it.assignmentId == assignmentId) return true;
    return false;
}

// Every record occupies a slot, whatever its state (rejected, stuck, sending
// and ambiguous included). startReview() refuses to start when this is smaller
// than the batch, so a completed review always has a place to go.
size_t outboxFreeSlots() {
    return gOutbox.size() < kk::kOutboxMax ? kk::kOutboxMax - gOutbox.size() : 0;
}

void removeSubjectFromReviews(kk::Summary& summary, uint32_t subjectId) {
    for (kk::SummaryBucket& b : summary.reviews) {
        auto it = std::find(b.subjectIds.begin(), b.subjectIds.end(), subjectId);
        if (it == b.subjectIds.end()) continue;
        b.subjectIds.erase(it);
        if (b.count > 0) --b.count;
    }
}

// Completed-but-unsent reviews are still in the outbox; they must not come
// back as due after a reboot.
void pruneCacheOutbox() {
    if (gOutbox.empty()) return;
    for (size_t i = 0; i < gBatch.assignments.size();) {
        if (outboxHasAssignment(gBatch.assignments[i].id)) {
            removeSubjectFromReviews(gSummary, gBatch.assignments[i].subjectId);
            gBatch.assignments.erase(gBatch.assignments.begin() + i);
        } else {
            ++i;
        }
    }
    for (const kk::OutboxItem& it : gOutbox)
        if (it.subjectId) removeSubjectFromReviews(gSummary, it.subjectId);
    gBatch.ok = !gBatch.assignments.empty() && !gBatch.subjects.empty();
}

// A fresh summary is authoritative: drop cached assignments that are no
// longer due or that were reviewed in the meantime.
void pruneBatchToSummary(uint32_t now) {
    if (gBatch.assignments.empty()) return;
    const std::vector<uint32_t> due = gSummary.dueSubjectIds(now, 1000);
    for (size_t i = 0; i < gBatch.assignments.size();) {
        const kk::Assignment& a = gBatch.assignments[i];
        const bool stillDue = std::find(due.begin(), due.end(), a.subjectId) != due.end();
        if (!stillDue || outboxHasAssignment(a.id) || (a.availableAt && a.availableAt > now))
            gBatch.assignments.erase(gBatch.assignments.begin() + i);
        else
            ++i;
    }
    gBatch.ok = !gBatch.assignments.empty() && !gBatch.subjects.empty();
}

void saveCacheSnapshot() {
    if (!gFsOk) return;  // no storage: keep everything in memory only
    if (!gHaveSummary || !kk::DevToken::has()) return;
    kk::CacheSnapshot snapshot;
    snapshot.meta.savedAt = gSyncedAt;
    snapshot.meta.accountKey = currentAccountKey();
    snapshot.summary = gSummary;
    if (gHaveUser) snapshot.user = gUser;
    snapshot.levelProgress = gLevelProgress;
    snapshot.srs = gSrs;
    if (gBatch.ok) {
        snapshot.assignments = gBatch.assignments;
    } else if (gBatchEvicted) {
        // The batch is on disk only: keep its assignments instead of writing an
        // empty list. The summary and the other fields still come from memory.
        kk::CacheSnapshot previous;
        if (kk::cacheLoadSnapshot(&previous) &&
            previous.meta.accountKey == snapshot.meta.accountKey)
            snapshot.assignments = std::move(previous.assignments);
    }
    if (!kk::cacheSaveSnapshot(snapshot)) {
        Serial.println("[cache] snapshot save failed");
        return;
    }
    gCacheSavedAt = snapshot.meta.savedAt;
    gCachedBatchCount = snapshot.assignments.size();
    Serial.printf("[cache] snapshot saved: %u assignments, %u due\n",
                  static_cast<unsigned>(snapshot.assignments.size()),
                  static_cast<unsigned>(gDueReviews));
}

void saveSubjectCache() {
    if (!gFsOk) return;  // no storage: keep everything in memory only
    if (!gSdOk || !gBatch.ok || gBatch.subjects.empty() || !kk::DevToken::has()) return;
    kk::CacheMeta meta;
    meta.savedAt = gBatch.fetchedAt ? gBatch.fetchedAt : gSyncedAt;
    meta.accountKey = currentAccountKey();
    // cacheSaveSubjects() logs the count, size and time of a good save.
    if (!kk::cacheSaveSubjects(SD, meta, gBatch.subjects))
        Serial.println("[cache] subjects save failed");
}

void saveCacheAll() {
    saveCacheSnapshot();
    saveSubjectCache();
}

bool loadCache() {
    if (!gFsOk || !gSdOk || !kk::DevToken::has()) return false;
    kk::CacheSnapshot snapshot;
    if (!kk::cacheLoadSnapshot(&snapshot)) return false;
    const uint32_t key = currentAccountKey();
    if (snapshot.meta.accountKey != key) {
        Serial.println("[cache] token/account changed; discarding cache");
        kk::cacheClearSnapshot();
        kk::cacheClearSubjects(SD);
        return false;
    }
    gSummary = std::move(snapshot.summary);
    gHaveSummary = true;
    gHaveUser = !snapshot.user.username.empty() && snapshot.user.level > 0;
    if (gHaveUser) gUser = std::move(snapshot.user);
    gLevelProgress = snapshot.levelProgress;
    gSrs = snapshot.srs;

    // The assignment list is cheap and only a few hundred bytes; the subjects
    // are not. Keep the count for About and let startReview() load the batch
    // from disk when it is actually needed.
    gBatch = kk::ReviewBatch{};
    gCachedBatchCount = snapshot.assignments.size();
    gBatchEvicted = gCachedBatchCount > 0;

    gCacheSavedAt = snapshot.meta.savedAt;
    gSyncedAt = snapshot.meta.savedAt;
    if (gSyncedAt) localHm(gSyncedAt, gSyncClock, sizeof gSyncClock);
    pruneCacheOutbox();
    applySummary(effectiveNow());
    gOffline = true;
    gShowHome = true;
    Serial.printf("[cache] loaded: %u cached assignments (batch lazy), %u due, saved %u\n",
                  static_cast<unsigned>(gCachedBatchCount),
                  static_cast<unsigned>(gDueReviews), static_cast<unsigned>(gCacheSavedAt));
    if (gSrs.valid)
        Serial.printf("[cache] srs A %u, G %u, M %u, E %u, B %u\n",
                      static_cast<unsigned>(gSrs.apprentice), static_cast<unsigned>(gSrs.guru),
                      static_cast<unsigned>(gSrs.master),
                      static_cast<unsigned>(gSrs.enlightened),
                      static_cast<unsigned>(gSrs.burned));
    return true;
}

// Loads the cached review batch (assignments + subjects) on demand. The
// snapshot is authoritative for the assignment list; the subject cache on SD
// supplies the data used by the review screens.
bool loadCachedBatch() {
    if (!gFsOk || !gSdOk || !kk::DevToken::has()) return false;
    kk::CacheSnapshot snapshot;
    if (!kk::cacheLoadSnapshot(&snapshot) || snapshot.assignments.empty()) return false;
    const uint32_t key = currentAccountKey();
    if (snapshot.meta.accountKey != key) return false;

    kk::SubjectCache subjects;
    if (!kk::cacheLoadSubjects(SD, &subjects) || subjects.meta.accountKey != key) return false;

    kk::ReviewBatch batch;
    for (const kk::Assignment& a : snapshot.assignments) {
        const kk::Subject* match = nullptr;
        for (const kk::Subject& s : subjects.subjects)
            if (s.id == a.subjectId) {
                match = &s;
                break;
            }
        if (!match) continue;
        batch.assignments.push_back(a);
        batch.subjects.push_back(*match);
    }
    // Keep component subjects too, for the offline detail card.
    for (const kk::Subject& s : subjects.subjects) {
        if (std::any_of(batch.subjects.begin(), batch.subjects.end(),
                        [&s](const kk::Subject& x) { return x.id == s.id; }))
            continue;
        batch.subjects.push_back(s);
    }
    if (batch.assignments.empty()) return false;
    batch.fetchedAt = snapshot.meta.savedAt;
    batch.ok = true;
    gBatch = std::move(batch);
    gBatchEvicted = false;
    gCachedBatchCount = gBatch.assignments.size();
    pruneCacheOutbox();
    pruneBatchToSummary(effectiveNow());
    gCachedBatchCount = gBatch.assignments.size();
    gBatch.ok = !gBatch.assignments.empty() && !gBatch.subjects.empty();
    Serial.printf("[cache] batch loaded: %u assignments, %u subjects\n",
                  static_cast<unsigned>(gBatch.assignments.size()),
                  static_cast<unsigned>(gBatch.subjects.size()));
    return gBatch.ok;
}

// Drops the batch's subject strings when the app leaves review mode or starts
// a lesson fetch. The assignment list and subjects stay on disk, so the next
// session reloads them without a network round trip.
void evictCachedBatch(bool save) {
    if (gBatch.assignments.empty() && gBatch.subjects.empty() && !gBatchEvicted) return;
    if (save) saveCacheSnapshot();
    if (!gBatch.assignments.empty()) gCachedBatchCount = gBatch.assignments.size();
    gBatch = kk::ReviewBatch{};
    gBatchEvicted = gCachedBatchCount > 0;
    Serial.printf("[cache] batch evicted, %u assignments remain on disk\n",
                  static_cast<unsigned>(gCachedBatchCount));
}

void markCacheCompleted(uint32_t subjectId, uint32_t assignmentId) {
    if (!gHaveSummary) return;
    removeSubjectFromReviews(gSummary, subjectId);
    for (size_t i = 0; i < gBatch.assignments.size();) {
        if (gBatch.assignments[i].id == assignmentId)
            gBatch.assignments.erase(gBatch.assignments.begin() + i);
        else
            ++i;
    }
    gBatch.ok = !gBatch.assignments.empty() && !gBatch.subjects.empty();
    gCachedBatchCount = gBatch.assignments.size();
    saveCacheSnapshot();
}

lgfx::LovyanGFX& target() {
    if (gCanvas) return *gCanvas;
    return *gDisp;
}
void present() {
    if (gCanvas && gDisp) gCanvas->pushSprite(0, 0);
}

bool ensureCanvas() {
    if (gCanvas || !gDisp) return gCanvas != nullptr;
    for (int attempt = 0; attempt < 4; ++attempt) {
        gCanvas = new lgfx::LGFX_Sprite(gDisp);
        gCanvas->setColorDepth(lgfx::rgb332_1Byte);
        if (gCanvas->createSprite(gDisp->width(), gDisp->height())) return true;
        Serial.printf("[kk] canvas attempt %d failed, heap %u largest %u\n", attempt + 1,
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                             MALLOC_CAP_8BIT)));
        delete gCanvas;
        gCanvas = nullptr;
        if (attempt < 3) delay(120);
    }
    Serial.println("[kk] drawing directly");
    return false;
}

void releaseCanvas() {
    if (!gCanvas) return;
    delete gCanvas;
    gCanvas = nullptr;
}

// TLS needs one large block; give it the canvas only for the request and take
// it back before the response is parsed, so the frame buffer never fragments
// the heap while subjects are being decoded.
bool gFreeCanvasForRequest = false;
void requestBefore() {
    if (gFreeCanvasForRequest) releaseCanvas();
}
void requestAfter() {
    if (gFreeCanvasForRequest) ensureCanvas();
}

bool gShift = false;
bool gFn = false;
bool gCtrl = false;
bool gOpt = false;
bool gAlt = false;

struct Line {
    char text[44];
};
Line gLog[3];

const char* keyName(Key k) {
    switch (k) {
        case Key::Up: return "Up";
        case Key::Down: return "Down";
        case Key::Left: return "Left";
        case Key::Right: return "Right";
        case Key::Enter: return "Enter";
        case Key::Esc: return "Esc";
        case Key::Backspace: return "Backspace";
        case Key::Tab: return "Tab";
        case Key::Other: return "Other";
        case Key::None: return "None";
    }
    return "?";
}

void pushLog(const char* text) {
    for (int i = 0; i < 2; ++i) gLog[i] = gLog[i + 1];
    snprintf(gLog[2].text, sizeof gLog[2].text, "%s", text);
}

void updateModifier(uint8_t x, uint8_t y, bool down) {
    switch (tgc::cardputerModifierAt(x, y)) {
        case tgc::Modifier::Shift: gShift = down; break;
        case tgc::Modifier::Fn: gFn = down; break;
        case tgc::Modifier::Ctrl: gCtrl = down; break;
        case tgc::Modifier::Opt: gOpt = down; break;
        case tgc::Modifier::Alt: gAlt = down; break;
        case tgc::Modifier::None: break;
    }
}

void setSyncIssue(int httpCode, bool connected, int tlsError, const char* what) {
    gOutboxAuthIssue = false;
    if (!connected) {
        gSyncIssue = SyncIssue::Network;
        if (tlsError)
            snprintf(gNetLine, sizeof gNetLine, "%s: network/TLS %d", what, tlsError);
        else
            snprintf(gNetLine, sizeof gNetLine, "%s: network unreachable", what);
        return;
    }
    if (httpCode == 401) {
        gSyncIssue = SyncIssue::Auth;
        snprintf(gNetLine, sizeof gNetLine, "%s: token rejected (401)", what);
    } else if (httpCode == 403) {
        gSyncIssue = SyncIssue::Forbidden;
        snprintf(gNetLine, sizeof gNetLine, "%s: missing permissions (403)", what);
    } else if (httpCode == 429) {
        gSyncIssue = SyncIssue::RateLimit;
        const uint32_t wait = kk::wkRateLimitWaitSeconds();
        if (wait)
            snprintf(gNetLine, sizeof gNetLine, "%s: rate limited, %us", what,
                     static_cast<unsigned>(wait));
        else
            snprintf(gNetLine, sizeof gNetLine, "%s: rate limited (429)", what);
    } else if (httpCode >= 500) {
        gSyncIssue = SyncIssue::Server;
        snprintf(gNetLine, sizeof gNetLine, "%s: WaniKani error (%d)", what, httpCode);
    } else if (httpCode == 200) {
        gSyncIssue = SyncIssue::Other;
        snprintf(gNetLine, sizeof gNetLine, "%s: invalid response", what);
    } else if (httpCode > 0) {
        gSyncIssue = SyncIssue::Other;
        snprintf(gNetLine, sizeof gNetLine, "%s: HTTP %d", what, httpCode);
    } else {
        gSyncIssue = SyncIssue::Other;
        snprintf(gNetLine, sizeof gNetLine, "%s: invalid response", what);
    }
}

void applyDeferredAuthIssue() {
    if (gDeferredAuthIssue == SyncIssue::Auth) {
        setSyncIssue(401, true, 0, "Reviews");
        gOutboxAuthIssue = true;
    } else if (gDeferredAuthIssue == SyncIssue::Forbidden &&
               gSyncIssue != SyncIssue::Auth) {
        setSyncIssue(403, true, 0, "Reviews");
        gOutboxAuthIssue = true;
    }
}

void clearOutboxAuthIssue() {
    gDeferredAuthIssue = SyncIssue::None;
    if (gOutboxAuthIssue &&
        (gSyncIssue == SyncIssue::Auth || gSyncIssue == SyncIssue::Forbidden))
        gSyncIssue = SyncIssue::None;
    gOutboxAuthIssue = false;
}

void runProbe() {
    const String token = kk::DevToken::load();
    const uint8_t previousLevel = gHaveUser ? gUser.level : 0;
    snprintf(gNetLine, sizeof gNetLine, "API: probing...");
    gDirty = true;
    const kk::WkProbeResult r = kk::probeWaniKaniUser(token);
    if (r.parsed) {
        gUser = r.user;
        gHaveUser = true;
        if (previousLevel && r.user.level > previousLevel) {
            gLevelUpFrom = previousLevel;
            gLevelUpTo = static_cast<uint8_t>(r.user.level);
            gLevelUpAt = effectiveNow();
            gLevelUpNotice = true;
        } else if (!previousLevel || r.user.level < previousLevel) {
            clearLevelUpNotice();
        }
        gSyncIssue = SyncIssue::None;
        gOutboxAuthIssue = false;
    } else {
        setSyncIssue(r.httpCode, r.connected, r.tlsError, "API");
    }
    if (r.connected) {
        if (r.httpCode == 200 && r.parsed)
            snprintf(gNetLine, sizeof gNetLine, "API: 200 %s L%d", r.user.username.c_str(),
                     r.user.level);
        else if (!r.parsed)
            Serial.printf("[api] HTTP %d, parsed=%d\n", r.httpCode, r.parsed ? 1 : 0);
    }
    gDirty = true;
}

void runSummary() {
    const String token = kk::DevToken::load();
    const kk::SummaryProbeResult r = kk::probeWaniKaniSummary(token);
    const uint32_t now = effectiveNow();
    if (r.parsed) {
        gSummary = r.summary;
        gHaveSummary = true;
        gSyncIssue = SyncIssue::None;
        gOutboxAuthIssue = false;
        gSyncedAt = now >= 1700000000u ? now : 0;
        if (gSyncedAt) localHm(gSyncedAt, gSyncClock, sizeof gSyncClock);
        pruneBatchToSummary(now);
        applySummary(now);
        gOffline = false;
        gShowHome = true;
        saveCacheSnapshot();
    }
    if (r.parsed)
        Serial.printf("[summary] due %u of %u reviews, %u lessons, next %s\n",
                      static_cast<unsigned>(r.summary.dueReviews(now)),
                      static_cast<unsigned>(r.summary.totalReviews()),
                      static_cast<unsigned>(r.summary.totalLessons()),
                      r.summary.nextReviewsAt.c_str());
    if (r.parsed)
        snprintf(gNetLine, sizeof gNetLine, "Summary: %ur %ul",
                 static_cast<unsigned>(r.summary.totalReviews()),
                 static_cast<unsigned>(r.summary.totalLessons()));
    else
        setSyncIssue(r.httpCode, r.connected, r.tlsError, "Summary");
    gDirty = true;
}

void formatLevelProgressLine(char* out, size_t n) {
    if (!out || n == 0) return;
    out[0] = 0;
    if (!gLevelProgress.valid) return;
    char date[16];
    localDate(gLevelProgress.unlockedAt ? gLevelProgress.unlockedAt : gLevelProgress.startedAt,
              date, sizeof date);
    const uint8_t percent =
        kk::levelKanjiPercent(gLevelProgress.kanjiPassed, gLevelProgress.kanjiTotal);
    if (gLevelProgress.kanjiTotal) {
        if (date[0])
            snprintf(out, n, "L%u %u%% since %s", static_cast<unsigned>(gLevelProgress.level),
                     static_cast<unsigned>(percent), date);
        else
            snprintf(out, n, "L%u %u%%", static_cast<unsigned>(gLevelProgress.level),
                     static_cast<unsigned>(percent));
    } else if (date[0]) {
        snprintf(out, n, "L%u since %s", static_cast<unsigned>(gLevelProgress.level), date);
    } else {
        snprintf(out, n, "Level %u", static_cast<unsigned>(gLevelProgress.level));
    }
}

void runLevelProgress() {
    if (!gHaveUser) return;
    const kk::LevelProgress p = kk::fetchLevelProgress(kk::DevToken::load(), gUser.level);
    if (!p.valid) return;
    gLevelProgress = p;
    saveCacheSnapshot();
    char date[16];
    localDate(gLevelProgress.unlockedAt ? gLevelProgress.unlockedAt : gLevelProgress.startedAt,
              date, sizeof date);
    Serial.printf("[level] %u %u%% since %s\n", static_cast<unsigned>(gLevelProgress.level),
                  static_cast<unsigned>(kk::levelKanjiPercent(gLevelProgress.kanjiPassed,
                                                               gLevelProgress.kanjiTotal)),
                  date);
}

bool runSrsCounts() {
    kk::SrsCounts counts;
    if (!kk::fetchSrsCounts(kk::DevToken::load(), &counts)) {
        Serial.println("[srs] distribution unavailable; keeping the cached counts");
        return false;
    }
    gSrs = counts;
    saveCacheSnapshot();
    return true;
}

bool runCriticalItems() {
    constexpr uint8_t kMaxPercent = 75;
    constexpr uint16_t kMinAnswers = 5;
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
    gCritical = std::move(items);
    gCriticalLoaded = true;
    Serial.printf("[critical] %u items\n", static_cast<unsigned>(gCritical.size()));
    return true;
}

bool runBatch() {
    if (!gHaveSummary) {
        Serial.println("[review] no summary yet");
        return false;
    }
    const size_t limit = static_cast<size_t>(kk::settingsReviewBatchSize());
    const uint32_t now = effectiveNow();
    const std::vector<uint32_t> due = gSummary.dueSubjectIds(now, 1000);
    // The summary keeps up to 1000 ids per bucket, so the batch can be chosen
    // from every due item. lower_levels_first keeps the summary order; the
    // shuffled order takes a fresh sample per session.
    std::vector<uint32_t> ids;
    if (gHaveUser && gUser.reviewsOrder == kk::ReviewOrder::Shuffled) {
        ids = kk::sampleIds(due, limit, esp_random());
    } else {
        ids.assign(due.begin(), due.begin() + std::min(due.size(), limit));
    }
    Serial.printf("[review] %u due, %u chosen\n", static_cast<unsigned>(due.size()),
                  static_cast<unsigned>(ids.size()));
    gFreeCanvasForRequest = true;
    kk::ReviewBatch batch = kk::fetchReviewBatch(kk::DevToken::load(), ids, now, limit);
    gFreeCanvasForRequest = false;
    if (!batch.ok) {
        Serial.printf("[review] batch failed: %s\n", batch.error.c_str());
        snprintf(gNetLine, sizeof gNetLine, "Batch: %s", batch.error.c_str());
        gBatch = kk::ReviewBatch{};
        gBatch.httpCode = batch.httpCode;
        gDirty = true;
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
    snprintf(gNetLine, sizeof gNetLine, "Batch: %ua %us",
             static_cast<unsigned>(batch.assignments.size()),
             static_cast<unsigned>(batch.subjects.size()));
    gBatch = std::move(batch);
    gBatchEvicted = false;
    gCachedBatchCount = gBatch.assignments.size();
    saveCacheAll();
    gDirty = true;
    return true;
}

const kk::Subject* subjectById(uint32_t id) {
    for (const kk::Subject& s : gBatch.subjects)
        if (s.id == id) return &s;
    for (const kk::Subject& s : gPracticeSubjects)
        if (s.id == id) return &s;
    for (const kk::Subject& s : gCriticalSubjects)
        if (s.id == id) return &s;
    return nullptr;
}

std::string correctAnswer(const kk::Subject& s, kk::ReviewPart part) {
    if (part == kk::ReviewPart::Reading) {
        for (const kk::Reading& r : s.readings)
            if (r.accepted) return r.text;
        if (!s.readings.empty()) return s.readings[0].text;
        return "-";
    }
    for (const kk::Meaning& m : s.meanings)
        if (m.accepted) return m.text;
    if (!s.meanings.empty()) return s.meanings[0].text;
    return "-";
}

void clearReviewInput() {
    gMeaningInput.clear();
#ifdef KANIKEY_DEV
    gDirectReading.clear();
#endif
    gIme.clear();
}

// The serial test helper can inject a reading directly; release builds only
// use the romaji IME.
std::string reviewInput(bool meaning) {
    if (meaning) return gMeaningInput;
#ifdef KANIKEY_DEV
    if (!gDirectReading.empty()) return gDirectReading;
#endif
    return gIme.text();
}

void showReview() {
    if (!gDisp) return;
    const kk::SessionItem* item = gQueue.current();
    const kk::Subject* s = gFeedback ? subjectById(gFeedbackSubjectId)
                                     : (item ? subjectById(item->subjectId) : nullptr);
    if (!s) return;
    const kk::ReviewPart part = gFeedback ? gFeedbackPart : gQueue.part();

    ReviewView view;
    view.subject = s;
    view.part = part;
    view.feedback = gFeedback;
    view.srsStage = gFeedbackCompleted ? gCompletedItem.srsStage : (item ? item->srsStage : 0);
    view.total = static_cast<int>(gQueue.completedCount() + gQueue.remaining());
    view.position = static_cast<int>(gQueue.completedCount()) + 1;
    if (gFeedback && gFeedbackCompleted)
        view.position = static_cast<int>(gQueue.completedCount());
    if (view.position > view.total) view.position = view.total;
    if (!gFeedback) {
        view.typed = reviewInput(part == kk::ReviewPart::Meaning);
        view.romaji = gIme.raw();
    } else {
        view.typed = gFeedbackInput;
        view.status = gCheck.status;
        view.message = gCheck.message;
        // A warning must not leak the answer through the feedback frame or the
        // detail card; the user is expected to fix the typed text instead.
        if (gCheck.status != kk::AnswerStatus::Warning)
            view.answer = correctAnswer(*s, part);
    }

    auto& d = target();
    d.startWrite();
    drawReviewScreen(d, gPack, view);
    d.endWrite();
    present();
}

bool hasRelatedSubjects(const kk::Subject& s) {
    return !s.visuallySimilarSubjectIds.empty() || !s.amalgamationSubjectIds.empty();
}

int detailPageCount(const kk::Subject& s) {
    return 5 + (s.type == kk::SubjectType::Vocabulary ? 1 : 0) + (hasRelatedSubjects(s) ? 1 : 0);
}

int detailPageKind(int index, const kk::Subject& s) {
    if (index < 5) return index;
    int next = 5;
    if (s.type == kk::SubjectType::Vocabulary) {
        if (index == next) return 5;
        ++next;
    }
    if (hasRelatedSubjects(s)) return 6;
    return 4;
}

void showReviewDetail() {
    if (!gDisp) return;
    uint32_t subjectId = gDetailSubjectId ? gDetailSubjectId : gFeedbackSubjectId;
    if (!subjectId && gQueue.current()) subjectId = gQueue.current()->subjectId;
    const kk::Subject* s = subjectById(subjectId);
    if (!s) return;

    ReviewDetailView view;
    view.subject = s;
    view.page = gReviewDetailPage;
    view.pages = gReviewDetailPages;
    view.kind = detailPageKind(gReviewDetailPage, *s);
    for (uint32_t id : s->componentSubjectIds) view.components.push_back(subjectById(id));
    for (uint32_t id : s->visuallySimilarSubjectIds) view.similar.push_back(subjectById(id));
    for (uint32_t id : s->amalgamationSubjectIds) view.amalgamations.push_back(subjectById(id));

    auto& d = target();
    d.startWrite();
    drawReviewDetailScreen(d, gPack, view);
    d.endWrite();
    present();
}

void reviewDetailKey(const KeyEvent& e) {
    if (!e.down) return;
    if (e.key == Key::Esc || e.key == Key::Tab) {
        gReviewDetail = false;
        if (gDetailFromCritical) {
            gDetailFromCritical = false;
            gDetailSubjectId = 0;
            gCriticalListActive = true;
        }
        gDirty = true;
        return;
    }
    if (e.key == Key::Right ||
        (e.key == Key::Other && tgc::cardputerCharAt(e.x, e.y, false) == ' ')) {
        if (gReviewDetailPage + 1 < gReviewDetailPages) ++gReviewDetailPage;
        gDirty = true;
    } else if (e.key == Key::Left || e.key == Key::Backspace) {
        if (gReviewDetailPage > 0) --gReviewDetailPage;
        gDirty = true;
    }
}

void showReviewSummary() {
    if (!gDisp) return;
    const kk::SessionStats stats = kk::summarizeSession(gQueue.completed());
    const kk::Subject* lastSubject =
        gCompletedItem.subjectId ? subjectById(gCompletedItem.subjectId) : nullptr;

    ReviewSummaryView view;
    view.reviews = static_cast<unsigned>(gQueue.completedCount());
    view.accuracy = static_cast<unsigned>(stats.accuracy);
    kk::formatSessionDuration(gReviewElapsedMs, view.duration, sizeof view.duration);
    view.stageUps = static_cast<unsigned>(gStageUps);
    view.mistakes = static_cast<unsigned>(stats.mistakes);
    view.pending = static_cast<unsigned>(kk::outboxPendingCount(gOutbox));
    view.attention = static_cast<unsigned>(kk::outboxAttentionCount(gOutbox));
    view.stageChangeValid = gStageChange.valid;
    view.stageFrom = gStageChange.from;
    view.stageTo = gStageChange.to;
    view.lastHasStats = lastSubject && lastSubject->hasReviewStats;
    view.practice = gPracticeActive;
    view.skipped = static_cast<unsigned>(gPracticeSkipped);
    if (lastSubject) {
        view.lastReviewCount = static_cast<unsigned>(lastSubject->reviewCount());
        view.lastPercentage = lastSubject->percentageCorrect;
    }

    auto& d = target();
    d.startWrite();
    drawReviewSummaryScreen(d, view);
    d.endWrite();
    present();
}

void saveOutbox() {
    if (!gFsOk) return;  // no storage: the in-memory outbox is all we have
    if (!kk::outboxSave(gOutbox)) Serial.println("[outbox] save failed");
}

bool outboxNeedsAttention(const kk::OutboxItem& item) {
    return item.state == kk::OutboxState::Rejected || item.state == kk::OutboxState::Stuck;
}

bool reconcileOutboxItem(kk::OutboxItem& item) {
    if (item.createdAt == 0) return false;
    const String token = kk::DevToken::load();
    String path = "/v2/assignments?ids=";
    path += String(static_cast<unsigned long>(item.assignmentId));
    const kk::WkResponse r = kk::wkGet(path, token, 4096, 12000);
    if (r.httpCode != 200) {
        Serial.printf("[outbox] reconcile %u -> HTTP %d\n",
                      static_cast<unsigned>(item.assignmentId), r.httpCode);
        return false;
    }
    kk::JsonParser p;
    kk::JsonValue v;
    if (!p.parse(r.body, &v)) return false;
    const std::vector<kk::Assignment> as = kk::parseAssignments(v);
    for (const kk::Assignment& a : as) {
        if (a.id != item.assignmentId) continue;
        if (a.dataUpdatedAt > item.createdAt && a.availableAt > item.createdAt) {
            kk::outboxRemove(gOutbox, item.seq);
            saveOutbox();
            markCacheCompleted(item.subjectId, item.assignmentId);
            Serial.printf("[outbox] %u was already accepted\n",
                          static_cast<unsigned>(item.assignmentId));
            return true;
        }
        break;
    }
    return false;
}

bool sendOutboxItem(uint32_t seq) {
    kk::OutboxItem* item = kk::outboxFind(gOutbox, seq);
    if (!item) return true;
    if (outboxNeedsAttention(*item)) return false;
    if (item->state == kk::OutboxState::Sending ||
        item->state == kk::OutboxState::Ambiguous) {
        if (reconcileOutboxItem(*item)) return true;
        item = kk::outboxFind(gOutbox, seq);
        if (!item) return true;
        if (kk::wkRateLimitWaitSeconds() > 0 || kk::wkAuthBlocked()) {
            // The GET was blocked by the shared 429/401 gate, so the POST may
            // have succeeded earlier; keep it ambiguous instead of resending.
            item->state = kk::OutboxState::Ambiguous;
            saveOutbox();
            if (kk::wkAuthBlocked()) gDeferredAuthIssue = SyncIssue::Auth;
            Serial.printf("[outbox] reconcile %u gate blocked; keeping ambiguous\n",
                          static_cast<unsigned>(item->assignmentId));
            return false;
        }
        item->state = kk::OutboxState::Pending;
        saveOutbox();
    }

    if (kk::outboxShouldGiveUp(item->attempts)) {
        item->state = kk::OutboxState::Stuck;
        saveOutbox();
        Serial.printf("[outbox] assignment %u stuck after %u attempts\n",
                      static_cast<unsigned>(item->assignmentId),
                      static_cast<unsigned>(item->attempts));
        return false;
    }

    item->state = kk::OutboxState::Sending;
    if (item->attempts < 255) ++item->attempts;
    saveOutbox();

    String body = "{\"review\":{\"assignment_id\":";
    body += String(static_cast<unsigned long>(item->assignmentId));
    body += ",\"incorrect_meaning_answers\":";
    body += String(static_cast<unsigned>(item->incorrectMeaning));
    body += ",\"incorrect_reading_answers\":";
    body += String(static_cast<unsigned>(item->incorrectReading));
    if (item->createdAt) {
        const std::string created = kk::formatIso8601(item->createdAt);
        body += ",\"created_at\":\"";
        body += created.c_str();
        body += "\"";
    }
    body += "}}";

    gFreeCanvasForRequest = true;
    const kk::WkResponse r = kk::wkPost("/v2/reviews", kk::DevToken::load(), body, 4096);
    gFreeCanvasForRequest = false;

    item = kk::outboxFind(gOutbox, seq);
    if (!item) return true;
    if (r.httpCode >= 200 && r.httpCode < 300) {
        const uint32_t assignmentId = item->assignmentId;
        const uint32_t subjectId = item->subjectId;
        const bool ownJournal = gPendingJournalHandle && item->seq == gPendingSeq;
        kk::JsonParser rp;
        kk::JsonValue rv;
        if (rp.parse(r.body, &rv)) {
            kk::ReviewUpdate update;
            if (kk::parseReviewUpdate(rv, &update)) {
                gStageChange.valid = true;
                gStageChange.to = update.srsStage;
                gStageChange.from =
                    update.assignmentId == gPendingStageAssignment ? gPendingStageFrom : 0;
                if (gStageChange.from && gStageChange.to > gStageChange.from) ++gStageUps;
                if (gSrs.valid && gStageChange.from && gStageChange.to)
                    gSrs.moveStage(gStageChange.from, gStageChange.to);
                if (ownJournal)
                    kk::journalPatchSrsAfter(gPendingJournalHandle, update.srsStage);
                Serial.printf("[review] assignment %u SRS %u -> %u\n",
                              static_cast<unsigned>(assignmentId),
                              static_cast<unsigned>(gStageChange.from),
                              static_cast<unsigned>(gStageChange.to));
            }
        }
        if (ownJournal) gPendingJournalHandle = 0;
        kk::outboxRemove(gOutbox, seq);
        saveOutbox();
        markCacheCompleted(subjectId, assignmentId);
        Serial.printf("[outbox] sent assignment %u (%d)\n",
                      static_cast<unsigned>(assignmentId), r.httpCode);
        return true;
    }
    if (!r.connected)
        item->state = kk::OutboxState::Pending;  // never reached the server
    else if (r.httpCode == 0 || r.httpCode >= 500)
        item->state = kk::OutboxState::Ambiguous;  // may have been accepted
    else if (r.httpCode == 401) {
        item->state = kk::OutboxState::Pending;
        gDeferredAuthIssue = SyncIssue::Auth;
        setSyncIssue(401, true, 0, "Review");
        gOutboxAuthIssue = true;
    } else if (r.httpCode == 403) {
        item->state = kk::OutboxState::Pending;
        gDeferredAuthIssue = SyncIssue::Forbidden;
        setSyncIssue(403, true, 0, "Review");
        gOutboxAuthIssue = true;
    } else if (r.httpCode == 429)
        item->state = kk::OutboxState::Pending;
    else {
        item->state = kk::OutboxState::Rejected;
        item->httpCode = static_cast<uint16_t>(r.httpCode);
    }
    saveOutbox();
    Serial.printf("[outbox] assignment %u -> HTTP %d, state %u\n",
                  static_cast<unsigned>(item->assignmentId), r.httpCode,
                  static_cast<unsigned>(item->state));
    return false;
}

bool queueReviewLocal(const kk::SessionItem& item) {
    gPendingStageAssignment = item.assignmentId;
    gPendingStageFrom = item.srsStage;
    uint32_t created = static_cast<uint32_t>(time(nullptr));
    if (created < 1700000000u) created = 0;  // clock not trusted; let the server stamp it
    const uint32_t seq = kk::outboxAdd(gOutbox, item.assignmentId, item.subjectId,
                                       item.incorrectMeaning, item.incorrectReading, created);
    if (!seq) {
        // A session starts only with enough free slots (startReview), so this
        // is a safety net: report loudly, never drop the review silently.
        Serial.println("[outbox] full; review not queued");
        snprintf(gNetLine, sizeof gNetLine, "Outbox full; review not saved");
        gPendingSeq = 0;
        return false;
    }
    gPendingSeq = seq;
    gPendingJournalHandle = 0;
    if (gFsOk && kk::journalOpen(currentAccountKey())) {
        kk::JournalRecord record;
        record.completedAt = created;
        record.subjectId = item.subjectId;
        record.assignmentId = item.assignmentId;
        record.srsBefore = item.srsStage;
        record.offline = !kk::DevWifi::connected();
        record.incorrectMeaning =
            static_cast<uint8_t>(std::min<uint16_t>(item.incorrectMeaning, 255));
        record.incorrectReading =
            static_cast<uint8_t>(std::min<uint16_t>(item.incorrectReading, 255));
        gPendingJournalHandle = kk::journalAppend(record);
    }
    saveOutbox();
    Serial.printf("[outbox] queued seq %u assignment %u\n", static_cast<unsigned>(seq),
                  static_cast<unsigned>(item.assignmentId));
    return true;
}

void sendQueuedReview(uint32_t seq) {
    if (seq) sendOutboxItem(seq);
}

void flushOutbox() {
    if (gOutbox.empty()) {
        clearOutboxAuthIssue();
        return;
    }
    Serial.printf("[outbox] flushing %u\n", static_cast<unsigned>(gOutbox.size()));
    std::vector<uint32_t> seqs;
    for (const kk::OutboxItem& it : gOutbox)
        if (!outboxNeedsAttention(it)) seqs.push_back(it.seq);
    for (uint32_t seq : seqs) {
        if (!sendOutboxItem(seq)) {
            const kk::OutboxItem* it = kk::outboxFind(gOutbox, seq);
            if (!it || outboxNeedsAttention(*it)) continue;
            break;
        }
    }
    if (kk::outboxPendingCount(gOutbox) == 0) clearOutboxAuthIssue();
    Serial.printf("[outbox] %u pending, %u need attention (%u stuck)\n",
                  static_cast<unsigned>(kk::outboxPendingCount(gOutbox)),
                  static_cast<unsigned>(kk::outboxAttentionCount(gOutbox)),
                  static_cast<unsigned>(kk::outboxStuckCount(gOutbox)));
}

std::vector<const kk::OutboxItem*> attentionOutboxItems() {
    std::vector<const kk::OutboxItem*> items;
    for (const kk::OutboxItem& it : gOutbox)
        if (outboxNeedsAttention(it)) items.push_back(&it);
    return items;
}

const kk::OutboxItem* selectedOutboxItem() {
    const kk::OutboxItem* selected = kk::outboxFind(gOutbox, gOutboxSelectedSeq);
    if (selected && outboxNeedsAttention(*selected)) return selected;
    for (const kk::OutboxItem& it : gOutbox) {
        if (!outboxNeedsAttention(it)) continue;
        gOutboxSelectedSeq = it.seq;
        return &it;
    }
    gOutboxSelectedSeq = 0;
    return nullptr;
}

std::string outboxItemLabel(const kk::OutboxItem& item) {
    const kk::Subject* subject = subjectById(item.subjectId);
    if (subject) {
        const std::string meaning = subjectMainMeaning(*subject);
        if (!meaning.empty()) {
            if (subject->level) {
                char level[12];
                snprintf(level, sizeof level, " L%u", static_cast<unsigned>(subject->level));
                return meaning + level;
            }
            return meaning;
        }
    }
    char fallback[32];
    if (item.assignmentId)
        snprintf(fallback, sizeof fallback, "assignment %u",
                 static_cast<unsigned>(item.assignmentId));
    else
        snprintf(fallback, sizeof fallback, "subject %u",
                 static_cast<unsigned>(item.subjectId));
    return fallback;
}

void outboxReasonShort(const kk::OutboxItem& item, char* out, size_t n) {
    if (item.state == kk::OutboxState::Stuck)
        snprintf(out, n, "gave up");
    else if (item.httpCode)
        snprintf(out, n, "HTTP %u", static_cast<unsigned>(item.httpCode));
    else
        snprintf(out, n, "rejected");
}

std::string outboxAgeLabel(const kk::OutboxItem& item, uint32_t now) {
    if (!item.createdAt || now < 1700000000u || now < item.createdAt) return "unknown";
    const uint32_t age = now - item.createdAt;
    char out[16];
    if (age < 60)
        snprintf(out, sizeof out, "just now");
    else if (age < 3600)
        snprintf(out, sizeof out, "%um", static_cast<unsigned>(age / 60));
    else if (age < 86400)
        snprintf(out, sizeof out, "%uh", static_cast<unsigned>(age / 3600));
    else
        snprintf(out, sizeof out, "%ud", static_cast<unsigned>(age / 86400));
    return out;
}

void outboxReasonDetail(const kk::OutboxItem& item, char* out, size_t n) {
    const std::string age =
        outboxAgeLabel(item, static_cast<uint32_t>(time(nullptr)));
    const unsigned attempts = item.attempts;
    const char* suffix = attempts == 1 ? "attempt" : "attempts";
    if (item.state == kk::OutboxState::Stuck) {
        snprintf(out, n, "Not sent after %u %s. Age %s.", attempts, suffix, age.c_str());
        return;
    }
    if (item.httpCode == 422)
        snprintf(out, n, "WaniKani rejected this review (HTTP 422). %u %s, age %s.", attempts,
                 suffix, age.c_str());
    else if (item.httpCode == 409)
        snprintf(out, n, "Conflict (409); it may have changed. %u %s, age %s.", attempts,
                 suffix, age.c_str());
    else if (item.httpCode)
        snprintf(out, n, "WaniKani rejected this review (HTTP %u). %u %s, age %s.",
                 static_cast<unsigned>(item.httpCode), attempts, suffix, age.c_str());
    else
        snprintf(out, n, "WaniKani rejected this review. %u %s, age %s.", attempts, suffix,
                 age.c_str());
}

void moveOutboxSelection(int delta) {
    const std::vector<const kk::OutboxItem*> items = attentionOutboxItems();
    if (items.empty()) return;
    int index = -1;
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i]->seq == gOutboxSelectedSeq) index = static_cast<int>(i);
    if (index < 0) index = 0;
    index = (index + delta + static_cast<int>(items.size())) %
            static_cast<int>(items.size());
    gOutboxSelectedSeq = items[static_cast<size_t>(index)]->seq;
    gDirty = true;
}

void openOutbox() {
    if (kk::outboxAttentionCount(gOutbox) == 0) return;
    gOutboxScreen = true;
    gOutboxConfirm = false;
    gOutboxSelectedSeq = 0;
    gOutboxNote[0] = 0;
    selectedOutboxItem();
    gDirty = true;
}

void discardSelectedOutboxItem() {
    const kk::OutboxItem* selected = selectedOutboxItem();
    if (!selected) return;
    const uint32_t seq = selected->seq;
    const uint32_t assignmentId = selected->assignmentId;

    int index = 0;
    bool found = false;
    for (const kk::OutboxItem& it : gOutbox) {
        if (!outboxNeedsAttention(it)) continue;
        if (it.seq == seq) {
            found = true;
            break;
        }
        ++index;
    }
    if (!found) return;

    // Dismiss only after the new file is safely written. A failed save keeps
    // the in-memory item and shows a note instead of pretending it is gone.
    std::vector<kk::OutboxItem> next = gOutbox;
    if (!kk::outboxRemove(next, seq)) return;
    if (gFsOk && !kk::outboxSave(next)) {
        snprintf(gOutboxNote, sizeof gOutboxNote, "Could not save outbox");
        Serial.println("[outbox] discard save failed");
        gDirty = true;
        return;
    }

    std::vector<uint32_t> remaining;
    for (const kk::OutboxItem& it : next)
        if (outboxNeedsAttention(it)) remaining.push_back(it.seq);

    gOutbox = std::move(next);
    gOutboxConfirm = false;
    gOutboxNote[0] = 0;
    if (remaining.empty()) {
        gOutboxScreen = false;
        gOutboxSelectedSeq = 0;
        gShowHome = true;
    } else {
        if (index >= static_cast<int>(remaining.size()))
            index = static_cast<int>(remaining.size()) - 1;
        gOutboxSelectedSeq = remaining[static_cast<size_t>(index)];
    }
    Serial.printf("[outbox] discarded seq %u assignment %u\n", static_cast<unsigned>(seq),
                  static_cast<unsigned>(assignmentId));
    gDirty = true;
}

void journalPractice(const kk::SessionItem& item) {
    if (!gFsOk || !kk::journalOpen(currentAccountKey())) return;
    kk::JournalRecord record;
    uint32_t created = static_cast<uint32_t>(time(nullptr));
    record.completedAt = created >= 1700000000u ? created : 0;
    record.subjectId = item.subjectId;
    record.assignmentId = item.assignmentId;
    record.srsBefore = item.srsStage;
    record.practice = true;
    record.offline = !kk::DevWifi::connected();
    record.incorrectMeaning =
        static_cast<uint8_t>(std::min<uint16_t>(item.incorrectMeaning, 255));
    record.incorrectReading =
        static_cast<uint8_t>(std::min<uint16_t>(item.incorrectReading, 255));
    kk::journalAppend(record);
}

void checkCurrent() {
    const kk::SessionItem* item = gQueue.current();
    if (!item) return;
    const kk::Subject* s = subjectById(item->subjectId);
    if (!s) return;
    const bool meaning = gQueue.part() == kk::ReviewPart::Meaning;
    const std::string input = reviewInput(meaning);
    if (input.empty()) return;
    gFeedbackSubjectId = item->subjectId;
    gFeedbackPart = gQueue.part();
    gFeedbackInput = input;
    gCheck = kk::checkAnswer(*s, meaning ? kk::AnswerKind::Meaning : kk::AnswerKind::Reading,
                             input, true);
    gFeedback = true;
    gFeedbackCompleted = false;
    if (gCheck.status != kk::AnswerStatus::Warning) {
        const kk::AnswerOutcome o = gQueue.answer(gCheck.status);
        if (o.completed) {
            gCompletedItem = gQueue.completed().back();
            gFeedbackCompleted = true;
            if (gPracticeActive) {
                gNeedsSubmit = false;
                journalPractice(gCompletedItem);
            } else {
                // Queue and persist before the feedback is drawn: a crash,
                // power loss or an OTA install on the feedback screen keeps
                // the review.
                gNeedsSubmit = queueReviewLocal(gCompletedItem);
                if (gDueReviews > 0) --gDueReviews;
                markCacheCompleted(gCompletedItem.subjectId, gCompletedItem.assignmentId);
                if (!gNeedsSubmit) {
                    // Unreachable when startReview() checked the free slots;
                    // show the failure instead of silently losing the review.
                    gFeedback = false;
                    gReviewActive = false;
                    gReviewSummary = false;
                    gShowHome = false;
                    snprintf(gNoticeLine, sizeof gNoticeLine, "Review not saved; outbox full.");
                    gNoticeLine2[0] = 0;
                    gNoticeAction = NoticeAction::OutboxFull;
                    gNoticeActive = true;
                }
            }
        }
    }
    gDirty = true;
}

void advanceFeedback() {
    if (gCheck.status == kk::AnswerStatus::Warning) {
        // Keep the typed text (meaning string and IME raw) so Enter returns to
        // the same question ready for a correction.
        gFeedback = false;
        gDirty = true;
        return;
    }
    if (gNeedsSubmit) {
        sendQueuedReview(gPendingSeq);
        gPendingSeq = 0;
        gNeedsSubmit = false;
    }
    gReviewDetail = false;
    gFeedback = false;
    gFeedbackCompleted = false;
    clearReviewInput();
    if (gQueue.empty()) {
        gReviewElapsedMs = millis() - gReviewStartMs;
        gReviewActive = false;
        gReviewSummary = true;
    }
    gDirty = true;
}

void endReview() {
    if (gNeedsSubmit) {
        sendQueuedReview(gPendingSeq);
        gPendingSeq = 0;
        gNeedsSubmit = false;
    }
    gPauseActive = false;
    gSettingsActive = false;
    gAboutActive = false;
    gSignOutConfirm = false;
    gLessonEmptyActive = false;
    gLessonListActive = false;
    gLessonQuizActive = false;
    gLowBatteryPrompt = false;
    gLowBatteryConfirmed = false;
    gReviewActive = false;
    gReviewSummary = false;
    gReviewDetail = false;
    gShowHome = true;
    gFeedback = false;
    gFeedbackCompleted = false;
    gQueue = kk::ReviewQueue{};
    gPracticeActive = false;
    gPracticeMode = 0;
    gPracticeSubjects.clear();
    gPracticeSkipped = 0;
    gStudyMenuActive = false;
    gCriticalListActive = false;
    gDetailFromCritical = false;
    gDetailSubjectId = 0;
    // Home does not need the batch's subjects; the assignments stay on disk for
    // the next session.
    evictCachedBatch(true);
    gDirty = true;
}

bool lowBatteryNow() {
    const int battery = tgc::board::batteryPercent();
    return battery >= 0 && battery < 20 && !tgc::board::externalPower();
}

void requestLowBatteryConfirm(int action) {
    gLowBatteryPrompt = true;
    gLowBatteryAction = action;
    gLowBatteryConfirmed = false;
    gDirty = true;
}

void startReview() {
    if (gFsFormatPrompt) return;  // wait for the storage choice
    if (!gHaveSummary) return;
    gLessonQuizActive = false;
    gPracticeActive = false;
    gPracticeSubjects.clear();
    gPracticeSkipped = 0;
    gDetailFromCritical = false;
    gDetailSubjectId = 0;
    gReviewDetail = false;
    gReviewDetailPage = 0;
    gStageChange = StageChange{};
    gPendingStageAssignment = 0;
    gPendingStageFrom = 0;
    gStageUps = 0;
    gReviewElapsedMs = 0;
    if (!gLowBatteryConfirmed && lowBatteryNow()) {
        requestLowBatteryConfirm(1);
        return;
    }
    gLowBatteryConfirmed = false;
    // Create/open the journal before the session so the first completion does
    // not pay for a new 65 KB file between the answer and its feedback.
    if (gFsOk) kk::journalOpen(currentAccountKey());
    const size_t reviewLimit = static_cast<size_t>(kk::settingsReviewBatchSize());
    if (outboxFreeSlots() < reviewLimit) {
        snprintf(gNoticeLine, sizeof gNoticeLine, "Sync to send pending reviews first.");
        gNoticeLine2[0] = 0;
        gNoticeAction = NoticeAction::OutboxFull;
        gNoticeActive = true;
        gShowHome = false;
        gDirty = true;
        return;
    }
    if (gBatch.assignments.empty() && gBatchEvicted) loadCachedBatch();
    if (gBatch.assignments.empty()) {
        char subtitle[48];
        const uint32_t dueNow = gSummary.dueReviews(effectiveNow());
        snprintf(subtitle, sizeof subtitle, "%u due items, please wait",
                 static_cast<unsigned>(dueNow));
        drawLoading("Fetching reviews...", subtitle);
        flushOutbox();
        if (!runBatch()) {
            if (gBatch.httpCode == 401 || gBatch.httpCode == 403)
                setSyncIssue(gBatch.httpCode, true, 0, "Reviews");
            else if (kk::wkAuthBlocked())
                setSyncIssue(401, true, 0, "Reviews");
            snprintf(gNoticeLine, sizeof gNoticeLine, "%s", gNetLine);
            gNoticeLine2[0] = 0;
            gNoticeAction = NoticeAction::ReviewError;
            gNoticeActive = true;
            gShowHome = false;
            gDirty = true;
            return;
        }
    }
    if (kk::wkAuthBlocked()) {
        // Optional study materials/statistics/components can arm the gate after
        // the main batch succeeded; do not start a session with a rejected token.
        setSyncIssue(401, true, 0, "Reviews");
        snprintf(gNoticeLine, sizeof gNoticeLine, "%s", gNetLine);
        gNoticeLine2[0] = 0;
        gNoticeAction = NoticeAction::ReviewError;
        gNoticeActive = true;
        gShowHome = false;
        gDirty = true;
        return;
    }
    drawLoading("Starting session...", "");
    const uint32_t now = effectiveNow();
    std::vector<kk::Assignment> due;
    for (const kk::Assignment& a : gBatch.assignments) {
        // Radicals with no Unicode character are valid review items: their
        // prompt comes from the radical bitmap in the visual pack.
        if (a.availableAt != 0 && a.availableAt > now) continue;
        if (outboxHasAssignment(a.id)) continue;
        if (subjectById(a.subjectId)) due.push_back(a);
    }
    gQueue.setMeaningFirst(kk::settingsMeaningFirst());
    gQueue.setLowerLevelsFirst(gHaveUser &&
                               gUser.reviewsOrder == kk::ReviewOrder::LowerLevelsFirst);
    if (kk::settingsCriticalFirst()) {
        loadCritical();
        std::vector<uint32_t> ids;
        ids.reserve(gCritical.size());
        for (const kk::CriticalItem& item : gCritical) ids.push_back(item.subjectId);
        gQueue.setCriticalSubjectIds(ids);
    }
    gQueue.build(due, gBatch.subjects, now, reviewLimit);
    Serial.printf("[review] order %s, first levels",
                  gQueue.lowerLevelsFirst() ? "lower_levels_first" : "shuffled");
    for (size_t i = 0; i < gQueue.queue().size() && i < 8; ++i)
        Serial.printf(" %u", static_cast<unsigned>(gQueue.queue()[i].level));
    Serial.println();
    if (gQueue.empty()) {
        Serial.println("[review] no reviewable items");
        // Leave the "Starting session..." frame instead of freezing on it.
        snprintf(gNoticeLine, sizeof gNoticeLine, "Next review  %s", gNextReview);
        gNoticeLine2[0] = 0;
        gNoticeAction = NoticeAction::ReviewEmpty;
        gNoticeActive = true;
        gShowHome = false;
        gDirty = true;
        return;
    }
    gReviewActive = true;
    gReviewSummary = false;
    gReviewStartMs = millis();
    gShowHome = false;
    gPauseActive = false;
    gSettingsActive = false;
    gAboutActive = false;
    gSignOutConfirm = false;
    gFeedback = false;
    gFeedbackCompleted = false;
    gNeedsSubmit = false;
    gPendingSeq = 0;
    clearReviewInput();
    Serial.printf("[review] session with %u items\n", static_cast<unsigned>(gQueue.remaining()));
    for (size_t i = 0; i < gQueue.queue().size() && i < 5; ++i) {
        const kk::SessionItem& it = gQueue.queue()[i];
        const kk::Subject* s = subjectById(it.subjectId);
        Serial.printf("[review] queue %u %s\n", static_cast<unsigned>(it.subjectId),
                      s ? s->characters.c_str() : "?");
    }
    gDirty = true;
}

bool reviewKey(const KeyEvent& e) {
    if (!e.down) return false;
    if (gReviewDetail) {
        reviewDetailKey(e);
        return true;
    }
    if (gFeedback) {
        if (e.key == Key::Enter) advanceFeedback();
        else if (e.key == Key::Esc) openPause();
        else if (e.key == Key::Tab && gCheck.status != kk::AnswerStatus::Warning) {
            const kk::Subject* detail = subjectById(gFeedbackSubjectId);
            gReviewDetailPages = detail ? detailPageCount(*detail) : 5;
            gDetailSubjectId = 0;
            gDetailFromCritical = false;
            gReviewDetail = true;
            gReviewDetailPage = 0;
            gDirty = true;
        }
        return true;
    }
    if (e.key == Key::Enter) {
        checkCurrent();
        return true;
    }
    if (e.key == Key::Esc) {
        openPause();
        return true;
    }
    if (e.key == Key::Backspace) {
        if (gQueue.part() == kk::ReviewPart::Meaning) {
            if (!gMeaningInput.empty()) gMeaningInput.pop_back();
        }
#ifdef KANIKEY_DEV
        else if (!gDirectReading.empty()) {
            gDirectReading.pop_back();
        }
#endif
        else {
            gIme.backspace();
        }
        gDirty = true;
        return true;
    }
    const char c = tgc::cardputerCharAt(e.x, e.y, gShift);
    if (c) {
        if (gQueue.part() == kk::ReviewPart::Meaning) {
            gMeaningInput.push_back(c);
        } else {
#ifdef KANIKEY_DEV
            gDirectReading.clear();
#endif
            gIme.feed(c);
        }
        gDirty = true;
    }
    return true;
}

const kk::Subject* lessonSubjectById(uint32_t id) {
    for (const kk::Subject& s : gLessonBatch.subjects)
        if (s.id == id) return &s;
    return nullptr;
}

const kk::Subject* lessonComponentById(uint32_t id) {
    for (const kk::Subject& s : gLessonComponents)
        if (s.id == id) return &s;
    return nullptr;
}

const kk::Subject* currentLessonSubject() {
    if (!gLessonActive || gLessonIndex >= gLessonBatch.assignments.size()) return nullptr;
    return lessonSubjectById(gLessonBatch.assignments[gLessonIndex].subjectId);
}

std::vector<std::string> wrapLines(lgfx::LovyanGFX& d, const std::string& text, int width) {
    d.setTextFont(0);
    d.setTextSize(1);
    std::vector<std::string> lines;
    std::string cur;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == ' ') ++i;
        size_t j = i;
        while (j < text.size() && text[j] != ' ') ++j;
        if (j == i) break;
        const std::string word = text.substr(i, j - i);
        const std::string cand = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && d.textWidth(cand.c_str()) > width) {
            lines.push_back(cur);
            cur = word;
        } else {
            cur = cand;
        }
        i = j;
    }
    if (!cur.empty()) lines.push_back(cur);
    if (lines.empty()) lines.emplace_back();
    return lines;
}

std::string subjectMainMeaning(const kk::Subject& s) {
    for (const kk::Meaning& m : s.meanings)
        if (m.accepted) return m.text;
    if (!s.meanings.empty()) return s.meanings[0].text;
    return std::string();
}

void showLessonList() {
    if (!gDisp) return;
    LessonListView view;
    view.selectedIndex = gLessonIndex;
    view.rows.reserve(gLessonBatch.assignments.size());
    for (const kk::Assignment& a : gLessonBatch.assignments) {
        const kk::Subject* s = lessonSubjectById(a.subjectId);
        LessonRowView row;
        row.subject = s;
        if (s) row.meaning = subjectMainMeaning(*s);
        view.rows.push_back(std::move(row));
    }

    auto& d = target();
    d.startWrite();
    drawLessonListScreen(d, gPack, view);
    d.endWrite();
    present();
}

void showLessonEmpty() {
    if (!gDisp) return;
    LessonEmptyView view;
    view.nextReview = gNextReview;

    auto& d = target();
    d.startWrite();
    drawLessonEmptyScreen(d, view);
    d.endWrite();
    present();
}

void showLesson() {
    if (!gDisp) return;
    const kk::Subject* s = currentLessonSubject();
    if (!s) return;
    auto& d = target();

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

    std::string mnemonic = s->meaningMnemonic;
    if (mnemonic.empty()) mnemonic = s->readingMnemonic;
    if (mnemonic.empty()) mnemonic = s->meaningHint;
    if (mnemonic.empty()) mnemonic = "No mnemonic cached.";
    std::string components;
    for (uint32_t id : s->componentSubjectIds) {
        const kk::Subject* c = lessonComponentById(id);
        if (!c) continue;
        const std::string componentMeaning = subjectMainMeaning(*c);
        if (componentMeaning.empty()) continue;
        if (!components.empty()) components += " + ";
        components += componentMeaning;
    }
    std::string extra;
    if (!components.empty()) extra += "Components: " + components + ". ";
    if (!s->meaningHint.empty()) extra += "Hint: " + s->meaningHint + ". ";
    if (!s->readingHint.empty()) extra += "Reading hint: " + s->readingHint + ". ";
    if (!s->meaningNote.empty()) extra += "Meaning note: " + s->meaningNote + ". ";
    if (!s->readingNote.empty()) extra += "Reading note: " + s->readingNote + ". ";
    if (!extra.empty()) mnemonic += " " + extra;
    std::vector<std::string> lines = wrapLines(d, mnemonic, 232);
    const size_t pages = (lines.size() + 2) / 3;
    if (gLessonPage >= pages) gLessonPage = pages - 1;

    LessonCardView view;
    view.subject = s;
    view.meaning = meaning;
    view.reading = reading;
    view.lines = std::move(lines);
    view.page = gLessonPage;
    view.pages = pages;
    view.index = gLessonIndex;
    view.total = gLessonBatch.assignments.size();

    d.startWrite();
    drawLessonCardScreen(d, gPack, view);
    d.endWrite();
    present();
}

void clearLessonQuizInput() {
    gLessonQuizInput.clear();
#ifdef KANIKEY_DEV
    gLessonQuizDirectReading.clear();
#endif
    gLessonQuizIme.clear();
}

std::string lessonQuizInput(bool meaning) {
    if (meaning) return gLessonQuizInput;
#ifdef KANIKEY_DEV
    if (!gLessonQuizDirectReading.empty()) return gLessonQuizDirectReading;
#endif
    return gLessonQuizIme.text();
}

void startLessonQuiz() {
    const kk::Subject* s = currentLessonSubject();
    if (!s) return;
    gLessonQuiz.start(*s);
    clearLessonQuizInput();
    gLessonQuizActive = true;
    gLessonActive = false;
    gDirty = true;
    Serial.printf("[lesson] quiz %u\n", static_cast<unsigned>(s->id));
}

void checkLessonQuiz() {
    if (!gLessonQuiz.active() || gLessonQuiz.feedback()) return;
    const bool meaning = gLessonQuiz.part() == kk::AnswerKind::Meaning;
    const std::string input = lessonQuizInput(meaning);
    if (input.empty()) return;
    gLessonQuiz.submit(input, true);
    const char* status = gLessonQuiz.result().status == kk::AnswerStatus::Correct ? "correct"
                         : gLessonQuiz.result().status == kk::AnswerStatus::Warning ? "warning"
                                                                                   : "incorrect";
    Serial.printf("[lesson] quiz %s %s\n", meaning ? "meaning" : "reading", status);
    gDirty = true;
}

void showLessonQuiz() {
    if (!gDisp) return;
    const kk::Subject& s = gLessonQuiz.subject();
    const bool meaning = gLessonQuiz.part() == kk::AnswerKind::Meaning;
    const int total = gLessonQuiz.needsReading() ? 2 : 1;
    int pos = 1;
    if (gLessonQuiz.complete())
        pos = total;
    else if (gLessonQuiz.part() == kk::AnswerKind::Reading)
        pos = 2;

    LessonQuizView view;
    view.subject = &s;
    view.complete = gLessonQuiz.complete();
    view.feedback = gLessonQuiz.feedback();
    view.meaning = meaning;
    view.mistakes = gLessonQuiz.mistakes();
    view.position = pos;
    view.total = total;
    if (view.feedback) {
        view.status = gLessonQuiz.result().status;
        view.message = gLessonQuiz.result().message;
    }
    if (!view.complete) {
        view.typed = lessonQuizInput(meaning);
        if (!view.feedback || view.status != kk::AnswerStatus::Warning)
            view.answer = correctAnswer(s, meaning ? kk::ReviewPart::Meaning
                                                   : kk::ReviewPart::Reading);
        view.romaji = gLessonQuizIme.raw();
    }

    auto& d = target();
    d.startWrite();
    drawLessonQuizScreen(d, gPack, view);
    d.endWrite();
    present();
}

void lessonQuizKey(const KeyEvent& e) {
    if (!e.down) return;
    if (e.key == Key::Esc) {
        gLessonQuizActive = false;
        gLessonActive = true;
        gDirty = true;
        return;
    }
    if (gLessonQuiz.complete()) {
        if (e.key == Key::Enter) {
            gLessonQuizActive = false;
            gLessonActive = true;
            gDirty = true;
        }
        return;
    }
    if (gLessonQuiz.feedback()) {
        if (e.key == Key::Enter) {
            const bool warning =
                gLessonQuiz.result().status == kk::AnswerStatus::Warning;
            gLessonQuiz.advance();
            // A warning retries the same part; keep the typed text so the user
            // can correct it instead of retyping.
            if (!warning) clearLessonQuizInput();
            gDirty = true;
        }
        return;
    }
    if (e.key == Key::Enter) {
        checkLessonQuiz();
        return;
    }
    if (e.key == Key::Backspace) {
        if (gLessonQuiz.part() == kk::AnswerKind::Meaning) {
            if (!gLessonQuizInput.empty()) gLessonQuizInput.pop_back();
        }
#ifdef KANIKEY_DEV
        else if (!gLessonQuizDirectReading.empty()) {
            gLessonQuizDirectReading.pop_back();
        }
#endif
        else {
            gLessonQuizIme.backspace();
        }
        gDirty = true;
        return;
    }
    const char c = tgc::cardputerCharAt(e.x, e.y, gShift);
    if (c) {
        if (gLessonQuiz.part() == kk::AnswerKind::Meaning)
            gLessonQuizInput.push_back(c);
        else {
#ifdef KANIKEY_DEV
            gLessonQuizDirectReading.clear();
#endif
            gLessonQuizIme.feed(c);
        }
        gDirty = true;
    }
}

void releaseLessonData() {
    gLessonBatch = kk::LessonBatch{};
    gLessonComponents.clear();
    gLessonIndex = 0;
    gLessonPage = 0;
}

void showLessonFetchError(int httpCode, const String& error) {
    Serial.printf("[lesson] failed: %s\n", error.c_str());
    if (error == "no due lessons" || error == "no lessons") {
        gLessonEmptyActive = true;
        gLessonListActive = false;
        gLessonActive = false;
        gShowHome = false;
        gDirty = true;
        return;
    }
    const char* lessonError = error.c_str();
    if (httpCode == 401 || httpCode == 403) {
        setSyncIssue(httpCode, true, 0, "Lessons");
        lessonError = gNetLine;
    } else if (kk::wkAuthBlocked()) {
        setSyncIssue(401, true, 0, "Lessons");
        lessonError = gNetLine;
    }
    drawLoading("No lessons available", lessonError);
    gShowHome = false;
    snprintf(gNoticeLine, sizeof gNoticeLine, "%s", lessonError);
    gNoticeLine2[0] = 0;
    gNoticeAction = NoticeAction::LessonError;
    gNoticeActive = true;
    gDirty = true;
}

void startLessons() {
    if (!gHaveSummary) return;
    gLessonQuizActive = false;
    if (gSummary.totalLessons() == 0) {
        gLessonEmptyActive = true;
        gLessonListActive = false;
        gLessonActive = false;
        gShowHome = false;
        gDirty = true;
        return;
    }
    if (!gLowBatteryConfirmed && lowBatteryNow()) {
        requestLowBatteryConfirm(2);
        return;
    }
    gLowBatteryConfirmed = false;
    // Home does not need the review batch while the user is in lessons; drop
    // its subjects before the lesson fetch so TLS keeps a healthy block.
    evictCachedBatch(true);
    gLessonComponents.clear();
    gLessonBatch = kk::LessonBatch{};
    gLessonEmptyActive = false;
    size_t lessonLimit = static_cast<size_t>(kk::settingsLessonBatchSize());
    if (gHaveUser && gUser.lessonsBatchSize > 0)
        lessonLimit = std::min(lessonLimit, static_cast<size_t>(gUser.lessonsBatchSize));
    if (lessonLimit == 0) lessonLimit = 1;
    // Sort a much larger candidate pool than a single batch: WaniKani orders
    // lessons by level and lesson_position, while the summary order is not
    // authoritative. Fetching the first ten only could hide a radical behind
    // ten vocabulary items and offer the wrong lesson first.
    const size_t candidateLimit = std::max<size_t>(lessonLimit, 60);
    const std::vector<uint32_t> candidateIds = gSummary.lessonSubjectIds(candidateLimit);
    char subtitle[48];
    snprintf(subtitle, sizeof subtitle, "up to %u, please wait",
             static_cast<unsigned>(candidateIds.size()));
    drawLoading("Fetching lessons...", subtitle);
    const String token = kk::DevToken::load();
    gFreeCanvasForRequest = true;
    std::vector<kk::Assignment> chosen;
    {
        const kk::LessonAssignments assignments =
            kk::fetchLessonAssignments(token, candidateIds, candidateLimit);
        if (!assignments.ok) {
            gFreeCanvasForRequest = false;
            showLessonFetchError(assignments.httpCode, assignments.error);
            return;
        }

        std::vector<uint32_t> subjectIds;
        subjectIds.reserve(assignments.assignments.size());
        for (const kk::Assignment& a : assignments.assignments)
            if (std::find(subjectIds.begin(), subjectIds.end(), a.subjectId) == subjectIds.end())
                subjectIds.push_back(a.subjectId);
        const kk::SubjectLevels levels = kk::fetchSubjectLevels(token, subjectIds);
        if (!levels.ok) {
            gFreeCanvasForRequest = false;
            showLessonFetchError(levels.httpCode, levels.error);
            return;
        }

        std::vector<kk::LessonCandidate> ordered;
        ordered.reserve(assignments.assignments.size());
        for (const kk::Assignment& a : assignments.assignments) {
            kk::LessonCandidate c;
            c.assignmentId = a.id;
            c.subjectId = a.subjectId;
            c.lessonPosition = a.lessonPosition;
            c.type = a.type;
            for (const kk::SubjectLevel& level : levels.levels)
                if (level.subjectId == a.subjectId) {
                    c.level = level.level;
                    // lesson_position belongs to the subject, not the
                    // assignment; prefer it over any assignment copy.
                    c.lessonPosition = level.lessonPosition;
                    break;
                }
            ordered.push_back(c);
        }
        kk::sortLessonCandidates(&ordered);
        if (ordered.size() > lessonLimit) ordered.resize(lessonLimit);
        Serial.printf("[lesson] candidates %u, chosen %u\n",
                      static_cast<unsigned>(assignments.assignments.size()),
                      static_cast<unsigned>(ordered.size()));

        chosen.reserve(ordered.size());
        for (const kk::LessonCandidate& c : ordered) {
            for (const kk::Assignment& a : assignments.assignments)
                if (a.id == c.assignmentId) {
                    chosen.push_back(a);
                    break;
                }
        }
    }
    gLessonBatch = kk::fetchLessonDetails(token, std::move(chosen));
    gFreeCanvasForRequest = false;
    if (!gLessonBatch.ok) {
        showLessonFetchError(gLessonBatch.httpCode, gLessonBatch.error);
        return;
    }
    gLessonComponents.clear();
    std::vector<uint32_t> componentIds;
    for (const kk::Assignment& a : gLessonBatch.assignments) {
        const kk::Subject* s = lessonSubjectById(a.subjectId);
        if (!s) continue;
        for (uint32_t id : s->componentSubjectIds) {
            if (std::find(componentIds.begin(), componentIds.end(), id) == componentIds.end())
                componentIds.push_back(id);
        }
    }
    if (!componentIds.empty()) {
        drawLoading("Loading components...", "lesson detail");
        gFreeCanvasForRequest = true;
        gLessonComponents =
            kk::fetchSubjectsByIds(kk::DevToken::load(), componentIds, 10);
        gFreeCanvasForRequest = false;
    }
    if (kk::wkAuthBlocked()) {
        // An optional materials/component request rejected the token; stop
        // before showing a lesson list that cannot be started.
        setSyncIssue(401, true, 0, "Lessons");
        drawLoading("No lessons available", gNetLine);
        gShowHome = false;
        snprintf(gNoticeLine, sizeof gNoticeLine, "%s", gNetLine);
        gNoticeLine2[0] = 0;
        gNoticeAction = NoticeAction::LessonError;
        gNoticeActive = true;
        gDirty = true;
        return;
    }
    gLessonListActive = true;
    gLessonActive = false;
    gShowHome = false;
    gLessonIndex = 0;
    gLessonPage = 0;
    Serial.printf("[lesson] list %u assignments, %u subjects\n",
                  static_cast<unsigned>(gLessonBatch.assignments.size()),
                  static_cast<unsigned>(gLessonBatch.subjects.size()));
    gDirty = true;
}

void startCurrentLesson() {
    if (!gLessonActive || gLessonIndex >= gLessonBatch.assignments.size()) return;
    const kk::Assignment& a = gLessonBatch.assignments[gLessonIndex];
    const kk::Subject* s = lessonSubjectById(a.subjectId);
    const std::string subtitle = s ? subjectMainMeaning(*s) : std::string();
    drawLoading("Starting lesson...", subtitle.c_str());
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    int httpCode;
#ifdef KANIKEY_DEV
    if (gDevLessonStartCode >= 0) {
        httpCode = gDevLessonStartCode;
        Serial.printf("[lesson] dev start code %d\n", httpCode);
    } else
#endif
    {
        gFreeCanvasForRequest = true;
        httpCode = kk::startAssignment(kk::DevToken::load(), a.id,
                                       now >= 1700000000u ? now : 0);
        gFreeCanvasForRequest = false;
    }
    if ((httpCode >= 200 && httpCode < 300) || httpCode == 422) {
        // 422 means the item was already started on WaniKani; either way the
        // card is done and the summary's lesson bucket is stale.
        gSummary.removeLessonSubject(a.subjectId);
        if (gLessons > 0) --gLessons;
        saveCacheSnapshot();
        ++gLessonIndex;
        gLessonPage = 0;
        if (gLessonIndex >= gLessonBatch.assignments.size()) {
            gLessonActive = false;
            gShowHome = true;
            releaseLessonData();
        }
    } else if (httpCode == 401 || httpCode == 403) {
        setSyncIssue(httpCode, true, 0, "Lesson");
        drawLoading("Could not start lesson", gNetLine);
        gLessonActive = false;
        gLessonListActive = false;
        gShowHome = true;
        releaseLessonData();
    } else {
        // Keep the card (gLessonActive, gLessonIndex) so Enter can retry.
        if (httpCode == 0)
            snprintf(gNoticeLine, sizeof gNoticeLine, "No connection");
        else
            snprintf(gNoticeLine, sizeof gNoticeLine, "HTTP %d", httpCode);
        gNoticeLine2[0] = 0;
        gNoticeAction = NoticeAction::LessonStart;
        gNoticeActive = true;
        gShowHome = false;
    }
    gDirty = true;
}

void lessonListKey(const KeyEvent& e) {
    if (!e.down) return;
    const size_t total = gLessonBatch.assignments.size();
    if (total == 0) {
        gLessonListActive = false;
        gShowHome = true;
        releaseLessonData();
        gDirty = true;
        return;
    }
    if (e.key == Key::Esc) {
        gLessonListActive = false;
        gShowHome = true;
        releaseLessonData();
        gDirty = true;
        return;
    }
    if (e.key == Key::Up) {
        gLessonIndex = gLessonIndex == 0 ? total - 1 : gLessonIndex - 1;
        gDirty = true;
    } else if (e.key == Key::Down) {
        gLessonIndex = (gLessonIndex + 1) % total;
        gDirty = true;
    } else if (e.key == Key::Enter) {
        if (gLessonIndex >= total) gLessonIndex = 0;
        gLessonQuizActive = false;
        gLessonListActive = false;
        gLessonActive = true;
        gLessonPage = 0;
        gDirty = true;
    }
}

void lessonKey(const KeyEvent& e) {
    if (!e.down) return;
    if (e.key == Key::Enter) {
        startCurrentLesson();
        return;
    }
    if (e.key == Key::Other) {
        const char c = tgc::cardputerCharAt(e.x, e.y, gShift);
        if (c == 'q' || c == 'Q') {
            startLessonQuiz();
            return;
        }
    }
    if (e.key == Key::Esc) {
        gLessonActive = false;
        if (!gLessonBatch.assignments.empty() && gLessonIndex < gLessonBatch.assignments.size())
            gLessonListActive = true;
        else {
            gShowHome = true;
            releaseLessonData();
        }
        gDirty = true;
        return;
    }
    if (e.key == Key::Right || (e.key == Key::Other && tgc::cardputerCharAt(e.x, e.y, false) == ' ')) {
        ++gLessonPage;
        gDirty = true;
    } else if (e.key == Key::Left || e.key == Key::Backspace) {
        if (gLessonPage > 0) --gLessonPage;
        gDirty = true;
    }
}

constexpr int kSettingsRows = 14;

void setSettingsNote(const char* text, uint16_t color = kFaint) {
    snprintf(gSettingsNote, sizeof gSettingsNote, "%s", text ? text : "");
    gSettingsNoteColor = color;
}

void openSettings(SettingsOrigin origin) {
    gSettingsActive = true;
    gAboutActive = false;
    gFirmwareActive = false;
    gSignOutConfirm = false;
    gPauseActive = false;
    gSettingsOrigin = origin;
    gSettingsIndex = 0;
    gSettingsNote[0] = 0;
    gShowHome = false;
    gDirty = true;
}

void closeSettings() {
    gSettingsActive = false;
    gAboutActive = false;
    gFirmwareActive = false;
    gSignOutConfirm = false;
    gFirmwareConfirm = false;
    gSettingsNote[0] = 0;
    if (gSettingsOrigin == SettingsOrigin::Pause)
        gPauseActive = true;
    else
        gShowHome = true;
    gDirty = true;
}

void openPause() {
    if (!gReviewActive) return;
    gPauseActive = true;
    gPauseIndex = 0;
    gSettingsActive = false;
    gAboutActive = false;
    gFirmwareActive = false;
    gSignOutConfirm = false;
    gDirty = true;
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
    switch (gSettingsIndex) {
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
        case 8:
            if (gReviewActive) {
                setSettingsNote("End the review session first", kWarn);
                break;
            }
            gSettingsActive = false;
            gShowHome = false;
            startSetupPortal();
            break;
        case 9:
            if (!gSdOk || !gPack.ready() || !kk::DevToken::has() || !kk::DevWifi::hasSaved()) {
                setSettingsNote("Configure Wi-Fi, token and pack", kWarn);
                break;
            }
            gSettingsActive = false;
            gShowHome = false;
            startSync();
            break;
        case 10:
            openFirmware();
            break;
        case 11:
            gAboutActive = true;
            gSettingsNote[0] = 0;
            break;
        case 12:
            if (gFsOk) kk::cacheClearSnapshot();
            if (gSdOk) kk::cacheClearSubjects(SD);
            gCacheSavedAt = 0;
            setSettingsNote("Cache cleared; next boot is cold", kOk);
            break;
        case 13:
            gSignOutConfirm = true;
            gSettingsNote[0] = 0;
            break;
    }
    gDirty = true;
}

void settingsKey(const KeyEvent& e) {
    if (!e.down) return;
    if (gSignOutConfirm) {
        if (e.key == Key::Esc) {
            gSignOutConfirm = false;
            gDirty = true;
        } else if (e.key == Key::Enter) {
            kk::DevToken::clear();
            kk::wkClearAuthBlock();
            gDeferredAuthIssue = SyncIssue::None;
            if (gFsOk) kk::cacheClearSnapshot();
            if (gSdOk) {
                kk::cacheClearSubjects(SD);
                kk::catalogClear(SD);
            }
            gOutbox.clear();
            saveOutbox();
            kk::journalClear();
            kk::criticalClear();
            gCritical.clear();
            gCriticalLoaded = false;
            gStatsHistoryLoaded = false;
            gPendingJournalHandle = 0;
            gUser = kk::User{};
            gSummary = kk::Summary{};
            gLevelProgress = kk::LevelProgress{};
            clearLevelUpNotice();
            gSrs = kk::SrsCounts{};
            gHaveUser = false;
            gHaveSummary = false;
            gBatch = kk::ReviewBatch{};
            gBatchEvicted = false;
            gCachedBatchCount = 0;
            gQueue = kk::ReviewQueue{};
            gDueReviews = 0;
            gLessons = 0;
            gCacheSavedAt = 0;
            gOffline = false;
            gSyncing = false;
            gSyncAttempted = false;
            gSyncIssue = SyncIssue::None;
            gOutboxAuthIssue = false;
            gReviewActive = false;
            gReviewSummary = false;
            gLessonQuizActive = false;
            gFeedback = false;
            gNeedsSubmit = false;
            gPauseActive = false;
            gSettingsActive = false;
            gAboutActive = false;
            gFirmwareActive = false;
            gSignOutConfirm = false;
            gLowBatteryPrompt = false;
            gLowBatteryConfirmed = false;
            gShowHome = false;
            snprintf(gSyncClock, sizeof gSyncClock, "--:--");
            snprintf(gNextReview, sizeof gNextReview, "--:--");
            snprintf(gNetLine, sizeof gNetLine, "Signed out");
            Serial.println("[setup] signed out; token, cache and outbox cleared");
            gDirty = true;
        }
        return;
    }
    if (e.key == Key::Esc) {
        if (gAboutActive) {
            gAboutActive = false;
            gSettingsNote[0] = 0;
        } else {
            closeSettings();
        }
        gDirty = true;
        return;
    }
    if (gAboutActive) return;
    if (e.key == Key::Up) {
        gSettingsIndex = (gSettingsIndex + kSettingsRows - 1) % kSettingsRows;
        gSettingsNote[0] = 0;
        gDirty = true;
    } else if (e.key == Key::Down) {
        gSettingsIndex = (gSettingsIndex + 1) % kSettingsRows;
        gSettingsNote[0] = 0;
        gDirty = true;
    } else if (e.key == Key::Enter) {
        settingsAction();
    }
}

void pauseKey(const KeyEvent& e) {
    if (!e.down) return;
    if (e.key == Key::Esc) {
        gPauseActive = false;
        gDirty = true;
        return;
    }
    if (e.key == Key::Up) {
        gPauseIndex = (gPauseIndex + 2) % 3;
        gDirty = true;
    } else if (e.key == Key::Down) {
        gPauseIndex = (gPauseIndex + 1) % 3;
        gDirty = true;
    } else if (e.key == Key::Enter) {
        if (gPauseIndex == 0) {
            gPauseActive = false;
        } else if (gPauseIndex == 1) {
            gPauseActive = false;
            endReview();
        } else {
            gPauseActive = false;
            openSettings(SettingsOrigin::Pause);
        }
        gDirty = true;
    }
}

void outboxKey(const KeyEvent& e) {
    if (!e.down) return;
    if (gOutboxConfirm) {
        if (e.key == Key::Esc) {
            gOutboxConfirm = false;
            gOutboxNote[0] = 0;
            gDirty = true;
        } else if (e.key == Key::Enter) {
            discardSelectedOutboxItem();
        }
        return;
    }
    if (e.key == Key::Esc) {
        gOutboxScreen = false;
        gOutboxConfirm = false;
        gOutboxNote[0] = 0;
        gShowHome = true;
        gDirty = true;
        return;
    }
    if (e.key == Key::Up) {
        gOutboxNote[0] = 0;
        moveOutboxSelection(-1);
    } else if (e.key == Key::Down) {
        gOutboxNote[0] = 0;
        moveOutboxSelection(1);
    } else if (e.key == Key::Enter) {
        if (selectedOutboxItem()) {
            gOutboxConfirm = true;
            gOutboxNote[0] = 0;
            gDirty = true;
        }
    } else if (e.key == Key::Other) {
        const char c = tgc::cardputerCharAt(e.x, e.y, gShift);
        if ((c == 'd' || c == 'D') && selectedOutboxItem()) {
            gOutboxConfirm = true;
            gOutboxNote[0] = 0;
            gDirty = true;
        }
    }
}

void syncScreenKey(const KeyEvent& e) {
    if (!e.down) return;
    if (!gSyncFinished) {
        if (e.key == Key::Esc) gSyncCancelRequested = true;
        return;
    }
    if (e.key == Key::Enter) {
        if (gSyncIssue == SyncIssue::Auth || gSyncIssue == SyncIssue::Forbidden) {
            gSyncScreen = false;
            gShowHome = false;
            startSetupPortal();
        } else {
            startSync();
        }
        return;
    }
    if (e.key == Key::Esc) {
        gSyncScreen = false;
        gShowHome = gHaveSummary;
        gDirty = true;
    }
}

void onKey(const KeyEvent& e) {
#ifdef KANIKEY_DEV
    if (gDevKeyTest) {
        Serial.printf("[key] test %s %s\n", e.key == Key::None ? "marker" : "event",
                      e.down ? "down" : "up");
        if (e.key == Key::None) gDevKeyTest = false;
    }
#endif
    updateModifier(e.x, e.y, e.down);
    if (gPortalActive) {
        if (e.down && e.key == Key::Esc) {
            gSetupPortal.end(false);
            gPortalActive = false;
            gShowHome = false;
            snprintf(gNetLine, sizeof gNetLine, "Setup cancelled");
            gDirty = true;
        }
        return;
    }
    if (gNoticeActive) {
        noticeKey(e);
        return;
    }
    if (gLowBatteryPrompt) {
        if (e.down && e.key == Key::Enter) {
            const int action = gLowBatteryAction;
            gLowBatteryPrompt = false;
            gLowBatteryAction = 0;
            gLowBatteryConfirmed = true;
            if (action == 1)
                startReview();
            else if (action == 2)
                startLessons();
            gDirty = true;
        } else if (e.down && e.key == Key::Esc) {
            gLowBatteryPrompt = false;
            gLowBatteryAction = 0;
            gLowBatteryConfirmed = false;
            gShowHome = true;
            gDirty = true;
        }
        return;
    }
    if (gSyncScreen) {
        syncScreenKey(e);
        return;
    }
    if (gOutboxScreen) {
        outboxKey(e);
        return;
    }
    if (gStatsActive) {
        statsKey(e);
        return;
    }
    if (gReviewDetail) {
        reviewDetailKey(e);
        return;
    }
    if (gStudyMenuActive) {
        studyMenuKey(e);
        return;
    }
    if (gCriticalListActive) {
        criticalListKey(e);
        return;
    }
    if (gFirmwareActive) {
        firmwareKey(e);
        return;
    }
    if (gSettingsActive) {
        settingsKey(e);
        return;
    }
    if (gPauseActive) {
        pauseKey(e);
        return;
    }
#ifdef KANIKEY_DEV
    if (gShowRadicalTest) {
        if (e.down && e.key == Key::Esc) {
            gShowRadicalTest = false;
            gDirty = true;
        }
        return;
    }
#endif
    if (gLessonEmptyActive) {
        if (e.down && (e.key == Key::Enter || e.key == Key::Esc)) {
            gLessonEmptyActive = false;
            gShowHome = true;
            gDirty = true;
        }
        return;
    }
    if (gLessonListActive) {
        lessonListKey(e);
        return;
    }
    if (gLessonQuizActive) {
        lessonQuizKey(e);
        return;
    }
    if (gLessonActive) {
        lessonKey(e);
        return;
    }
    if (gReviewActive) {
        reviewKey(e);
        return;
    }
    if (gReviewSummary) {
        if (e.down && (e.key == Key::Enter || e.key == Key::Esc)) endReview();
        return;
    }
#ifdef KANIKEY_DEV
    if (e.down && e.key == Key::Tab) {
        gShowDebug = !gShowDebug;
        gDirty = true;
        return;
    }
#endif
    if (e.down && gShowHome && e.key == Key::Esc) {
        openSettings(SettingsOrigin::Home);
        return;
    }
    if (e.down && e.key == Key::Enter) {
        if (gShowHome && gHaveSummary) {
            if (gSummary.dueReviews(effectiveNow()) == 0) {
                // Nothing due: no network call, just the next time. Refresh it
                // first: answering the last due item leaves gNextReview at "now".
                applySummary(effectiveNow());
                snprintf(gNoticeLine, sizeof gNoticeLine, "Next review  %s", gNextReview);
                gNoticeLine2[0] = 0;
                gNoticeAction = NoticeAction::ReviewEmpty;
                gNoticeActive = true;
                gShowHome = false;
                gDirty = true;
            } else {
                startReview();
            }
        } else {
            retryBoot();
        }
        return;
    }
    if (e.down && gShowHome && e.key == Key::Other &&
        (tgc::cardputerCharAt(e.x, e.y, gShift) == 'o' ||
         tgc::cardputerCharAt(e.x, e.y, gShift) == 'O')) {
        openOutbox();
        return;
    }
    if (e.down && gShowHome && e.key == Key::Other &&
        (tgc::cardputerCharAt(e.x, e.y, gShift) == 'l' ||
         tgc::cardputerCharAt(e.x, e.y, gShift) == 'L')) {
        startLessons();
        return;
    }
    if (e.down && gShowHome && e.key == Key::Other &&
        (tgc::cardputerCharAt(e.x, e.y, gShift) == 't' ||
         tgc::cardputerCharAt(e.x, e.y, gShift) == 'T')) {
        openStats();
        return;
    }
    if (e.down && gShowHome && e.key == Key::Other &&
        (tgc::cardputerCharAt(e.x, e.y, gShift) == 'p' ||
         tgc::cardputerCharAt(e.x, e.y, gShift) == 'P')) {
        openStudyMenu();
        return;
    }
    if (e.down && gShowHome && !gSyncing && e.key == Key::Other &&
        (tgc::cardputerCharAt(e.x, e.y, gShift) == 's' ||
         tgc::cardputerCharAt(e.x, e.y, gShift) == 'S')) {
        startSync();
        return;
    }
    const char c = tgc::cardputerCharAt(e.x, e.y, gShift);

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
    gDirty = true;

    char cs[2] = {c, 0};
    Serial.printf("[key] %s code=%u grid=%u,%u key=%s char=%s shift=%d fn=%d ctrl=%d opt=%d "
                  "alt=%d\n",
                  e.down ? "down" : "up", static_cast<unsigned>(e.code),
                  static_cast<unsigned>(e.x), static_cast<unsigned>(e.y), keyName(e.key),
                  c ? cs : "-", gShift, gFn, gCtrl, gOpt, gAlt);
}

bool mountSd() {
    tgc::board::SdPins pins;
    if (!tgc::board::sdPins(&pins)) {
        snprintf(gSdLine, sizeof gSdLine, "SD: no slot");
        return false;
    }
    if (pins.otherCs >= 0) {
        pinMode(pins.otherCs, OUTPUT);
        digitalWrite(pins.otherCs, HIGH);
    }
    // FSPI (SPI2): M5GFX drives the Cardputer panels through Arduino's HSPI
    // bus (SPI3), and sharing that bus's mutex deadlocked a card read made
    // inside a display transaction (the canvas-less "drawing directly" path).
    static SPIClass spi(FSPI);
    spi.begin(pins.sck, pins.miso, pins.mosi, pins.cs);
    const size_t heapBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const uint32_t t0 = millis();
    // Four handles: the glyph pack, one catalog/subject file, a directory and
    // one spare for the catalog scan. The old limit of two held only the pack
    // plus one file.
    if (!SD.begin(pins.cs, spi, 20000000, "/sd", 4)) {
        snprintf(gSdLine, sizeof gSdLine, "SD: no card / not FAT32");
        Serial.printf("[sd] mount failed in %u ms, heap %u -> %u\n",
                      static_cast<unsigned>(millis() - t0),
                      static_cast<unsigned>(heapBefore),
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
        return false;
    }
    const uint64_t mb = SD.cardSize() / (1024ull * 1024ull);
    snprintf(gSdLine, sizeof gSdLine, "SD: %llu MB", static_cast<unsigned long long>(mb));
    Serial.printf("[sd] mounted in %u ms, %llu MB, heap %u -> %u, largest %u\n",
                  static_cast<unsigned>(millis() - t0), static_cast<unsigned long long>(mb),
                  static_cast<unsigned>(heapBefore),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                         MALLOC_CAP_8BIT)));
    return true;
}

void localHm(uint32_t epoch, char* out, size_t n) {
    const time_t t = static_cast<time_t>(epoch);
    struct tm tm {};
    if (!localtime_r(&t, &tm)) {
        snprintf(out, n, "--:--");
        return;
    }
    snprintf(out, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

void localDate(uint32_t epoch, char* out, size_t n) {
    if (epoch < 1700000000u) {
        if (n) out[0] = '\0';
        return;
    }
    const time_t t = static_cast<time_t>(epoch);
    struct tm tm {};
    if (!localtime_r(&t, &tm) || !std::strftime(out, n, "%d %b", &tm)) {
        if (n) out[0] = '\0';
    }
}

void drawLoading(const char* title, const char* subtitle) {
    if (!gDisp) return;
    kk::app::LoadingView view;
    view.title = title;
    view.subtitle = subtitle;
    auto& d = target();
    d.startWrite();
    kk::app::drawLoadingScreen(d, view);
    d.endWrite();
    present();
}

void resetSyncScreen() {
    static const char* kLabels[kSyncRowCount] = {"Wi-Fi", "Outbox", "Account", "Summary",
                                                 "Level", "SRS",    "Critical"};
    for (int i = 0; i < kSyncRowCount; ++i) {
        gSyncRows[i].label = kLabels[i];
        gSyncRows[i].value[0] = '\0';
        gSyncRows[i].state = SyncRowState::Pending;
    }
    gSyncCompleted = 0;
    gSyncFinished = false;
    gSyncCancelRequested = false;
    gSyncScreen = true;
}

void syncRowText(int row, const char* text) {
    if (row < 0 || row >= kSyncRowCount) return;
    snprintf(gSyncRows[row].value, sizeof gSyncRows[row].value, "%s", text ? text : "");
}

void syncRowBegin(int row, const char* text) {
    if (row < 0 || row >= kSyncRowCount) return;
    syncRowText(row, text);
    gSyncRows[row].state = SyncRowState::Active;
    drawSync();
}

void syncRowDone(int row, const char* text) {
    if (row < 0 || row >= kSyncRowCount) return;
    syncRowText(row, text);
    gSyncRows[row].state = SyncRowState::Done;
    if (gSyncCompleted < kSyncRowCount) ++gSyncCompleted;
    drawSync();
}

void syncRowWarn(int row, const char* text) {
    if (row < 0 || row >= kSyncRowCount) return;
    syncRowText(row, text);
    gSyncRows[row].state = SyncRowState::Warn;
    if (gSyncCompleted < kSyncRowCount) ++gSyncCompleted;
    drawSync();
}

void syncRowFail(int row, const char* text) {
    if (row < 0 || row >= kSyncRowCount) return;
    syncRowText(row, text);
    gSyncRows[row].state = SyncRowState::Failed;
    gSyncFinished = true;
    gSyncing = false;
    gLastInputAt = millis();
    gDirty = true;
    Serial.printf("[sync] %s failed: %s\n", gSyncRows[row].label, gNetLine);
    drawSync();
}

// Pump the keyboard between blocking requests; Esc is the only action.
bool syncStopRequested() {
    if (gSyncCancelRequested) return true;
    tgc::board::update();
    KeyEvent e;
    while (tgc::board::nextKeyEvent(&e)) {
        gLastInputAt = millis();
        updateModifier(e.x, e.y, e.down);
        if (e.down && e.key == Key::Esc) gSyncCancelRequested = true;
    }
    return gSyncCancelRequested;
}

void finishSyncCancelled() {
    gSyncFinished = true;
    gSyncScreen = false;
    gSyncing = false;
    gSyncAttempted = false;
    gSyncIssue = SyncIssue::None;
    gOutboxAuthIssue = false;
    snprintf(gNetLine, sizeof gNetLine, "Sync cancelled");
    Serial.println("[sync] cancelled");
    gShowHome = gHaveSummary;
    gLastInputAt = millis();
    gDirty = true;
}

void startSync() {
    if (gFsFormatPrompt) return;  // wait for the storage choice
    if (!gSdOk || !gPack.ready() || !kk::DevToken::has() || !kk::DevWifi::hasSaved()) {
        gSyncScreen = false;
        gDirty = true;
        return;
    }
    gSyncing = true;
    gSyncAttempted = true;
    gSyncIssue = SyncIssue::None;
    gOutboxAuthIssue = false;
    resetSyncScreen();
    drawSync();

    // Wi-Fi (the only step shown before the first API call).
    if (!kk::DevWifi::connected()) {
        const String ssid = kk::DevWifi::savedSsid();
        syncRowBegin(SyncWifi, ssid.c_str());
        snprintf(gNetLine, sizeof gNetLine, "NET: joining %s", ssid.c_str());
        if (!kk::DevWifi::connect(30000)) {
            gSyncIssue = SyncIssue::Network;
            snprintf(gNetLine, sizeof gNetLine, "NET: join failed");
            syncRowFail(SyncWifi, "join failed");
            return;
        }
    } else {
        syncRowBegin(SyncWifi, kk::DevWifi::ip().c_str());
    }
    snprintf(gNetLine, sizeof gNetLine, "NET: %s", kk::DevWifi::ip().c_str());
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
    const size_t beforePending = kk::outboxPendingCount(gOutbox);
    const size_t beforeAttention = kk::outboxAttentionCount(gOutbox);
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
    const size_t afterPending = kk::outboxPendingCount(gOutbox);
    const size_t afterAttention = kk::outboxAttentionCount(gOutbox);
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
    if (gSyncIssue != SyncIssue::None) {
        syncRowFail(SyncAccount, gNetLine);
        return;
    }
    char account[40];
    snprintf(account, sizeof account, "%s L%d", gUser.username.c_str(), gUser.level);
    syncRowDone(SyncAccount, account);
    if (syncStopRequested()) {
        finishSyncCancelled();
        return;
    }

    syncRowBegin(SyncSummary, "fetching");
    runSummary();
    if (gSyncIssue != SyncIssue::None) {
        syncRowFail(SyncSummary, gNetLine);
        return;
    }
    char summary[40];
    snprintf(summary, sizeof summary, "%u due, %u lessons",
             static_cast<unsigned>(gDueReviews), static_cast<unsigned>(gLessons));
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
    } else if (gLevelProgress.valid) {
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
                 static_cast<unsigned>(gSrs.apprentice), static_cast<unsigned>(gSrs.guru),
                 static_cast<unsigned>(gSrs.master), static_cast<unsigned>(gSrs.enlightened),
                 static_cast<unsigned>(gSrs.burned));
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
        if (gCritical.empty())
            snprintf(critical, sizeof critical, "none");
        else
            snprintf(critical, sizeof critical, "%u critical items",
                     static_cast<unsigned>(gCritical.size()));
        syncRowDone(SyncCritical, critical);
    } else {
        syncRowWarn(SyncCritical, "unavailable");
    }
    if (syncStopRequested()) {
        finishSyncCancelled();
        return;
    }

    applyDeferredAuthIssue();
    gSyncing = false;
    gSyncFinished = true;
    gSyncScreen = false;
    Serial.println("[sync] complete");
    firmwareAfterSync();
    gLastInputAt = millis();
    gDirty = true;
}

bool portalValidateToken(const String& token, String* username, int* level, String* error) {
    if (!kk::DevWifi::connected()) {
        if (error) *error = "Wi-Fi is not connected yet.";
        return false;
    }
    const bool tokenChanged = kk::DevToken::load() != token;
    kk::wkClearAuthBlock();
    gFreeCanvasForRequest = true;
    const kk::WkProbeResult r = kk::probeWaniKaniUser(token);
    gFreeCanvasForRequest = false;
    if (r.parsed) {
        if (tokenChanged) {
            // A new token must not inherit the previous account's level as the
            // baseline for a level-up notice.
            clearLevelUpNotice();
            gHaveUser = false;
        }
        gDeferredAuthIssue = SyncIssue::None;
        gSyncIssue = SyncIssue::None;
        gOutboxAuthIssue = false;
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
    if (gPack.ready()) return true;
    if (!gSdOk) gSdOk = mountSd();
    if (!gSdOk) return false;
    // Opening inside the handler is unsafe and the CRC takes ~3.6 s. loop()
    // does it with the CRC after the response, so the next page load shows the
    // new status.
    gPackRecheckPending = true;
    return false;
}

String portalPackStatus() { return String(gPack.statusLine()); }

void portalSwitchToStation() {
    const String ssid = kk::DevWifi::savedSsid();
    gSetupPortal.end(false);
    gPortalActive = false;
    drawLoading("Connecting Wi-Fi...", ssid.c_str());
    if (!kk::DevWifi::connect(30000)) {
        const String notice = "Could not join \"" + ssid + "\". Check the password and try again.";
        gPortalActive = gSetupPortal.beginAp(gSetupHooks, notice);
        gDirty = true;
        return;
    }
    const bool ok = gSetupPortal.beginStation(
        gSetupHooks, "Wi-Fi OK. Add the token, then Finish.");
    gPortalActive = ok;
    if (!ok) snprintf(gNetLine, sizeof gNetLine, "Setup portal failed");
    gDirty = true;
}

void portalFinish() {
    gSetupPortal.end(true);
    gPortalActive = false;
    gShowHome = false;
    drawLoading("Setup complete", "Syncing...");
    startSync();
    gDirty = true;
}

bool startSetupPortal() {
    if (gPortalActive) return true;
    if (gReviewActive || gLessonActive || gLessonListActive || gLessonEmptyActive) return false;
    kk::wkClearAuthBlock();
    tgc::board::setDisplayOn(true);
    tgc::board::setBrightness(kk::settingsBrightness());
    gDimmed = false;
    gScreenOff = false;
    gSetupHooks.validateToken = portalValidateToken;
    gSetupHooks.recheckPack = portalRecheckPack;
    gSetupHooks.packStatus = portalPackStatus;
    gSetupHooks.onSwitchToStation = portalSwitchToStation;
    gSetupHooks.onFinish = portalFinish;
    gShowHome = false;
    gReviewSummary = false;
    gFeedback = false;
    gPauseActive = false;
    gSettingsActive = false;
    gAboutActive = false;
    gSignOutConfirm = false;
    gLessonEmptyActive = false;
    gLessonListActive = false;
    gLowBatteryPrompt = false;
    gLowBatteryConfirmed = false;

    // With a saved network, go straight to the station page so the user can
    // change the token without re-entering Wi-Fi. If it cannot join, fall back
    // to the setup access point.
    if (kk::DevWifi::hasSaved()) {
        if (!kk::DevWifi::connected()) {
            drawLoading("Connecting Wi-Fi...", kk::DevWifi::savedSsid().c_str());
            kk::DevWifi::connect(30000);
        }
        if (kk::DevWifi::connected() &&
            gSetupPortal.beginStation(gSetupHooks,
                                      "Enter the token, then press Finish.")) {
            gPortalActive = true;
            gLastInputAt = millis();
            gDirty = true;
            return true;
        }
    }

    drawLoading("Starting setup...", "Scanning Wi-Fi...");
    kk::DevWifi::disconnect();
    gPortalActive = gSetupPortal.beginAp(
        gSetupHooks, "Connect your phone to this setup network to configure KaniKey.");
    gLastInputAt = millis();
    gDirty = true;
    if (!gPortalActive) {
        snprintf(gNetLine, sizeof gNetLine, "Setup: AP failed");
        Serial.println("[setup] could not start the access point");
    }
    return gPortalActive;
}

void retryBoot() {
    if (gHaveSummary) {
        gShowHome = true;
        gDirty = true;
        return;
    }
    if (!gSdOk) {
        gSdOk = mountSd();
        if (gSdOk) {
            gPack.open(SD, "/kanikey/glyphs.kgp");
            Serial.printf("[pack] %s\n", gPack.statusLine());
        }
    }
    if (gSdOk && !gPack.ready()) {
        gPack.open(SD, "/kanikey/glyphs.kgp");
        Serial.printf("[pack] %s\n", gPack.statusLine());
    }
    if (gSdOk && gPack.ready() && !gHaveSummary && kk::DevToken::has() && loadCache()) {
        gDirty = true;
        return;
    }
    if (gSyncIssue == SyncIssue::Auth || gSyncIssue == SyncIssue::Forbidden) {
        startSetupPortal();
        return;
    }
    if (!kk::DevToken::has() || !kk::DevWifi::hasSaved()) {
        startSetupPortal();
        return;
    }
    if (!gSdOk) {
        snprintf(gNetLine, sizeof gNetLine, "SD: not mounted");
    } else if (!gPack.ready()) {
        snprintf(gNetLine, sizeof gNetLine, "Pack: %s", gPack.statusLine());
    } else if (!kk::DevToken::has()) {
        snprintf(gNetLine, sizeof gNetLine, "Token: not configured");
    } else if (!kk::DevWifi::hasSaved()) {
        snprintf(gNetLine, sizeof gNetLine, "Wi-Fi: not configured");
    } else {
        startSync();
        return;
    }
    gDirty = true;
}

uint32_t cacheAgeSeconds() {
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    if (gCacheSavedAt == 0 || now < 1700000000u || now < gCacheSavedAt) return 0;
    return now - gCacheSavedAt;
}

bool cacheIsStale() { return cacheAgeSeconds() >= 24u * 3600u; }

void cacheStatusLine(char* out, size_t n) {
    const uint32_t age = cacheAgeSeconds();
    if (age == 0) {
        snprintf(out, n, "Offline - cached data");
        return;
    }
    if (age >= 24u * 3600u)
        snprintf(out, n, "Stale cache - %ud old", static_cast<unsigned>(age / 86400));
    else if (age < 60)
        snprintf(out, n, "Offline - just synced");
    else if (age < 3600)
        snprintf(out, n, "Offline - %um old", static_cast<unsigned>(age / 60));
    else
        snprintf(out, n, "Offline - %uh old", static_cast<unsigned>(age / 3600));
}

kk::app::HomeView buildHomeView() {
    kk::app::HomeView view;
    view.batteryPercent = tgc::board::batteryPercent();
    view.signedIn = gHaveUser;
    view.level = gHaveUser ? gUser.level : 0;
    view.username = gHaveUser ? gUser.username.c_str() : "";
    view.dueReviews = gDueReviews;
    view.lessons = gLessons;
    view.nextReview = gNextReview;
    view.syncClock = gSyncClock;
    view.offline = gOffline;
    view.srs = gSrs;
    view.outboxAttention = static_cast<uint32_t>(kk::outboxAttentionCount(gOutbox));
    view.outboxPending = static_cast<uint32_t>(kk::outboxPendingCount(gOutbox));
    // "Nearly full" starts at 96 of 128 slots; every state occupies a slot.
    view.outboxNearlyFull = gOutbox.size() >= (kk::kOutboxMax * 3u) / 4u;

    // Status priority: API/auth, outbox, offline cache, then notices.
    if (gSyncIssue == SyncIssue::Auth) {
        snprintf(view.statusText, sizeof view.statusText, "Auth: token rejected");
        view.statusColor = kErr;
    } else if (gSyncIssue == SyncIssue::Forbidden) {
        snprintf(view.statusText, sizeof view.statusText, "Auth: missing permissions");
        view.statusColor = kErr;
    } else if (gSyncIssue == SyncIssue::RateLimit) {
        view.statusColor = kWarn;
        const uint32_t wait = kk::wkRateLimitWaitSeconds();
        if (wait)
            snprintf(view.statusText, sizeof view.statusText, "Rate limited; retry in %us",
                     static_cast<unsigned>(wait));
        else
            snprintf(view.statusText, sizeof view.statusText, "Rate limited; retry later");
    } else if (gSyncIssue == SyncIssue::Server || gSyncIssue == SyncIssue::Other) {
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
    } else if (!gFsOk) {
        snprintf(view.statusText, sizeof view.statusText, "Storage unavailable");
        view.statusColor = kErr;
    } else if (gOffline) {
        cacheStatusLine(view.statusText, sizeof view.statusText);
        view.statusColor = cacheIsStale() ? kErr : kWarn;
    } else if (gLevelUpNotice) {
        snprintf(view.statusText, sizeof view.statusText, "Level up! L%u -> L%u",
                 static_cast<unsigned>(gLevelUpFrom), static_cast<unsigned>(gLevelUpTo));
        view.statusColor = kOk;
        view.levelUp = true;
    } else if (gHaveUser && gUser.vacationStartedAt) {
        snprintf(view.statusText, sizeof view.statusText, "Vacation mode");
        view.statusColor = kWarn;
    } else if (gLevelProgress.valid) {
        formatLevelProgressLine(view.levelLine, sizeof view.levelLine);
    }
    return view;
}

void drawHome() {
    if (!gDisp) return;
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

void openStats() {
    gStatsActive = true;
    gStatsPage = 0;
    gStatsHistoryLoaded = false;
    gShowHome = false;
    gDirty = true;
}

void statsKey(const KeyEvent& e) {
    if (!e.down) return;
    if (e.key == Key::Esc) {
        gStatsActive = false;
        gShowHome = true;
        gDirty = true;
        return;
    }
    if (e.key == Key::Left && gStatsPage > 0) {
        --gStatsPage;
        gDirty = true;
    } else if (e.key == Key::Right && gStatsPage < 3) {
        ++gStatsPage;
        gDirty = true;
    }
}

kk::app::StatsView buildStatsView() {
    kk::app::StatsView view;
    view.page = static_cast<uint8_t>(gStatsPage);
    view.pages = 4;
    const uint32_t now = effectiveNow();
    kk::buildForecast(gSummary, now, &view.forecast);
    for (int i = 0; i < 4; ++i)
        localHm(view.forecast.startHour + static_cast<uint32_t>(i) * 6u * 3600u, view.labels[i],
                sizeof view.labels[i]);
    view.srs = gSrs;

    view.level.valid = gLevelProgress.valid;
    view.level.level =
        gLevelProgress.valid ? gLevelProgress.level : (gHaveUser ? gUser.level : 0);
    const uint32_t since =
        gLevelProgress.unlockedAt ? gLevelProgress.unlockedAt : gLevelProgress.startedAt;
    if (since && now > since) {
        const uint32_t days = (now - since) / 86400u;
        snprintf(view.level.days, sizeof view.level.days, "%u day%s", static_cast<unsigned>(days),
                 days == 1 ? "" : "s");
    }
    view.level.kanjiPassed = gLevelProgress.kanjiPassed;
    view.level.kanjiTotal = gLevelProgress.kanjiTotal;
    view.level.kanjiPercent =
        kk::levelKanjiPercent(gLevelProgress.kanjiPassed, gLevelProgress.kanjiTotal);
    view.level.needed =
        kk::kanjiNeededForNinetyPercent(gLevelProgress.kanjiPassed, gLevelProgress.kanjiTotal);
    view.level.ninetyReached = gLevelProgress.kanjiTotal > 0 && view.level.needed == 0;
    view.level.apprentice = gSrs.apprentice;
    view.level.srsValid = gSrs.valid;

    if (gStatsPage == 3) {
        if (!gStatsHistoryLoaded) loadStatsHistory();
        view.history = gStatsHistory;
    }
    return view;
}

void drawStats() {
    if (!gDisp) return;
    const kk::app::StatsView view = buildStatsView();
    auto& d = target();
    d.startWrite();
    kk::app::drawStatsScreen(d, view);
    d.endWrite();
    present();
}

struct MistakeEntry {
    uint32_t subjectId = 0;
    uint32_t at = 0;
};
struct MistakeCollector {
    uint32_t cutoff = 0;
    std::vector<MistakeEntry> entries;
};

bool collectMistake(const kk::JournalRecord& record, void* user) {
    MistakeCollector* collector = static_cast<MistakeCollector*>(user);
    if (!collector || record.practice || record.completedAt < collector->cutoff) return true;
    if (record.incorrectMeaning + record.incorrectReading == 0) return true;
    collector->entries.push_back({record.subjectId, record.completedAt});
    return true;
}

std::vector<uint32_t> recentMistakeIds(size_t limit, size_t* total) {
    std::vector<uint32_t> ids;
    if (total) *total = 0;
    if (!gFsOk || !kk::journalOpen(currentAccountKey(), false)) return ids;
    const uint32_t now = effectiveNow();
    if (now < 86400u) return ids;
    MistakeCollector collector;
    collector.cutoff = now - 86400u;  // S4 default: last 24 h
    kk::journalForEach(collectMistake, &collector);
    std::sort(collector.entries.begin(), collector.entries.end(),
              [](const MistakeEntry& a, const MistakeEntry& b) { return a.at > b.at; });
    for (const MistakeEntry& entry : collector.entries) {
        if (std::find(ids.begin(), ids.end(), entry.subjectId) != ids.end()) continue;
        ids.push_back(entry.subjectId);
        if (total) ++*total;
        if (limit && ids.size() >= limit) break;
    }
    return ids;
}

bool resolvePracticeSubjects(const std::vector<uint32_t>& ids, std::vector<kk::Subject>* out,
                             std::vector<uint32_t>* missing) {
    out->clear();
    missing->clear();
    const bool catalogOk = gSdOk && kk::catalogOpen(SD, currentAccountKey());
    for (uint32_t id : ids) {
        kk::Subject subject;
        if (catalogOk && kk::catalogLoad(SD, id, &subject))
            out->push_back(std::move(subject));
        else
            missing->push_back(id);
    }
    if (!missing->empty() && kk::DevWifi::connected() && kk::DevToken::has()) {
        const std::vector<kk::Subject> fetched =
            kk::fetchSubjectsByIds(kk::DevToken::load(), *missing, 5);
        for (const kk::Subject& subject : fetched) {
            out->push_back(subject);
            if (catalogOk) kk::catalogStore(SD, subject.id, subject);
        }
        for (const kk::Subject& subject : fetched)
            missing->erase(std::remove(missing->begin(), missing->end(), subject.id),
                           missing->end());
    }
    return !out->empty();
}

void refreshStudyCounts() {
    size_t mistakes = 0;
    recentMistakeIds(999, &mistakes);
    snprintf(gStudyCounts[0], sizeof gStudyCounts[0], "%u", static_cast<unsigned>(mistakes));
    snprintf(gStudyCounts[1], sizeof gStudyCounts[1], "online");
    loadCritical();
    snprintf(gStudyCounts[2], sizeof gStudyCounts[2], "%u",
             static_cast<unsigned>(gCritical.size()));
    snprintf(gStudyCounts[3], sizeof gStudyCounts[3], "online");
}

void openStudyMenu() {
    gStudyMenuActive = true;
    gStudyIndex = 0;
    gStudyNote[0] = 0;
    gShowHome = false;
    refreshStudyCounts();
    gDirty = true;
}

void drawStudyMenu() {
    if (!gDisp) return;
    kk::app::StudyMenuView view;
    view.selectedIndex = gStudyIndex;
    for (int i = 0; i < kk::app::kStudyRows; ++i)
        snprintf(view.counts[i], sizeof view.counts[i], "%s", gStudyCounts[i]);
    snprintf(view.note, sizeof view.note, "%s", gStudyNote);
    auto& d = target();
    d.startWrite();
    kk::app::drawStudyMenuScreen(d, view);
    d.endWrite();
    present();
}

void studyMenuKey(const KeyEvent& e) {
    if (!e.down) return;
    if (e.key == Key::Esc) {
        gStudyMenuActive = false;
        gShowHome = true;
        gDirty = true;
        return;
    }
    if (e.key == Key::Up)
        gStudyIndex = (gStudyIndex + kk::app::kStudyRows - 1) % kk::app::kStudyRows;
    else if (e.key == Key::Down)
        gStudyIndex = (gStudyIndex + 1) % kk::app::kStudyRows;
    else if (e.key == Key::Enter) {
        if (gStudyIndex == 2)
            openCriticalList();
        else
            startPractice(gStudyIndex);
    }
    gDirty = true;
}

void openCriticalList() {
    loadCritical();
    gCriticalSubjects.clear();
    gCriticalRows.clear();
    std::vector<uint32_t> ids;
    ids.reserve(gCritical.size());
    for (const kk::CriticalItem& item : gCritical) ids.push_back(item.subjectId);
    std::vector<uint32_t> missing;
    if (!ids.empty()) {
        drawLoading("Preparing critical items...", "");
        gFreeCanvasForRequest = true;
        resolvePracticeSubjects(ids, &gCriticalSubjects, &missing);
        gFreeCanvasForRequest = false;
    }
    for (const kk::CriticalItem& item : gCritical) {
        const kk::Subject* subject = nullptr;
        for (const kk::Subject& candidate : gCriticalSubjects)
            if (candidate.id == item.subjectId) {
                subject = &candidate;
                break;
            }
        if (!subject) continue;
        kk::app::CriticalRowView row;
        row.subject = subject;
        row.meaning = subjectMainMeaning(*subject);
        row.percentage = item.percentageCorrect;
        gCriticalRows.push_back(std::move(row));
    }
    gCriticalIndex = 0;
    gCriticalListActive = true;
    gStudyMenuActive = false;
    gShowHome = false;
    gDirty = true;
}

void drawCriticalList() {
    if (!gDisp) return;
    kk::app::CriticalListView view;
    view.rows = gCriticalRows;
    view.selectedIndex = gCriticalIndex;
    auto& d = target();
    d.startWrite();
    kk::app::drawCriticalListScreen(d, gPack, view);
    d.endWrite();
    present();
}

void criticalListKey(const KeyEvent& e) {
    if (!e.down) return;
    const int total = static_cast<int>(gCriticalRows.size());
    if (e.key == Key::Esc) {
        gCriticalListActive = false;
        gStudyMenuActive = true;
        gDirty = true;
        return;
    }
    if (total > 0) {
        if (e.key == Key::Up)
            gCriticalIndex = (gCriticalIndex + total - 1) % total;
        else if (e.key == Key::Down)
            gCriticalIndex = (gCriticalIndex + 1) % total;
        else if (e.key == Key::Enter) {
            const kk::Subject* s = gCriticalRows[static_cast<size_t>(gCriticalIndex)].subject;
            if (s) {
                gDetailSubjectId = s->id;
                gDetailFromCritical = true;
                gReviewDetailPage = 0;
                gReviewDetailPages = detailPageCount(*s);
                gReviewDetail = true;
                gCriticalListActive = false;
                gDirty = true;
            }
        } else if (e.key == Key::Other) {
            const char c = tgc::cardputerCharAt(e.x, e.y, gShift);
            if (c == 'p' || c == 'P') startPractice(2);
        }
    }
}

void startPractice(int mode) {
    static const char* kNames[4] = {"No recent mistakes", "No recent lessons",
                                    "No critical items", "Current level complete"};
    static const char* kLoading[4] = {"recent mistakes", "recent lessons", "critical items",
                                      "current level"};
    gStudyNote[0] = 0;
    gPracticeSkipped = 0;
    gPracticeSubjects.clear();
    gPracticeActive = false;
    std::vector<kk::Assignment> fetched;
    std::vector<uint32_t> ids;
    if (mode == 0) {
        size_t total = 0;
        ids = recentMistakeIds(10, &total);
    } else if (mode == 2) {
        loadCritical();
        for (const kk::CriticalItem& item : gCritical) {
            ids.push_back(item.subjectId);
            if (ids.size() >= 10) break;
        }
    } else {
        drawLoading("Preparing practice...", kLoading[mode]);
        gFreeCanvasForRequest = true;
        kk::AssignmentBatch batch = kk::fetchPracticeAssignments(
            kk::DevToken::load(), mode == 3 ? (gHaveUser ? gUser.level : 0) : 0, mode == 3);
        gFreeCanvasForRequest = false;
        if (!batch.ok) {
            snprintf(gStudyNote, sizeof gStudyNote, "Needs Wi-Fi for this mode");
            gDirty = true;
            return;
        }
        fetched = std::move(batch.assignments);
        if (fetched.size() > 10) fetched.resize(10);
        for (const kk::Assignment& a : fetched) ids.push_back(a.subjectId);
    }
    if (ids.empty()) {
        snprintf(gStudyNote, sizeof gStudyNote, "%s", kNames[mode]);
        gDirty = true;
        return;
    }
    drawLoading("Preparing practice...", kLoading[mode]);
    std::vector<uint32_t> missing;
    gFreeCanvasForRequest = true;
    resolvePracticeSubjects(ids, &gPracticeSubjects, &missing);
    gFreeCanvasForRequest = false;
    gPracticeSkipped = static_cast<uint32_t>(missing.size());
    if (gPracticeSubjects.empty()) {
        snprintf(gStudyNote, sizeof gStudyNote, "Nothing cached for this mode");
        gDirty = true;
        return;
    }

    const uint32_t now = effectiveNow();
    std::vector<kk::Assignment> practice;
    for (const kk::Assignment& source : fetched) {
        bool have = false;
        for (const kk::Subject& subject : gPracticeSubjects)
            if (subject.id == source.subjectId) {
                have = true;
                break;
            }
        if (!have) continue;
        kk::Assignment a = source;
        a.availableAt = now;
        a.hidden = false;
        a.pendingReview = false;
        practice.push_back(a);
        if (practice.size() >= 10) break;
    }
    if (practice.empty()) {
        for (const kk::Subject& subject : gPracticeSubjects) {
            kk::Assignment a;
            a.id = 0;
            a.subjectId = subject.id;
            a.type = subject.type;
            a.srsStage = 1;
            a.availableAt = now;
            practice.push_back(a);
            if (practice.size() >= 10) break;
        }
    }
    gQueue = kk::ReviewQueue{};
    gQueue.setMeaningFirst(kk::settingsMeaningFirst());
    gQueue.setLowerLevelsFirst(false);
    gQueue.build(practice, gPracticeSubjects, now, 10, esp_random());
    if (gQueue.empty()) {
        snprintf(gStudyNote, sizeof gStudyNote, "%s", kNames[mode]);
        gDirty = true;
        return;
    }
    gPracticeActive = true;
    gPracticeMode = mode;
    gStudyMenuActive = false;
    gCriticalListActive = false;
    gDetailFromCritical = false;
    gDetailSubjectId = 0;
    gShowHome = false;
    gReviewActive = true;
    gReviewSummary = false;
    gFeedback = false;
    gFeedbackCompleted = false;
    gReviewDetail = false;
    gReviewElapsedMs = 0;
    gReviewStartMs = millis();
    gStageChange = StageChange{};
    gStageUps = 0;
    gNeedsSubmit = false;
    gPendingSeq = 0;
    Serial.printf("[practice] mode %d, %u items, %u skipped\n", mode,
                  static_cast<unsigned>(gQueue.remaining()),
                  static_cast<unsigned>(gPracticeSkipped));
    gDirty = true;
}

void drawPause() {
    if (!gDisp) return;

    const int total = static_cast<int>(gQueue.completedCount() + gQueue.remaining());
    PauseView view;
    view.selectedIndex = gPauseIndex;
    view.completed = static_cast<unsigned>(gQueue.completedCount());
    view.total = static_cast<unsigned>(total);

    auto& d = target();
    d.startWrite();
    kk::app::drawPauseScreen(d, view);
    d.endWrite();
    present();
}

void drawSettings() {
    if (!gDisp) return;
    auto& d = target();

    static const char* kLabels[] = {"Brightness", "Dim after", "Blank after", "Sleep after",
                                    "Review batch", "Order", "Lesson batch", "Critical first",
                                    "Setup portal", "Sync now", "Firmware", "About", "Clear cache",
                                    "Sign out"};
    String values[kSettingsRows];
    values[0] = String(kk::settingsBrightness());
    values[1] = secondsLabel(kk::settingsDimSeconds());
    values[2] = secondsLabel(kk::settingsBlankSeconds());
    values[3] = secondsLabel(kk::settingsSleepSeconds());
    values[4] = String(kk::settingsReviewBatchSize());
    values[5] = kk::settingsMeaningFirst() ? "Meaning" : "Reading";
    values[6] = String(kk::settingsLessonBatchSize());
    values[7] = kk::settingsCriticalFirst() ? "On" : "Off";
    values[8] = "open";
    values[9] = "run";
    values[10] = gOtaState.badgeVersion.empty() ? kk::kFirmwareVersion
                                                : gOtaState.badgeVersion.c_str();
    values[11] = "info";
    values[12] = "clear";
    values[13] = "out";

    SettingsView view;
    view.selectedIndex = gSettingsIndex;
    view.note = gSettingsNote;
    view.noteColor = gSettingsNoteColor;
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

void drawOutbox() {
    if (!gDisp) return;
    auto& d = target();
    d.startWrite();

    OutboxView view;
    const std::vector<const kk::OutboxItem*> items = attentionOutboxItems();
    const kk::OutboxItem* selected = selectedOutboxItem();
    view.total = static_cast<unsigned>(kk::outboxAttentionCount(gOutbox));
    view.rows.reserve(items.size());
    if (!items.empty()) view.selectedIndex = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        const kk::OutboxItem& item = *items[i];
        OutboxRowView row;
        row.label = outboxItemLabel(item);
        char reason[16];
        outboxReasonShort(item, reason, sizeof reason);
        row.reason = reason;
        row.reasonKind = item.state == kk::OutboxState::Stuck
                             ? OutboxReasonKind::Warn
                             : (item.httpCode ? OutboxReasonKind::Err
                                              : OutboxReasonKind::Faint);
        view.rows.push_back(std::move(row));
        if (item.seq == gOutboxSelectedSeq) view.selectedIndex = static_cast<int>(i);
    }
    if (selected) {
        view.confirm = gOutboxConfirm;
        if (view.confirm) {
            view.confirmLabel = outboxItemLabel(*selected);
            char reason[128];
            outboxReasonDetail(*selected, reason, sizeof reason);
            view.confirmReason = reason;
        }
    }
    view.note = gOutboxNote;

    drawOutboxScreen(d, view);
    d.endWrite();
    present();
}

void drawLowBattery() {
    if (!gDisp) return;

    LowBatteryView view;
    view.batteryPercent = tgc::board::batteryPercent();
    view.lessonAction = gLowBatteryAction == 2;

    auto& d = target();
    d.startWrite();
    kk::app::drawLowBatteryScreen(d, view);
    d.endWrite();
    present();
}

void drawSignOutConfirm() {
    if (!gDisp) return;
    auto& d = target();
    d.startWrite();
    kk::app::drawSignOutScreen(d);
    d.endWrite();
    present();
}

// Blocking notice frame: the lessons empty state's shape with a caller-supplied
// title, line and key hint. Used for review empty/error states, a full outbox
// and the storage prompt.
void drawNotice() {
    if (!gDisp) return;
    const char* header = "REVIEWS";
    const char* title = "";
    const char* hint = "Enter: retry  Esc: home";
    switch (gNoticeAction) {
        case NoticeAction::ReviewEmpty:
            title = "No reviews now";
            hint = "Enter: sync  Esc: home";
            break;
        case NoticeAction::ReviewError:
            title = "Could not load reviews";
            break;
        case NoticeAction::LessonError:
            header = "LESSONS";
            title = "No lessons available";
            break;
        case NoticeAction::LessonStart:
            header = "LESSONS";
            title = "Could not start lesson";
            hint = "Enter: retry  Esc: home";
            break;
        case NoticeAction::OutboxFull:
            title = "Outbox full";
            hint = "Enter: sync  Esc: home";
            break;
        case NoticeAction::StorageFormat:
            header = "STORAGE";
            title = "Storage unreadable";
            hint = "Enter: format  Esc: continue";
            break;
        default:
            return;
    }

    auto& d = target();
    d.startWrite();
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString(header, 6, 3);
    d.setTextFont(4);
    d.setTextSize(1);
    d.setTextColor(ui::kText, ui::kBg);
    // "Could not start lesson" is wider than the screen in Font4.
    if (d.textWidth(title) > 228) d.setTextFont(2);
    d.drawString(title, 8, 38);
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString(gNoticeLine, 8, 82);
    if (gNoticeLine2[0]) d.drawString(gNoticeLine2, 8, 102);
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString(hint, 8, 125);
    d.endWrite();
    present();
}

void noticeKey(const KeyEvent& e) {
    if (!e.down) return;
    if (gFsFormatPrompt) {
        if (e.key == Key::Esc) {
            // Continue without storage: the in-memory outbox and cache only.
            gFsFormatPrompt = false;
            gFsOk = false;
            gNoticeActive = false;
            gNoticeAction = NoticeAction::None;
            snprintf(gNetLine, sizeof gNetLine, "Storage unavailable");
            Serial.println("[kk] continuing without storage");
            continueBootAfterStorage();
            return;
        }
        if (e.key == Key::Enter) {
            if (LittleFS.begin(true)) {
                gFsOk = true;
                gFsFormatPrompt = false;
                gNoticeActive = false;
                gNoticeAction = NoticeAction::None;
                kk::outboxLoad(gOutbox);
                Serial.printf("[outbox] pending %u\n",
                              static_cast<unsigned>(kk::outboxPendingCount(gOutbox)));
                continueBootAfterStorage();
            } else {
                snprintf(gNoticeLine, sizeof gNoticeLine, "Format failed; try again");
                gDirty = true;
            }
            return;
        }
        return;
    }
    if (e.key == Key::Esc) {
        gNoticeActive = false;
        if (gNoticeAction == NoticeAction::LessonStart) {
            // Leave the lesson like the 401 path does; the card is still the
            // current item until the summary is refreshed by a sync.
            gNoticeAction = NoticeAction::None;
            gLessonActive = false;
            gLessonListActive = false;
            gShowHome = true;
            releaseLessonData();
        } else {
            gShowHome = gHaveSummary;
        }
        gDirty = true;
        return;
    }
    if (e.key != Key::Enter) return;
    const NoticeAction action = gNoticeAction;
    gNoticeActive = false;
    if (action == NoticeAction::ReviewError) {
        startReview();
    } else if (action == NoticeAction::LessonError) {
        startLessons();
    } else if (action == NoticeAction::LessonStart) {
        // gLessonActive and gLessonIndex were kept, so this retries the same
        // card; the failure branch can raise the notice again.
        gNoticeAction = NoticeAction::None;
        startCurrentLesson();
    } else if (action == NoticeAction::OutboxFull || action == NoticeAction::ReviewEmpty) {
        startSync();
    }
    gDirty = true;
}

// ---------------------------------------------------------------- firmware

void formatBytes(uint32_t bytes, char* out, size_t n) {
    if (bytes >= 1024u * 1024u)
        snprintf(out, n, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    else
        snprintf(out, n, "%u KB", static_cast<unsigned>((bytes + 1023u) / 1024u));
}

void firmwareStatus(const char* text, uint16_t color) {
    snprintf(gFirmwareStatus, sizeof gFirmwareStatus, "%s", text ? text : "");
    gFirmwareStatusColor = color;
}

void firmwareNote(const char* text, uint16_t color) {
    snprintf(gFirmwareNote, sizeof gFirmwareNote, "%s", text ? text : "");
    gFirmwareNoteColor = color;
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

void firmwareBoot() {
    gOtaState = kk::ota::loadSavedState();
    const bool layout = kk::ota::layoutIsOurs();
    if (!layout) {
        Serial.printf("[ota] no OTA layout (running %s): USB updates only\n",
                      kk::ota::runningSlot());
    } else {
        Serial.printf("[ota] %s (%s) in %s; update slot %s, %u bytes\n", kk::kFirmwareVersion,
                      kk::ota::buildEnv(), kk::ota::runningSlot(), kk::ota::nextSlot(),
                      static_cast<unsigned>(kk::ota::slotBytes()));
    }
    if (!gOtaState.installing.empty()) {
        kk::ota::FirmwareVersion installed, running;
        bool same = gOtaState.installing == kk::kFirmwareVersion;
        if (!same && kk::ota::parseFirmwareVersion(gOtaState.installing, &installed) &&
            kk::ota::parseFirmwareVersion(kk::kFirmwareVersion, &running))
            same = kk::ota::compareFirmwareVersions(installed, running) == 0;
        if (same) {
            Serial.printf("[ota] %s installed over the air in %s\n",
                          gOtaState.installing.c_str(), kk::ota::runningSlot());
        } else {
            Serial.printf("[ota] %s did not start in %s: back on %s\n",
                          gOtaState.installing.c_str(), gOtaState.installSlot.c_str(),
                          kk::kFirmwareVersion);
            // Two-strike rule: one rollback may be a power cut, so the version
            // stays on offer; the second one blacklists it.
            const kk::ota::RollbackRecord record = kk::ota::loadRollbackRecord();
            const kk::ota::RollbackDecision d =
                kk::ota::rollbackDecision(record, gOtaState.installing);
            kk::ota::saveRollbackRecord(d.record);
            if (d.blacklist) {
                gOtaState.failedVersion = gOtaState.installing;
                kk::ota::saveFailedVersion(gOtaState.failedVersion);
                firmwareNote("Update failed twice; not offered", kWarn);
            } else {
                firmwareNote("Last update rolled back", kWarn);
            }
        }
        gOtaState.installing.clear();
        gOtaState.installSlot.clear();
        kk::ota::clearInstallRecord();
    }
    gOtaPending = layout && kk::ota::pendingVerify();
    if (gOtaPending) {
        gOtaLoopAt = millis();
        Serial.printf("[ota] first boot of %s: valid after an API success or 10 minutes\n",
                      kk::kFirmwareVersion);
    }
    // A badge for the running version (or for a failed one) is stale.
    if (!gOtaState.badgeVersion.empty()) {
        kk::ota::FirmwareVersion running, badge;
        const bool parsed = kk::ota::parseFirmwareVersion(kk::kFirmwareVersion, &running) &&
                            kk::ota::parseFirmwareVersion(gOtaState.badgeVersion, &badge);
        if ((parsed && kk::ota::compareFirmwareVersions(badge, running) <= 0) ||
            gOtaState.badgeVersion == gOtaState.failedVersion) {
            gOtaState.badgeVersion.clear();
            gOtaState.badgeBytes = 0;
            kk::ota::saveBadge("", 0);
        }
    }
    if (!gOtaState.badgeVersion.empty())
        Serial.printf("[ota] cached offer %s (%u bytes)\n", gOtaState.badgeVersion.c_str(),
                      static_cast<unsigned>(gOtaState.badgeBytes));
    if (!gOtaState.failedVersion.empty())
        Serial.printf("[ota] not offering %s again (rolled back)\n",
                      gOtaState.failedVersion.c_str());
}

// A pending image is valid after the first 2xx API response or 10 minutes of
// loop, whichever comes first. The time fallback keeps an offline device from
// staying in the verification window forever.
bool otaVerificationDue(uint32_t now) {
    if (!gOtaLoopAt) gOtaLoopAt = now ? now : 1;
    return kk::wkHadApiSuccess() || now - gOtaLoopAt >= 600000;
}

void otaMarkValidIfDue(uint32_t now, const char* when) {
    if (!gOtaPending || !otaVerificationDue(now)) return;
    const bool apiOk = kk::wkHadApiSuccess();
    if (!kk::ota::markValid()) return;
    gOtaPending = false;
    Serial.printf("[ota] %s marked valid %s (%s)\n", kk::kFirmwareVersion, when,
                  apiOk ? "API success" : "10 min loop");
    // A good image clears the two-strike record of its own version only.
    const kk::ota::RollbackRecord record = kk::ota::loadRollbackRecord();
    if (record.version[0] && std::strcmp(record.version, kk::kFirmwareVersion) == 0)
        kk::ota::clearRollbackRecord();
}

void firmwareTick(uint32_t now) { otaMarkValidIfDue(now, "after boot"); }

void firmwareBeforeSleep() { otaMarkValidIfDue(millis(), "before sleep"); }

void buildFirmwareView(kk::app::FirmwareView* view) {
    view->version = kk::kFirmwareVersion;
    view->env = kk::ota::buildEnv();
    view->slot = kk::ota::runningSlot();
    view->status = gFirmwareStatus;
    view->statusColor = gFirmwareStatusColor;
    view->note = gFirmwareNote;
    view->noteColor = gFirmwareNoteColor;
    view->progress = gFirmwareProgress;
    view->busy = gFirmwareStep != FirmwareStep::Idle;
    view->selectedIndex = gFirmwareIndex;
    if (gFirmwareStep != FirmwareStep::Idle) return;

    const bool canInstall = gFirmwareOffer.verdict == kk::ota::OfferVerdict::Offer &&
                            gFirmwareOffer.app != nullptr;
    if (canInstall) {
        kk::app::FirmwareRowView install;
        install.label = "Install update";
        install.value = gFirmwareManifest.version;
        view->rows.push_back(std::move(install));
    }
    kk::app::FirmwareRowView check;
    check.label = canInstall ? "Check again" : "Check now";
    check.value = "GitHub";
    view->rows.push_back(std::move(check));
}

void drawFirmware() {
    if (!gDisp) return;
    kk::app::FirmwareView view;
    buildFirmwareView(&view);
    auto& d = target();
    d.startWrite();
    kk::app::drawFirmwareScreen(d, view);
    d.endWrite();
    present();
}

void openFirmware() {
    gFirmwareActive = true;
    gSettingsActive = false;
    gAboutActive = false;
    gSignOutConfirm = false;
    gShowHome = false;
    gSettingsNote[0] = 0;
    gFirmwareIndex = 0;
    gFirmwareConfirm = false;
    gFirmwareProgress = -1;
    gFirmwareStep = FirmwareStep::Idle;
    if (!gFirmwareChecked) {
        if (!gOtaState.badgeVersion.empty()) {
            char status[96];
            snprintf(status, sizeof status, "%s available; check to install",
                     gOtaState.badgeVersion.c_str());
            firmwareStatus(status, kAccentHi);
            firmwareNote("Check again before installing", kFaint);
            // The cached offer has no AppImage; a Check now is still required
            // before installing after a reboot.
            gFirmwareOffer = kk::ota::Offer{};
        } else if (gFirmwareStatus[0] == 0) {
            firmwareStatus("Not checked", kDim);
            if (gFirmwareNote[0] == 0)
                firmwareNote("Check GitHub for a new release", kFaint);
        }
    }
    gDirty = true;
}

void firmwareCheck(bool manual) {
    if (gFirmwareStep != FirmwareStep::Idle) return;
    if (!kk::ota::layoutIsOurs()) {
        firmwareStatus("No OTA slot on this build", kWarn);
        firmwareNote("Updates over USB only", kFaint);
        if (gFirmwareActive) drawFirmware();
        return;
    }
    if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
        firmwareStatus("No Wi-Fi connection", kWarn);
        firmwareNote("Join the saved network first", kFaint);
        if (gFirmwareActive) drawFirmware();
        return;
    }

    gFirmwareStep = FirmwareStep::Checking;
    gFirmwareProgress = -1;
    firmwareStatus("Checking GitHub...", kDim);
    firmwareNote(manual ? "This can take a few seconds" : "", kFaint);
    if (gFirmwareActive) drawFirmware();

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
            snprintf(gFirmwareNote, sizeof gFirmwareNote, "HTTP %d  %s", r.httpCode, url.c_str());
        else
            snprintf(gFirmwareNote, sizeof gFirmwareNote, "%s", url.c_str());
        gFirmwareNoteColor = kFaint;
    } else {
        const kk::ota::ManifestParseResult parsed =
            kk::ota::parseManifest(text, std::string_view(url.c_str(), url.length()));
        if (parsed.error != kk::ota::ManifestError::Ok) {
            char status[96];
            snprintf(status, sizeof status, "Bad manifest: %s",
                     kk::ota::manifestErrorString(parsed.error));
            firmwareStatus(status, kWarn);
            if (parsed.line)
                snprintf(gFirmwareNote, sizeof gFirmwareNote, "Line %u  %s", parsed.line,
                         url.c_str());
            else
                snprintf(gFirmwareNote, sizeof gFirmwareNote, "%s", url.c_str());
            gFirmwareNoteColor = kFaint;
        } else {
            gFirmwareManifest = parsed.manifest;
            kk::ota::OfferInput in;
            in.running = kk::kFirmwareVersion;
            in.env = kk::ota::buildEnv();
            in.slotBytes = kk::ota::slotBytes();
            in.rolledBack = gOtaState.failedVersion;
#ifdef KANIKEY_DEV
            in.allowAnyDevVersion = true;
#endif
            gFirmwareOffer = kk::ota::offerFor(gFirmwareManifest, in);
            gFirmwareChecked = true;
            if (gFirmwareOffer.verdict == kk::ota::OfferVerdict::Offer) {
                char status[96];
                char size[16];
                formatBytes(gFirmwareOffer.app->bytes, size, sizeof size);
                snprintf(status, sizeof status, "%s available (%s)",
                         gFirmwareManifest.version.c_str(), size);
                firmwareStatus(status, kAccentHi);
                firmwareNote("Enter: install  Esc: back", kFaint);
                gOtaState.badgeVersion = gFirmwareManifest.version;
                gOtaState.badgeBytes = gFirmwareOffer.app->bytes;
                kk::ota::saveBadge(gOtaState.badgeVersion, gOtaState.badgeBytes);
            } else {
                char status[96];
                snprintf(status, sizeof status, "%s: %s",
                         offerLabel(gFirmwareOffer.verdict), gFirmwareManifest.version.c_str());
                firmwareStatus(status,
                               gFirmwareOffer.verdict == kk::ota::OfferVerdict::UpToDate ? kOk
                                                                                         : kWarn);
                firmwareNote(parsed.manifest.version.c_str(), kFaint);
                if (!gOtaState.badgeVersion.empty()) {
                    gOtaState.badgeVersion.clear();
                    gOtaState.badgeBytes = 0;
                    kk::ota::saveBadge("", 0);
                }
            }
        }
    }
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    if (now >= 1700000000u) {
        gOtaState.lastCheckAt = now;
        kk::ota::saveLastCheck(now);
    }
    gFirmwareStep = FirmwareStep::Idle;
    gFirmwareIndex = 0;
    gFirmwareConfirm = false;
    Serial.printf("[ota] check: %s (%s, http %d, %u bytes)\n",
                  kk::ota::fetchStatusString(r.status), gFirmwareStatus, r.httpCode,
                  static_cast<unsigned>(r.bytes));
    gDirty = true;
    if (gFirmwareActive) drawFirmware();
}

// After a sync the device is already online: a silent check once a day keeps
// the Settings row and About current without a visible delay.
void firmwareAfterSync() {
    if (!kk::ota::layoutIsOurs() || gOtaPending || !gFirmwareStateIdle()) return;
    if (!kk::DevWifi::connected()) return;
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    if (now < 1700000000u) return;
    if (gOtaState.lastCheckAt && now - gOtaState.lastCheckAt < 24u * 60u * 60u) return;
    firmwareStatus("Checking for firmware...", kFaint);
    firmwareCheck(false);
}

bool gFirmwareStateIdle() { return gFirmwareStep == FirmwareStep::Idle; }

bool firmwareProgress(size_t done, size_t total, void*) {
    if (!total) return true;
    gLastInputAt = millis();
    if (gScreenOff) wakeScreen();
    const int pct = static_cast<int>(done * 100 / total);
    if (pct == gFirmwareProgress) return true;
    gFirmwareProgress = pct;
    snprintf(gFirmwareStatus, sizeof gFirmwareStatus, "Downloading update %d%%", pct);
    // No std::string/vector allocations while the TLS download owns the heap:
    // draw a small progress frame directly.
    if (!gDisp) return true;
    auto& d = *gDisp;
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
    snprintf(line, sizeof line, "%s", gFirmwareManifest.version.c_str());
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

void startFirmwareInstall() {
    if (gFirmwareStep != FirmwareStep::Idle) return;
    if (gReviewActive || gLessonActive || gLessonListActive || gLessonQuizActive) {
        firmwareNote("Finish the review/lesson first", kWarn);
        gDirty = true;
        return;
    }
    if (gFirmwareOffer.verdict != kk::ota::OfferVerdict::Offer || !gFirmwareOffer.app) {
        firmwareNote("Check for an update first", kWarn);
        gDirty = true;
        return;
    }
    if (lowBatteryNow()) {
        firmwareNote("Battery below 20%; connect power", kWarn);
        gDirty = true;
        return;
    }
    const kk::ota::AppImage app = *gFirmwareOffer.app;
    const std::string version = gFirmwareManifest.version;

    // Cache and outbox first: the restart after a good install must not lose a
    // completed review or the last snapshot.
    saveCacheSnapshot();
    saveOutbox();
    // The update slot is the one a pending image would roll back to; this
    // image has carried the user here, so it is good now.
    if (gOtaPending && kk::ota::markValid()) gOtaPending = false;

    gFirmwareStep = FirmwareStep::Installing;
    gFirmwareConfirm = false;
    gFirmwareProgress = 0;
    gLastInputAt = millis();
    snprintf(gFirmwareStatus, sizeof gFirmwareStatus, "Downloading update 0%%");
    firmwareNote("Do not power off", kFaint);
    if (gFirmwareActive) drawFirmware();
    releaseCanvas();

    const kk::ota::FetchResult r = kk::ota::install(app, firmwareProgress, nullptr);
    if (r.status == kk::ota::FetchStatus::Ok) {
        gFirmwareProgress = 100;
        firmwareStatus("Verifying update...", kDim);
        firmwareNote("Do not power off", kFaint);
        if (gFirmwareActive) drawFirmware();
        kk::ota::saveInstallRecord(version, kk::ota::nextSlot());
        if (kk::ota::activate()) {
            gFirmwareStep = FirmwareStep::Restarting;
            gFirmwareProgress = 100;
            snprintf(gFirmwareStatus, sizeof gFirmwareStatus, "Restarting into %s", version.c_str());
            firmwareNote("Installed; rebooting", kOk);
            if (gFirmwareActive) drawFirmware();
            Serial.printf("[ota] restarting into %s\n", version.c_str());
            Serial.flush();
            delay(1500);
            ESP.restart();
        }
        kk::ota::clearInstallRecord();
        kk::ota::abandon();
        firmwareStatus("Update image rejected", kWarn);
        firmwareNote("Stays on the current firmware", kFaint);
    } else {
        kk::ota::clearInstallRecord();
        char status[96];
        snprintf(status, sizeof status, "Install failed: %s",
                 kk::ota::fetchStatusString(r.status));
        firmwareStatus(status, kWarn);
        snprintf(gFirmwareNote, sizeof gFirmwareNote, "Stays on %s", kk::kFirmwareVersion);
        gFirmwareNoteColor = kFaint;
        Serial.printf("[ota] install failed: %s (http %d)\n",
                      kk::ota::fetchStatusString(r.status), r.httpCode);
    }
    ensureCanvas();
    gFirmwareStep = FirmwareStep::Idle;
    gFirmwareProgress = -1;
    gFirmwareIndex = 0;
    gFirmwareChecked = false;
    gOtaState.badgeVersion.clear();
    gOtaState.badgeBytes = 0;
    kk::ota::saveBadge("", 0);
    gDirty = true;
    if (gFirmwareActive) drawFirmware();
}

void firmwareKey(const KeyEvent& e) {
    if (!e.down) return;
    if (gFirmwareStep != FirmwareStep::Idle) return;
    if (e.key == Key::Esc) {
        if (gFirmwareConfirm) {
            gFirmwareConfirm = false;
            firmwareNote("Install cancelled", kFaint);
        } else {
            gFirmwareActive = false;
            gSettingsActive = true;
            gSettingsIndex = 10;
        }
        gDirty = true;
        return;
    }
    const bool canInstall = gFirmwareOffer.verdict == kk::ota::OfferVerdict::Offer &&
                            gFirmwareOffer.app != nullptr;
    const int rows = canInstall ? 2 : 1;
    if (e.key == Key::Up) {
        gFirmwareIndex = (gFirmwareIndex + rows - 1) % rows;
        gFirmwareConfirm = false;
        firmwareNote("", kFaint);
        gDirty = true;
    } else if (e.key == Key::Down) {
        gFirmwareIndex = (gFirmwareIndex + 1) % rows;
        gFirmwareConfirm = false;
        firmwareNote("", kFaint);
        gDirty = true;
    } else if (e.key == Key::Enter) {
        if (canInstall && gFirmwareIndex == 0) {
            if (!gFirmwareConfirm) {
                gFirmwareConfirm = true;
                firmwareNote("Enter again to install, Esc cancels", kWarn);
                gDirty = true;
            } else {
                startFirmwareInstall();
            }
        } else {
            firmwareCheck(true);
        }
    }
}

void drawAbout() {
    if (!gDisp) return;
    kk::app::AboutView view;
    view.version = kk::kFirmwareVersion;
    if (kk::ota::layoutIsOurs()) {
        view.version += " (";
        view.version += kk::ota::runningSlot();
        view.version += ")";
    }

    char build[48];
    if (!gOtaState.badgeVersion.empty())
        snprintf(build, sizeof build, "Build %s  upd %s", __DATE__,
                 gOtaState.badgeVersion.c_str());
    else
        snprintf(build, sizeof build, "Build %s %s", __DATE__, __TIME__);
    view.lines.push_back({build, gOtaState.badgeVersion.empty() ? kFaint : kAccentHi, 52});
    view.lines.push_back({std::string("Pack: ") + gPack.statusLine(),
                          gPack.ready() ? kOk : kWarn, 63});
    char cache[64];
    snprintf(cache, sizeof cache, "Cache: %u batch, %u due",
             static_cast<unsigned>(gCachedBatchCount),
             static_cast<unsigned>(gDueReviews));
    view.lines.push_back({cache, kDim, 74});
    view.lines.push_back({std::string("Last sync: ") + gSyncClock +
                              (gOffline ? " (offline)" : ""),
                          gOffline ? kWarn : kDim, 85});
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
    if (!gSdOk) return BootState::NoSd;
    if (!gPack.ready()) return BootState::NoPack;
    if (!kk::DevToken::has()) return BootState::NoToken;
    if (!kk::DevWifi::hasSaved()) return BootState::NoWifi;
    if (gSyncing) return BootState::Syncing;
    if (gSyncIssue == SyncIssue::Auth) return BootState::AuthError;
    if (gSyncIssue == SyncIssue::Forbidden) return BootState::Forbidden;
    if (gSyncIssue == SyncIssue::RateLimit) return BootState::RateLimited;
    if (gSyncIssue == SyncIssue::Server || gSyncIssue == SyncIssue::Other)
        return BootState::ServerError;
    if (!gHaveSummary) return gSyncAttempted ? BootState::Failed : BootState::NotSynced;
    return BootState::Ready;
}

void drawBoot() {
    if (!gDisp) return;

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
            line1 = "Copy glyphs.kgp to";
            line2 = "/kanikey/ on the SD card";
            action = "Enter: check again";
            break;
        case BootState::NoToken:
            title = "Token needed";
            line1 = "Press Enter for setup";
            line2 = "Wi-Fi and token from your phone";
            action = "Enter: setup";
            break;
        case BootState::NoWifi:
            title = "Wi-Fi needed";
            line1 = "Press Enter for setup";
            line2 = "Join KaniKey-XXXX on your phone";
            action = "Enter: setup";
            break;
        case BootState::NotSynced:
            title = "Not synced";
            line1 = gNetLine;
            line2 = "Press Enter to sync";
            action = "Enter: sync";
            break;
        case BootState::Syncing:
            title = "Syncing...";
            line1 = gNetLine;
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
            line1 = gNetLine;
            line2 = "Press Enter to retry";
            action = "Enter: retry";
            break;
        case BootState::Failed:
            title = "Sync failed";
            line1 = gNetLine;
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

void drawSync() {
    if (!gDisp) return;

    SyncView view;
    view.batteryPercent = tgc::board::batteryPercent();
    view.finished = gSyncFinished;
    view.completed = gSyncCompleted;
    for (int i = 0; i < kSyncRowCount; ++i)
        view.rows[i] = {gSyncRows[i].label, gSyncRows[i].value, gSyncRows[i].state};
    view.errorLine = gNetLine;
    switch (gSyncIssue) {
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

void drawSetupPortal() {
    if (!gDisp) return;

    SetupView view;
    view.batteryPercent = tgc::board::batteryPercent();
    view.apMode = gSetupPortal.mode() == kk::SetupPortal::Mode::Ap;
    view.ssid = gSetupPortal.ssid();
    view.apPassword = gSetupPortal.apPassword();
    view.url = gSetupPortal.url();
    view.sessionCode = gSetupPortal.sessionCode();
    view.tokenValidated = gSetupPortal.tokenValidated();
    view.tokenUser = gSetupPortal.tokenUser();
    view.tokenLevel = gSetupPortal.tokenLevel();
    view.tokenConfigured = kk::DevToken::has();
    view.message = gSetupPortal.message();
    view.packStatus = gSetupPortal.packStatus().c_str();

    auto& d = target();
    d.startWrite();
    kk::app::drawSetupScreen(d, view);
    d.endWrite();
    present();
}

#ifdef KANIKEY_DEV
void showDebug() {
    if (!gDisp) return;
    DebugView view;
    view.batteryPercent = tgc::board::batteryPercent();
    view.netLine = gNetLine;
    view.sdLine = gSdLine;
    view.sdOk = gSdOk;
    for (int i = 0; i < 3; ++i) view.log[i] = gLog[i].text;
    view.shift = gShift;
    view.fn = gFn;
    view.ctrl = gCtrl;
    view.opt = gOpt;
    view.alt = gAlt;
    snprintf(view.heapLine, sizeof view.heapLine, "heap %u  largest %u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                    MALLOC_CAP_8BIT)));

    auto& d = target();
    d.startWrite();
    drawDebugScreen(d, view);
    d.endWrite();
    present();
}

void showRadicalTest() {
    if (!gDisp) return;
    RadicalTestView view;
    view.subjectId = gRadicalTestId;

    auto& d = target();
    d.startWrite();
    drawRadicalTestScreen(d, gPack, view);
    d.endWrite();
    present();
}

// Seeds a small cache to verify CACHE-03/04 without WaniKani credentials.
// Dev-only: `C` writes it, `X` removes it.
void seedDevCache() {
    if (!kk::DevToken::has()) {
        Serial.println("[cache] set a token first: k <token>");
        return;
    }
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    const uint32_t stamp = now >= 1700000000u ? now : 1760000000u;

    kk::CacheSnapshot snapshot;
    snapshot.meta.savedAt = stamp;
    snapshot.meta.accountKey = currentAccountKey();
    snapshot.summary.reviews.push_back(
        kk::SummaryBucket{stamp - 60, 6, {1, 2, 3, 4, 5, 6}});
    snapshot.user.username = "dev";
    snapshot.user.level = 3;
    snapshot.user.subscriptionActive = true;
    snapshot.user.maxLevelGranted = 3;
    snapshot.user.lessonsBatchSize = 5;
    snapshot.levelProgress.level = 3;
    snapshot.levelProgress.unlockedAt = stamp - 7 * 86400;
    snapshot.levelProgress.startedAt = stamp - 7 * 86400;
    snapshot.levelProgress.valid = true;
    snapshot.srs = kk::SrsCounts{42, 18, 9, 4, 2, true};

    const auto addAssignment = [&snapshot, stamp](uint32_t id, uint32_t subjectId,
                                                  kk::SubjectType type) {
        kk::Assignment a;
        a.id = id;
        a.subjectId = subjectId;
        a.type = type;
        a.srsStage = 1;
        a.availableAt = stamp - 120;
        a.dataUpdatedAt = stamp - 240;
        snapshot.assignments.push_back(a);
    };
    addAssignment(1001, 1, kk::SubjectType::Radical);
    addAssignment(1002, 2, kk::SubjectType::Kanji);
    addAssignment(1003, 3, kk::SubjectType::Vocabulary);
    addAssignment(1004, 4, kk::SubjectType::Kanji);
    addAssignment(1005, 5, kk::SubjectType::Vocabulary);
    addAssignment(1006, 6, kk::SubjectType::Vocabulary);

    kk::SubjectCache subjects;
    subjects.meta = snapshot.meta;
    kk::Subject radical;
    radical.id = 1;
    radical.type = kk::SubjectType::Radical;
    radical.level = 1;
    radical.meanings = {{"ground", true, true}};
    subjects.subjects.push_back(radical);
    kk::Subject kanji;
    kanji.id = 2;
    kanji.type = kk::SubjectType::Kanji;
    kanji.level = 1;
    kanji.characters = "日";
    kanji.meanings = {{"day", true, true}};
    kanji.readings = {{"にち", true, true, kk::ReadingType::Onyomi}};
    kanji.meaningSynonyms = {"sun"};
    kanji.meaningMnemonic = "A window with the sun in it.";
    kanji.readingMnemonic = "Nichi sounds like a sunny day.";
    kanji.meaningHint = "Think of daylight.";
    kanji.readingHint = "The sun is nichi.";
    kanji.meaningNote = "Remember the sun.";
    kanji.readingNote = "Nichi, not ni.";
    kanji.componentSubjectIds = {1};
    kanji.visuallySimilarSubjectIds = {3};
    kanji.amalgamationSubjectIds = {3};
    subjects.subjects.push_back(kanji);
    kk::Subject vocabulary;
    vocabulary.id = 3;
    vocabulary.type = kk::SubjectType::Vocabulary;
    vocabulary.level = 1;
    vocabulary.characters = "日本";
    vocabulary.meanings = {{"Japan", true, true}};
    vocabulary.readings = {{"にほん", true, true, kk::ReadingType::Kunyomi}};
    vocabulary.meaningMnemonic = "The sun's origin is Japan.";
    vocabulary.readingMnemonic = "Nihon is Japan.";
    vocabulary.partsOfSpeech = {"noun"};
    vocabulary.contextSentences = {{"これは日本です。", "This is Japan."}};
    vocabulary.componentSubjectIds = {2};
    vocabulary.visuallySimilarSubjectIds = {2};
    subjects.subjects.push_back(vocabulary);

    // Answer-checker seeds: a kanji with two accepted on'yomi and one rejected
    // kun'yomi, plus two vocabulary items with a single reading.
    kk::Subject person;
    person.id = 4;
    person.type = kk::SubjectType::Kanji;
    person.level = 1;
    person.characters = "人";
    person.meanings = {{"person", true, true}};
    person.readings = {
        {"じん", true, true, kk::ReadingType::Onyomi},
        {"にん", false, true, kk::ReadingType::Onyomi},
        {"ひと", false, false, kk::ReadingType::Kunyomi},
    };
    subjects.subjects.push_back(person);
    kk::Subject tonight;
    tonight.id = 5;
    tonight.type = kk::SubjectType::Vocabulary;
    tonight.level = 1;
    tonight.characters = "今夜";
    tonight.meanings = {{"tonight", true, true}};
    tonight.readings = {{"こんや", true, true, kk::ReadingType::Kunyomi}};
    subjects.subjects.push_back(tonight);
    kk::Subject photograph;
    photograph.id = 6;
    photograph.type = kk::SubjectType::Vocabulary;
    photograph.level = 1;
    photograph.characters = "写真";
    photograph.meanings = {{"photograph", true, true}};
    photograph.readings = {{"しゃしん", true, true, kk::ReadingType::Kunyomi}};
    subjects.subjects.push_back(photograph);

    auto stats = [](kk::Subject& s, uint32_t meaningCorrect, uint32_t meaningIncorrect,
                    uint32_t readingCorrect, uint32_t readingIncorrect,
                    uint8_t percentageCorrect) {
        s.meaningCorrect = meaningCorrect;
        s.meaningIncorrect = meaningIncorrect;
        s.readingCorrect = readingCorrect;
        s.readingIncorrect = readingIncorrect;
        s.percentageCorrect = percentageCorrect;
        s.hasReviewStats = true;
    };
    stats(subjects.subjects[0], 12, 2, 0, 0, 86);
    stats(subjects.subjects[1], 10, 2, 8, 1, 86);
    stats(subjects.subjects[2], 9, 1, 7, 1, 89);

    const bool snapshotOk = kk::cacheSaveSnapshot(snapshot);
    const bool subjectsOk = gSdOk && kk::cacheSaveSubjects(SD, subjects);
    Serial.printf("[cache] dev seed snapshot=%d subjects=%d\n", snapshotOk ? 1 : 0,
                  subjectsOk ? 1 : 0);
    if (snapshotOk && loadCache()) gDirty = true;
}

// Dev-only `L`: a one-item lesson whose subject is a radical without a Unicode
// character, so the card must use the pack's radical bitmap. It bypasses the
// network and the summary so the radical-only path can be exercised without a
// low-level account.
void seedDevLesson() {
    gLessonBatch = kk::LessonBatch{};
    gLessonComponents.clear();
    kk::Assignment a;
    a.id = 2001;
    a.subjectId = 1;
    a.type = kk::SubjectType::Radical;
    a.srsStage = 0;
    a.lessonPosition = 0;
    a.availableAt = static_cast<uint32_t>(time(nullptr));
    gLessonBatch.assignments.push_back(a);
    kk::Subject radical;
    radical.id = 1;
    radical.type = kk::SubjectType::Radical;
    radical.level = 1;
    radical.meanings = {{"ground", true, true}};
    radical.meaningMnemonic = "The ground under your feet.";
    radical.meaningHint = "A flat line.";
    gLessonBatch.subjects.push_back(radical);
    gLessonBatch.ok = true;
    gLessonActive = false;
    gLessonListActive = true;
    gLessonEmptyActive = false;
    gLessonQuizActive = false;
    gShowHome = false;
    gLessonIndex = 0;
    gLessonPage = 0;
    gDirty = true;
    Serial.println("[lesson] dev radical-only lesson seeded");
}

void clearDevCache() {
    kk::cacheClearSnapshot();
    if (gSdOk) kk::cacheClearSubjects(SD);
    gHaveSummary = false;
    gHaveUser = false;
    gBatch = kk::ReviewBatch{};
    gBatchEvicted = false;
    gCachedBatchCount = 0;
    gSrs = kk::SrsCounts{};
    gDueReviews = 0;
    gLessons = 0;
    gOffline = false;
    gShowHome = false;
    gSyncedAt = 0;
    gCacheSavedAt = 0;
    gDirty = true;
    Serial.println("[cache] dev cache cleared");
}

// Dev-only `J`: two fixed rejected items and one stuck item for the outbox
// screen. Reusing the assignment IDs updates an earlier seed instead of piling
// up duplicates.
void seedDevRejectedOutbox() {
    struct Seed {
        uint32_t assignmentId;
        uint32_t subjectId;
        kk::OutboxState state;
        uint8_t attempts;
        uint16_t httpCode;
    };
    static const Seed kSeeds[] = {
        {999999901, 999999001, kk::OutboxState::Rejected, 1, 422},
        {999999902, 999999002, kk::OutboxState::Rejected, 1, 409},
        {999999903, 999999003, kk::OutboxState::Stuck, kk::kOutboxMaxAttempts, 0},
    };
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    const uint32_t createdAt = now >= 1700000000u ? now : 1793000000u;
    for (const Seed& seed : kSeeds) {
        kk::OutboxItem* item = nullptr;
        for (kk::OutboxItem& it : gOutbox)
            if (it.assignmentId == seed.assignmentId) {
                item = &it;
                break;
            }
        if (!item) {
            const uint32_t seq = kk::outboxAdd(gOutbox, seed.assignmentId, seed.subjectId, 0, 0,
                                               createdAt);
            if (!seq) {
                Serial.println("[outbox] seed failed: full");
                continue;
            }
            item = kk::outboxFind(gOutbox, seq);
        }
        if (!item) continue;
        item->state = seed.state;
        item->httpCode = seed.httpCode;
        item->attempts = seed.attempts;
        item->createdAt = createdAt;
        if (seed.state == kk::OutboxState::Stuck)
            Serial.printf("[outbox] synthetic stuck assignment %u after %u attempts\n",
                          static_cast<unsigned>(seed.assignmentId),
                          static_cast<unsigned>(seed.attempts));
        else
            Serial.printf("[outbox] synthetic rejected assignment %u HTTP %u\n",
                          static_cast<unsigned>(seed.assignmentId),
                          static_cast<unsigned>(seed.httpCode));
    }
    saveOutbox();
    if (!gReviewActive && !gReviewSummary && !gLessonActive && !gLessonListActive &&
        !gLessonQuizActive && !gSyncScreen && !gSettingsActive && !gPauseActive &&
        !gPortalActive) {
        gOutboxScreen = false;
        gOutboxConfirm = false;
        gShowHome = true;
    }
    gDirty = true;
}
#endif

void draw() {
    if (gNoticeActive) {
        drawNotice();
        return;
    }
    if (gPortalActive) {
        drawSetupPortal();
        return;
    }
    if (gLowBatteryPrompt) {
        drawLowBattery();
        return;
    }
    if (gSignOutConfirm) {
        drawSignOutConfirm();
        return;
    }
    if (gAboutActive) {
        drawAbout();
        return;
    }
    if (gOutboxScreen) {
        drawOutbox();
        return;
    }
    if (gStatsActive) {
        drawStats();
        return;
    }
    if (gStudyMenuActive) {
        drawStudyMenu();
        return;
    }
    if (gCriticalListActive) {
        drawCriticalList();
        return;
    }
    if (gFirmwareActive) {
        drawFirmware();
        return;
    }
    if (gSettingsActive) {
        drawSettings();
        return;
    }
    if (gPauseActive) {
        drawPause();
        return;
    }
    if (gSyncScreen) {
        drawSync();
        return;
    }
#ifdef KANIKEY_DEV
    if (gShowRadicalTest) {
        showRadicalTest();
        return;
    }
#endif
    if (gReviewDetail) {
        showReviewDetail();
        return;
    }
    if (gReviewActive) {
        showReview();
        return;
    }
    if (gReviewSummary) {
        showReviewSummary();
        return;
    }
    if (gLessonEmptyActive) {
        showLessonEmpty();
        return;
    }
    if (gLessonListActive) {
        showLessonList();
        return;
    }
    if (gLessonQuizActive) {
        showLessonQuiz();
        return;
    }
    if (gLessonActive) {
        showLesson();
        return;
    }
#ifdef KANIKEY_DEV
    if (gShowDebug) {
        showDebug();
        return;
    }
#endif
    if (gShowHome) {
        drawHome();
        return;
    }
    drawBoot();
}

void drawStatus() {
    if (!gDisp || gPortalActive || gLowBatteryPrompt || gSignOutConfirm || gAboutActive ||
        gFirmwareActive || gSettingsActive || gPauseActive || gSyncScreen || gShowHome ||
        gReviewActive || gReviewSummary || gLessonListActive || gLessonEmptyActive ||
        gLessonActive || gOutboxScreen || gStatsActive || gStudyMenuActive ||
        gCriticalListActive)
        return;
#ifdef KANIKEY_DEV
    if (gShowDebug) showDebug();
#endif
    // The boot screen is redrawn on state changes through gDirty; only the
    // debug screen needs a periodic heap/battery refresh.
}

void wakeScreen() {
    if (!gDimmed && !gScreenOff) return;
    tgc::board::setDisplayOn(true);
    tgc::board::setBrightness(kk::settingsBrightness());
    gDimmed = false;
    gScreenOff = false;
    gDirty = true;
}

void dumpCanvas() {
    if (!gCanvas) {
        Serial.println("#shot 0 0 none");
        return;
    }
    static const char kHex[] = "0123456789abcdef";
    static char line[16 + 4 * 240 + 2];
    const int w = gCanvas->width();
    const int h = gCanvas->height();
    // The sprite buffer is RGB332 (one byte per pixel); dump it raw.
    const bool rgb332 = gCanvas->getColorDepth() == lgfx::rgb332_1Byte;
    const uint8_t* px = static_cast<const uint8_t*>(gCanvas->getBuffer());
    Serial.printf("\n#shot %d %d %s\n", w, h, rgb332 ? "rgb332" : "rgb565");
    for (int y = 0; y < h; ++y) {
        int n = snprintf(line, sizeof line, "#r %d ", y);
        for (int x = 0; x < w; ++x) {
            if (rgb332) {
                const uint8_t v = px[static_cast<size_t>(y) * w + x];
                line[n++] = kHex[v >> 4];
                line[n++] = kHex[v & 15];
            } else {
                const uint8_t* p = px + (static_cast<size_t>(y) * w + x) * 2;
                line[n++] = kHex[p[0] >> 4];
                line[n++] = kHex[p[0] & 15];
                line[n++] = kHex[p[1] >> 4];
                line[n++] = kHex[p[1] & 15];
            }
        }
        line[n++] = '\n';
        Serial.write(reinterpret_cast<const uint8_t*>(line), n);
    }
    Serial.print("#end\n");
}

void assetRespond(WiFiClient& client, int code, const char* reason) {
    client.printf("HTTP/1.1 %d %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", code,
                  reason);
    client.flush();
    delay(20);
    client.stop();
}

void stopAssetServer() {
    gAssetServerActive = false;
    gAssetServer.end();
    gDirty = true;
}

void serviceAssetServer() {
    if (!gAssetServerActive) return;
    WiFiClient client = gAssetServer.available();
    if (!client) return;
    client.setTimeout(10);
    client.setNoDelay(true);

    String request = client.readStringUntil('\n');
    request.trim();
    Serial.printf("[asset] request %s\n", request.c_str());
    const int sp1 = request.indexOf(' ');
    const int sp2 = sp1 < 0 ? -1 : request.indexOf(' ', sp1 + 1);
    if (sp1 < 0 || sp2 < 0) {
        assetRespond(client, 400, "Bad Request");
        stopAssetServer();
        return;
    }
    const String method = request.substring(0, sp1);
    const String path = request.substring(sp1 + 1, sp2);
    int contentLength = -1;
    const uint32_t headerStart = millis();
    while (millis() - headerStart < 10000) {
        String line = client.readStringUntil('\n');
        if (line == "\r" || line.isEmpty()) break;
        if (line.startsWith("Content-Length:")) contentLength = line.substring(15).toInt();
    }
    if (method != "PUT" || !path.startsWith("/kanikey/") || path.indexOf("..") >= 0 ||
        path.length() > 60 || contentLength <= 0 || contentLength > 32 * 1024 * 1024) {
        Serial.printf("[asset] rejected %s %d\n", method.c_str(), contentLength);
        assetRespond(client, 400, "Bad Request");
        stopAssetServer();
        return;
    }
    const int slash = path.lastIndexOf('/');
    if (slash > 0) {
        const String dir = path.substring(0, slash);
        if (!SD.exists(dir)) SD.mkdir(dir);
    }
    const bool isPack = path == "/kanikey/glyphs.kgp";
    if (isPack) gPack.close();
    // Write a tmp file and rename it over the old one, so an interrupted
    // upload never leaves a broken pack or asset behind (FIX-24 c).
    const String tmpPath = path + ".tmp";
    if (SD.exists(tmpPath)) SD.remove(tmpPath);
    File f = SD.open(tmpPath, FILE_WRITE);
    if (!f) {
        Serial.printf("[asset] cannot open %s\n", tmpPath.c_str());
        assetRespond(client, 500, "Cannot Open");
        stopAssetServer();
        if (isPack) gPack.open(SD, "/kanikey/glyphs.kgp");
        return;
    }

    drawLoading("Wi-Fi upload", path.c_str());
    Serial.printf("[asset] writing %s (%d bytes)\n", path.c_str(), contentLength);
    uint8_t buf[1024];
    uint32_t got = 0;
    bool writeOk = true;
    uint32_t lastData = millis();
    while (got < static_cast<uint32_t>(contentLength)) {
        const int avail = client.available();
        if (avail > 0) {
            const size_t take = std::min<size_t>(
                sizeof buf, std::min<size_t>(static_cast<size_t>(avail),
                                            contentLength - static_cast<int>(got)));
            const int n = client.read(buf, take);
            if (n > 0) {
                if (f.write(buf, n) != static_cast<size_t>(n)) {
                    writeOk = false;
                    break;
                }
                got += static_cast<uint32_t>(n);
                lastData = millis();
                continue;
            }
        }
        if (!client.connected() && !client.available()) break;
        if (millis() - lastData > 10000) break;
        delay(1);
    }
    f.close();
    const bool complete = writeOk && got == static_cast<uint32_t>(contentLength);
    Serial.printf("[asset] done %u/%d\n", got, contentLength);
    if (!complete) {
        // Keep the old file; only the tmp file goes away.
        SD.remove(tmpPath);
        assetRespond(client, 500, writeOk ? "Short Body" : "Write Failed");
        stopAssetServer();
        if (isPack) {
            gPack.open(SD, "/kanikey/glyphs.kgp");
            Serial.printf("[pack] kept old: %s\n", gPack.statusLine());
        }
        return;
    }

    if (isPack) {
        // A new pack is verified before it replaces the working one. The CRC
        // runs here, not at boot (3,587 ms for the 3.7 MB pack).
        const bool crcOk = gPack.open(SD, tmpPath.c_str(), true);
        char status[48];
        snprintf(status, sizeof status, "%s", gPack.statusLine());
        gPack.close();
        Serial.printf("[pack] tmp %s\n", status);
        if (!crcOk) {
            SD.remove(tmpPath);
            assetRespond(client, 422, "Bad Pack");
            stopAssetServer();
            gPack.open(SD, "/kanikey/glyphs.kgp");
            Serial.printf("[pack] kept old: %s\n", gPack.statusLine());
            return;
        }
    }
    if (SD.exists(path)) SD.remove(path);
    if (!SD.rename(tmpPath, path)) {
        SD.remove(tmpPath);
        assetRespond(client, 500, "Rename Failed");
        stopAssetServer();
        if (isPack) {
            gPack.open(SD, "/kanikey/glyphs.kgp");
            Serial.printf("[pack] %s\n", gPack.statusLine());
        }
        return;
    }
    assetRespond(client, 201, "Created");
    // The client is closed before the pack is opened again (FIX-25).
    stopAssetServer();
    if (isPack) {
        gPack.open(SD, "/kanikey/glyphs.kgp");
        Serial.printf("[pack] %s\n", gPack.statusLine());
    }
}

void startAssetServer() {
    if (gReviewActive || gLessonActive || gLessonListActive || gLessonEmptyActive) {
        Serial.println("[asset] finish the review/lesson first");
        return;
    }
    if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
        Serial.println("[asset] no Wi-Fi");
        return;
    }
    gAssetServer.begin();
    gAssetServer.setNoDelay(true);
    gAssetServerActive = true;
    char url[64];
    snprintf(url, sizeof url, "http://%s:8080", kk::DevWifi::ip().c_str());
    drawLoading("Asset server", url);
    Serial.printf("[asset] ready %s\n", url);
}

void uploadSdFile(const String& line) {
    // up <bytes> <path>: the host sends the raw bytes right after the ack.
    const int sep = line.indexOf(' ', 3);
    if (sep < 0) {
        Serial.println("[up] use: up <bytes> <path>");
        return;
    }
    const long requested = line.substring(3, sep).toInt();
    String path = line.substring(sep + 1);
    path.trim();
    const uint32_t size = requested > 0 ? static_cast<uint32_t>(requested) : 0;
    if (!gSdOk || size == 0 || size > 32u * 1024u * 1024u || !path.startsWith("/") ||
        path.indexOf("..") >= 0 || path.length() > 60) {
        Serial.println("[up] bad size, path or SD");
        return;
    }

    const int slash = path.lastIndexOf('/');
    if (slash > 0) {
        const String dir = path.substring(0, slash);
        if (!SD.exists(dir)) SD.mkdir(dir);
    }
    const bool isPack = path == "/kanikey/glyphs.kgp";
    if (isPack) gPack.close();
    // Same tmp-and-rename as the Wi-Fi upload: a short transfer keeps the old
    // file, a bad pack CRC never replaces a working pack.
    const String tmpPath = path + ".tmp";
    if (SD.exists(tmpPath)) SD.remove(tmpPath);
    File f = SD.open(tmpPath, FILE_WRITE);
    if (!f) {
        Serial.printf("[up] cannot open %s\n", tmpPath.c_str());
        if (isPack) gPack.open(SD, "/kanikey/glyphs.kgp");
        return;
    }

    drawLoading("Writing SD...", path.c_str());
    Serial.printf("[up] ready %s %u\n", path.c_str(), static_cast<unsigned>(size));
    static uint8_t buf[8192];
    uint32_t got = 0;
    bool writeOk = true;
    uint32_t lastLog = millis();
    while (got < size) {
        // Keep the block at the host helper's 2 KiB chunk: the USB RX ring is
        // 4 KiB, and tools/device.py waits for one ack per chunk. A larger
        // `want` made both sides wait for each other (5 s timeout, then the
        // transfer failed on anything over 2 KiB).
        const size_t want = std::min<size_t>(2048, size - got);
        size_t have = 0;
        uint32_t lastByte = millis();
        while (have < want) {
            const int available = Serial.available();
            if (available > 0) {
                const size_t take =
                    std::min<size_t>(static_cast<size_t>(available), want - have);
                const size_t n = Serial.readBytes(buf + have, take);
                if (n) {
                    have += n;
                    lastByte = millis();
                    continue;
                }
            }
            if (millis() - lastByte > 5000) break;
            delay(1);
        }
        if (have == 0) {
            Serial.println("[up] timeout");
            break;
        }
        if (f.write(buf, have) != have) {
            Serial.println("[up] write failed");
            writeOk = false;
            break;
        }
        got += have;
        Serial.printf("[up] ack %u\n", static_cast<unsigned>(got));
        if (millis() - lastLog > 1000) {
            lastLog = millis();
            Serial.printf("[up] %u/%u\n", static_cast<unsigned>(got),
                          static_cast<unsigned>(size));
        }
        if (have < want) break;
    }
    f.close();
    Serial.printf("[up] done %u/%u\n", static_cast<unsigned>(got),
                  static_cast<unsigned>(size));
    if (!writeOk || got != size) {
        SD.remove(tmpPath);
        Serial.printf("[up] failed, kept %s\n", path.c_str());
        if (isPack) {
            gPack.open(SD, "/kanikey/glyphs.kgp");
            Serial.printf("[pack] %s\n", gPack.statusLine());
        }
        gDirty = true;
        return;
    }

    if (isPack) {
        const bool crcOk = gPack.open(SD, tmpPath.c_str(), true);
        char status[48];
        snprintf(status, sizeof status, "%s", gPack.statusLine());
        gPack.close();
        Serial.printf("[pack] tmp %s\n", status);
        if (!crcOk) {
            SD.remove(tmpPath);
            Serial.println("[up] bad pack crc, kept the old pack");
            gPack.open(SD, "/kanikey/glyphs.kgp");
            Serial.printf("[pack] %s\n", gPack.statusLine());
            gDirty = true;
            return;
        }
    }
    if (SD.exists(path)) SD.remove(path);
    if (!SD.rename(tmpPath, path)) {
        SD.remove(tmpPath);
        Serial.printf("[up] rename failed, %s removed\n", path.c_str());
        if (isPack) {
            gPack.open(SD, "/kanikey/glyphs.kgp");
            Serial.printf("[pack] %s\n", gPack.statusLine());
        }
        gDirty = true;
        return;
    }
    if (isPack) {
        gPack.open(SD, "/kanikey/glyphs.kgp");
        Serial.printf("[pack] %s\n", gPack.statusLine());
    }
    gDirty = true;
}

#ifdef KANIKEY_DEV
// "K <keys>" presses grid keys as if typed, so hardware checks can drive the
// real key handlers and the romaji IME. \n Enter, \e Esc, \t Tab, \b
// Backspace; any other character is found on the main or Shift layer.
void injectKeys(const String& keys) {
    const auto press = [](int x, int y, bool shift) {
        const bool savedShift = gShift;
        gShift = shift;
        KeyEvent e;
        e.key = tgc::cardputerKeyAt(x, y);
        e.x = static_cast<uint8_t>(x);
        e.y = static_cast<uint8_t>(y);
        e.down = true;
        onKey(e);
        e.down = false;
        onKey(e);
        gShift = savedShift;
    };
    for (unsigned i = 0; i < keys.length(); ++i) {
        char c = keys[i];
        if (c == '\\' && i + 1 < keys.length()) {
            const char next = keys[++i];
            if (next == 'n') press(13, 2, false);
            else if (next == 'e') press(0, 0, false);
            else if (next == 't') press(0, 1, false);
            else if (next == 'b') press(13, 0, false);
            else if (next == '\\') c = '\\';
            if (next != '\\') continue;
        }
        bool found = false;
        for (int layer = 0; layer < 2 && !found; ++layer)
            for (int y = 0; y < tgc::kCardputerRows && !found; ++y)
                for (int x = 0; x < tgc::kCardputerCols && !found; ++x)
                    if (tgc::cardputerCharAt(x, y, layer == 1) == c) {
                        press(x, y, layer == 1);
                        found = true;
                    }
        if (!found) Serial.printf("[key] no key for '%c'\n", c);
    }
    gLastInputAt = millis();
}
#endif

void handleSerial() {
    if (Serial.available()) {
        gLastInputAt = millis();
        wakeScreen();
    }
    while (Serial.available()) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\r') continue;
        if (c != '\n') {
            if (gSerialLine.length() < 256) gSerialLine += c;  // U <url> takes up to 200
            continue;
        }
        gSerialLine.trim();
        if (gSerialLine.startsWith("up ")) {
            uploadSdFile(gSerialLine);
        } else if (gSerialLine == "p") {
            dumpCanvas();
        } else if (gSerialLine == "h") {
            Serial.printf("[heap] free %u, largest %u, min %u, stack %u\n",
                          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                          static_cast<unsigned>(heap_caps_get_largest_free_block(
                              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                          static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
                          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr) *
                                                sizeof(StackType_t)));
        } else if (gSerialLine == "i") {
            Serial.printf("[pack] %s\n", gPack.statusLine());
        } else if (gSerialLine == "S") {
            startAssetServer();
        } else if (gSerialLine == "r") {
            Serial.println("[kk] restart");
            delay(100);
            ESP.restart();
#ifdef KANIKEY_DEV
        } else if (gSerialLine == "w") {
            snprintf(gNetLine, sizeof gNetLine, "NET: joining...");
            gDirty = true;
            if (kk::DevWifi::connect(30000))
                snprintf(gNetLine, sizeof gNetLine, "NET: %s", kk::DevWifi::ip().c_str());
            else
                snprintf(gNetLine, sizeof gNetLine, "NET: join failed");
        } else if (gSerialLine.startsWith("w ")) {
            String rest = gSerialLine.substring(2);
            const int sep = rest.indexOf('|');
            if (sep < 0) {
                Serial.println("[wifi] use: w <ssid>|<password>");
            } else {
                const String ssid = rest.substring(0, sep);
                const String pass = rest.substring(sep + 1);
                if (!kk::DevWifi::saveCredentials(ssid, pass)) {
                    Serial.println("[wifi] not saved (bad SSID or password length)");
                } else if (kk::DevWifi::connect(30000)) {
                    snprintf(gNetLine, sizeof gNetLine, "NET: %s", kk::DevWifi::ip().c_str());
                } else {
                    snprintf(gNetLine, sizeof gNetLine, "NET: join failed");
                }
            }
        } else if (gSerialLine == "t") {
            if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                snprintf(gNetLine, sizeof gNetLine, "API: no Wi-Fi");
            } else {
                runProbe();
            }
        } else if (gSerialLine == "u") {
            if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                snprintf(gNetLine, sizeof gNetLine, "API: no Wi-Fi");
            } else {
                runProbe();
            }
        } else if (gSerialLine.startsWith("k ")) {
            String token = gSerialLine.substring(2);
            token.trim();
            const String previousToken = kk::DevToken::load();
            if (kk::DevToken::save(token)) {
                if (previousToken != token) {
                    clearLevelUpNotice();
                    gHaveUser = false;
                }
                Serial.printf("[auth] token saved (%u chars)\n", static_cast<unsigned>(token.length()));
                gDeferredAuthIssue = SyncIssue::None;
                gOutboxAuthIssue = false;
                kk::wkClearAuthBlock();
                if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                    snprintf(gNetLine, sizeof gNetLine, "API: no Wi-Fi");
                } else {
                    runProbe();
                }
            } else {
                Serial.println("[auth] token not saved (empty or too long)");
            }
        } else if (gSerialLine == "c") {
            kk::DevToken::clear();
            kk::wkClearAuthBlock();
            kk::journalClear();
            kk::criticalClear();
            gCritical.clear();
            gCriticalLoaded = false;
            gStatsHistoryLoaded = false;
            gPendingJournalHandle = 0;
            gDeferredAuthIssue = SyncIssue::None;
            gOutboxAuthIssue = false;
            clearLevelUpNotice();
            gHaveUser = false;
            Serial.println("[auth] token cleared");
            snprintf(gNetLine, sizeof gNetLine, "API: token cleared");
        } else if (gSerialLine == "d") {
            kk::DevWifi::disconnect();
            snprintf(gNetLine, sizeof gNetLine, "NET: off");
            Serial.println("[wifi] disconnected");
        } else if (gSerialLine == "o") {
            Serial.printf("[outbox] %u items\n", static_cast<unsigned>(gOutbox.size()));
            for (const kk::OutboxItem& it : gOutbox)
                Serial.printf("[outbox] seq %u assignment %u state %u attempts %u http %u\n",
                              static_cast<unsigned>(it.seq),
                              static_cast<unsigned>(it.assignmentId),
                              static_cast<unsigned>(it.state), static_cast<unsigned>(it.attempts),
                              static_cast<unsigned>(it.httpCode));
        } else if (gSerialLine == "f") {
            if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                snprintf(gNetLine, sizeof gNetLine, "Outbox: no Wi-Fi");
            } else {
                flushOutbox();
            }
        } else if (gSerialLine == "x") {
            const uint32_t seq = kk::outboxAdd(gOutbox, 999999999, 1, 0, 0,
                                               static_cast<uint32_t>(time(nullptr)));
            if (seq) {
                saveOutbox();
                Serial.printf("[outbox] synthetic seq %u queued\n", static_cast<unsigned>(seq));
            }
        } else if (gSerialLine == "z") {
            gOutbox.clear();
            clearOutboxAuthIssue();
            saveOutbox();
            Serial.println("[outbox] cleared");
        } else if (gSerialLine == "l") {
            if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                snprintf(gNetLine, sizeof gNetLine, "Lesson: no Wi-Fi");
            } else {
                startLessons();
            }
        } else if (gSerialLine == "g") {
            if (gLessonActive) {
                ++gLessonPage;
                gDirty = true;
            } else if (gReviewDetail) {
                if (gReviewDetailPage + 1 < gReviewDetailPages) ++gReviewDetailPage;
                gDirty = true;
            }
        } else if (gSerialLine == "D") {
            if (gReviewActive && gFeedback) {
                const kk::Subject* detail = subjectById(gFeedbackSubjectId);
                gReviewDetailPages = detail ? detailPageCount(*detail) : 5;
                gReviewDetail = !gReviewDetail;
                gReviewDetailPage = 0;
                gDirty = true;
            }
        } else if (gSerialLine == "Q") {
            if (gLessonActive) {
                startLessonQuiz();
            } else {
                Serial.println("[lesson] open a lesson card first");
            }
        } else if (gSerialLine == "v") {
            if (gBatch.assignments.empty() && !gBatchEvicted && !kk::DevWifi::connected() &&
                !kk::DevWifi::connect(30000)) {
                snprintf(gNetLine, sizeof gNetLine, "Review: no Wi-Fi");
            } else {
                startReview();
            }
        } else if (gSerialLine == "e") {
            if (gLessonListActive) {
                if (!gLessonBatch.assignments.empty()) {
                    if (gLessonIndex >= gLessonBatch.assignments.size()) gLessonIndex = 0;
                    gLessonQuizActive = false;
                    gLessonListActive = false;
                    gLessonActive = true;
                    gLessonPage = 0;
                    gDirty = true;
                }
            } else if (gLessonQuizActive) {
                if (gLessonQuiz.complete()) {
                    gLessonQuizActive = false;
                    gLessonActive = true;
                } else if (gLessonQuiz.feedback()) {
                    gLessonQuiz.advance();
                    clearLessonQuizInput();
                } else {
                    checkLessonQuiz();
                }
                gDirty = true;
            } else if (gLessonActive) {
                startCurrentLesson();
            } else if (gReviewActive) {
                if (gFeedback) advanceFeedback();
                else checkCurrent();
            }
        } else if (gSerialLine == "q") {
            if (gFirmwareActive) {
                gFirmwareActive = false;
                gSettingsActive = true;
                gSettingsIndex = 10;
                gFirmwareConfirm = false;
                gDirty = true;
            } else if (gSettingsActive) {
                closeSettings();
            } else if (gLessonEmptyActive) {
                gLessonEmptyActive = false;
                gShowHome = true;
                releaseLessonData();
                gDirty = true;
            } else if (gLessonListActive) {
                gLessonListActive = false;
                gShowHome = true;
                releaseLessonData();
                gDirty = true;
            } else if (gLessonQuizActive) {
                gLessonQuizActive = false;
                gLessonActive = true;
                gDirty = true;
            } else if (gLessonActive) {
                gLessonActive = false;
                if (!gLessonBatch.assignments.empty() &&
                    gLessonIndex < gLessonBatch.assignments.size())
                    gLessonListActive = true;
                else {
                    gShowHome = true;
                    releaseLessonData();
                }
                gDirty = true;
            } else if (gReviewActive) {
                endReview();
            }
        } else if (gSerialLine.startsWith("y ")) {
            const String text = gSerialLine.substring(2);
            if (gLessonQuizActive && !gLessonQuiz.feedback()) {
                if (gLessonQuiz.part() == kk::AnswerKind::Meaning)
                    gLessonQuizInput += text.c_str();
                else
                    gLessonQuizDirectReading += text.c_str();
                gDirty = true;
            } else if (gReviewActive && !gFeedback) {
                if (gQueue.part() == kk::ReviewPart::Meaning)
                    gMeaningInput += text.c_str();
                else
                    gDirectReading += text.c_str();
                gDirty = true;
            }
        } else if (gSerialLine == "b") {
            if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                snprintf(gNetLine, sizeof gNetLine, "Batch: no Wi-Fi");
            } else {
                runBatch();
            }
        } else if (gSerialLine == "m") {
            if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                snprintf(gNetLine, sizeof gNetLine, "Summary: no Wi-Fi");
            } else {
                runSummary();
            }
        } else if (gSerialLine == "P") {
            startSetupPortal();
        } else if (gSerialLine.startsWith("U ")) {
            const String url = gSerialLine.substring(2);
            if (kk::ota::setManifestUrl(url))
                Serial.printf("[ota] manifest URL %s\n", kk::ota::manifestUrl().c_str());
            else
                Serial.println("[ota] manifest URL not saved");
        } else if (gSerialLine == "F") {
            firmwareCheck(true);
        } else if (gSerialLine == "I") {
            startFirmwareInstall();
        } else if (gSerialLine == "V") {
            openFirmware();
        } else if (gSerialLine == "s") {
            if (kk::DevWifi::connected()) {
                Serial.println("[wifi] disconnect first");
            } else {
                WiFi.mode(WIFI_STA);
                const int n = WiFi.scanNetworks();
                Serial.printf("[wifi] %d networks\n", n);
                for (int i = 0; i < n; ++i)
                    Serial.printf("[wifi] %2d  %4d dBm  %s\n", i, WiFi.RSSI(i),
                                  WiFi.SSID(i).c_str());
                WiFi.scanDelete();
                WiFi.mode(WIFI_OFF);
            }
        } else if (gSerialLine == "C") {
            seedDevCache();
        } else if (gSerialLine == "X") {
            clearDevCache();
        } else if (gSerialLine == "J") {
            seedDevRejectedOutbox();
        } else if (gSerialLine == "T") {
            if (kk::DevToken::save("dev-token")) {
                Serial.println("[auth] dev token saved (no probe)");
                gHaveUser = false;
            } else {
                Serial.println("[auth] dev token save failed");
            }
        } else if (gSerialLine == "L") {
            seedDevLesson();
        } else if (gSerialLine == "E") {
            const uint32_t now = static_cast<uint32_t>(time(nullptr));
            for (kk::SummaryBucket& bucket : gSummary.reviews)
                bucket.availableAt = now + 3600;
            gDueReviews = 0;
            gShowHome = true;
            gDirty = true;
            Serial.println("[summary] dev due cleared");
        } else if (gSerialLine == "H") {
            // Failed-fetch path: keep the summary but drop the cached batch and
            // add one lesson id, so Enter/l with Wi-Fi off reaches the notices.
            gLessonBatch = kk::LessonBatch{};
            gLessonComponents.clear();
            gBatch = kk::ReviewBatch{};
            gBatchEvicted = false;
            gCachedBatchCount = 0;
            if (gSdOk) kk::cacheClearSubjects(SD);
            gSummary.lessons.clear();
            kk::SummaryBucket lesson;
            lesson.availableAt = static_cast<uint32_t>(time(nullptr));
            lesson.count = 1;
            lesson.subjectIds = {1};
            gSummary.lessons.push_back(lesson);
            gLessons = 1;
            gShowHome = true;
            gDirty = true;
            Serial.println("[dev] cached batch dropped, one lesson bucket seeded");
        } else if (gSerialLine.startsWith("A ")) {
            gDevLessonStartCode = gSerialLine.substring(2).toInt();
            Serial.printf("[lesson] fake start code %d\n", gDevLessonStartCode);
        } else if (gSerialLine == "A") {
            gDevLessonStartCode = -1;
            Serial.println("[lesson] fake start code off");
        } else if (gSerialLine == "B") {
            if (!gSdOk) {
                Serial.println("[pack] no SD");
            } else {
                kk::GlyphPack bad;
                const bool opened = bad.open(SD, "/kanikey/glyphs-bad.kgp", true);
                Serial.printf("[pack] bad-crc test opened=%d status=%s\n", opened ? 1 : 0,
                              bad.statusLine());
                gPack.close();
                gPack.open(SD, "/kanikey/glyphs.kgp");
                Serial.printf("[pack] restored: %s\n", gPack.statusLine());
            }
        } else if (gSerialLine == "O") {
            gDevForceBattery = !gDevForceBattery;
            Serial.printf("[power] force battery %d\n", gDevForceBattery ? 1 : 0);
        } else if (gSerialLine.startsWith("Z ")) {
            const int seconds = gSerialLine.substring(2).toInt();
            if (kk::settingsSetSleepSeconds(seconds))
                Serial.printf("[settings] sleep %d s\n", kk::settingsSleepSeconds());
            else
                Serial.println("[settings] sleep value rejected");
        } else if (gSerialLine.startsWith("G ")) {
            const int count = gSerialLine.substring(2).toInt();
            gDevKeyTest = true;
            for (int i = 0; i < count; ++i) {
                KeyEvent e;
                e.key = Key::Other;
                e.x = 255;
                e.y = 255;
                e.down = (i % 2) == 0;
                tgc::board::devPushKeyEvent(e);
            }
            Serial.printf("[key] pushed %d events\n", count);
        } else if (gSerialLine == "Y") {
            gOtaState.failedVersion.clear();
            kk::ota::saveFailedVersion("");
            kk::ota::clearRollbackRecord();
            firmwareNote("Rollback record cleared", kOk);
            Serial.println("[ota] rollback record cleared");
        } else if (gSerialLine.startsWith("K ")) {
            injectKeys(gSerialLine.substring(2));
        } else if (gSerialLine == "N") {
            // Drop the canvas so the next frames take the "drawing directly"
            // path that a fragmented heap forces.
            releaseCanvas();
            gDirty = true;
            Serial.println("[kk] canvas released; drawing directly");
        } else if (gSerialLine == "W") {
            // Debug aid: a loop blocked for 5 s panics with a backtrace.
            // Long requests and the pack CRC trip it too, so arm it only
            // right before the step under test.
            enableLoopWDT();
            Serial.println("[kk] loop watchdog on (5 s)");
        } else if (gSerialLine.startsWith("R")) {
            String arg = gSerialLine.substring(1);
            arg.trim();
            gRadicalTestId = arg.length() ? static_cast<uint32_t>(arg.toInt()) : 1;
            gShowRadicalTest = true;
            gShowDebug = false;
            gDirty = true;
            Serial.printf("[test] radical %u\n", static_cast<unsigned>(gRadicalTestId));
#endif
        } else if (!gSerialLine.isEmpty()) {
#ifdef KANIKEY_DEV
            Serial.printf("[kk] commands: w=wifi w <ssid>|<pass> t=tls u=user m=summary b=batch v=review l=lessons g=page D=review-detail Q=lesson-quiz y<text> e=enter q=quit d=wifi-off o=outbox f=flush x=test-queue z=clear J=attention-outbox Y=clear-rollback C=dev-cache X=clear-cache K <keys>=type R<id>=radical U <url>=manifest F=firmware-check I=firmware-install V=firmware-screen k <token> c=clear h=heap i=pack S=asset-server p=picture s=scan r=restart up <bytes> <path>=sd-upload T=dev-token L=radical-lesson E=zero-due H=drop-batch A<code>=fake-start B=bad-pack-crc O=force-battery Z<s>=sleep G<n>=key-storm\n");
#else
            Serial.printf("[kk] commands: h=heap i=pack S=asset-server p=picture r=restart up <bytes> <path>=sd-upload\n");
#endif
        }
        gSerialLine = "";
        gDirty = true;
    }
}

// Shared boot tail: load the cache when storage is available, then start the
// setup portal or a sync. Used by setup() and after the storage prompt.
void continueBootAfterStorage() {
    const bool cacheLoaded = gSdOk && gPack.ready() && loadCache();
    Serial.printf("[wifi] saved credentials: %s\n", kk::DevWifi::hasSaved() ? "yes" : "no");
    if (!gSdOk) {
        snprintf(gNetLine, sizeof gNetLine, "SD: not mounted");
    } else if (!gPack.ready()) {
        snprintf(gNetLine, sizeof gNetLine, "Pack: %s", gPack.statusLine());
    } else if (!kk::DevToken::has() || !kk::DevWifi::hasSaved()) {
        if (cacheLoaded)
            snprintf(gNetLine, sizeof gNetLine, "NET: offline (cached)");
        else
            startSetupPortal();
    } else {
        startSync();
    }
    gDirty = true;
    drawStatus();
    gLastStatusMs = millis();
}

}  // namespace

void setup() {
    Serial.setRxBufferSize(4096);
    Serial.begin(115200);
    delay(250);

    tgc::board::begin();
    gDisp = &tgc::board::display();
    gDisp->setRotation(1);
    gDisp->setBrightness(kk::settingsBrightness());
    kk::settingsApplyTimeZone();

    ensureCanvas();
    kk::wkSetRequestHooks(requestBefore, requestAfter);
    firmwareBoot();

    Serial.printf("\n[kk] KaniKey boot\n");
    Serial.printf("[kk] version %s (%s)\n", kk::kFirmwareVersion, kk::ota::buildEnv());
    Serial.printf("[kk] canvas: %s (%u bytes, %d bpp)\n", gCanvas ? "ok" : "none",
                  static_cast<unsigned>(gCanvas ? gCanvas->bufferLength() : 0),
                  static_cast<int>(gCanvas ? gCanvas->getColorDepth() : 0));
    Serial.printf("[kk] board: %s\n", tgc::board::name());
    Serial.printf("[kk] flash: %u MB, PSRAM: %u MB\n",
                  static_cast<unsigned>(ESP.getFlashChipSize() / (1024 * 1024)),
                  static_cast<unsigned>(ESP.getPsramSize() / (1024 * 1024)));
    Serial.printf("[kk] internal heap: %u free, %u largest\n",
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                         MALLOC_CAP_8BIT)));
    Serial.printf("[kk] keyboard: %s\n", tgc::board::hasKeyboard() ? "yes" : "no");

    // Mount without formatting first: a failed mount must not wipe unsent
    // reviews. The format choice is explicit on the storage prompt.
    gFsOk = LittleFS.begin(false);
    if (gFsOk) {
        kk::outboxLoad(gOutbox);
        Serial.printf("[outbox] pending %u\n",
                      static_cast<unsigned>(kk::outboxPendingCount(gOutbox)));
    } else {
        Serial.println("[kk] LittleFS mount failed");
        gFsFormatPrompt = true;
        gNoticeAction = NoticeAction::StorageFormat;
        gNoticeActive = true;
        snprintf(gNoticeLine, sizeof gNoticeLine, "Pending reviews cannot be read.");
        gNoticeLine2[0] = 0;
    }

    gSdOk = mountSd();
    if (gSdOk) {
        gPack.open(SD, "/kanikey/glyphs.kgp");
        Serial.printf("[pack] %s\n", gPack.statusLine());
    }
    if (gFsFormatPrompt) {
        gLastInputAt = millis();
        gDirty = true;
        Serial.println("[kk] waiting for the storage choice");
        draw();
        gDirty = false;
        gLastDrawMs = millis();
        return;  // no sync or review before the choice
    }
    gLastInputAt = millis();
    gDirty = true;
    Serial.println("[kk] first draw");
    draw();
    Serial.println("[kk] first draw done");
    gDirty = false;
    gLastDrawMs = millis();

    continueBootAfterStorage();
}

void loop() {
    tgc::board::update();
    handleSerial();
    // The portal closes itself after 15 idle minutes; leave gPortalActive set
    // until here so the hooks can keep their own messages.
    if (gPortalActive) {
        gSetupPortal.poll();
        if (gPortalActive && !gSetupPortal.active()) {
            gPortalActive = false;
            snprintf(gNetLine, sizeof gNetLine, "Setup timed out");
            gDirty = true;
        } else if (gSetupPortal.takeRedrawRequest()) {
            gDirty = true;
        }
    }
    if (!gPortalActive) serviceAssetServer();
    if (gPackRecheckPending) {
        // Deferred from portalRecheckPack(): the HTTP request has returned, so
        // the new handle cannot be invalidated by the client closing.
        gPackRecheckPending = false;
        if (!gSdOk) gSdOk = mountSd();
        if (gSdOk) {
            gPack.open(SD, "/kanikey/glyphs.kgp", true);
            Serial.printf("[pack] %s\n", gPack.statusLine());
        }
        gDirty = true;
    }

    const uint32_t now = millis();
    firmwareTick(now);
    if (gScreenOff) {
        bool wake = false;
        KeyEvent ignored;
        while (tgc::board::nextKeyEvent(&ignored)) wake = true;
        if (tgc::board::wasPressed(tgc::board::Button::Select) ||
            tgc::board::wasHeld(tgc::board::Button::Select))
            wake = true;
        if (wake) {
            gLastInputAt = now;
            wakeScreen();
        } else {
            delay(10);
            return;
        }
    }

    if (gDimmed && (tgc::board::wasPressed(tgc::board::Button::Select) ||
                     tgc::board::wasHeld(tgc::board::Button::Select))) {
        gLastInputAt = now;
        wakeScreen();
    }

    bool firstKey = true;
    KeyEvent e;
    while (tgc::board::nextKeyEvent(&e)) {
        gLastInputAt = now;
        if (firstKey) {
            firstKey = false;
            if (gDimmed) wakeScreen();
        }
        if (e.key == Key::None) {
#ifdef KANIKEY_DEV
            if (gDevKeyTest) {
                Serial.println("[key] test marker (all released)");
                gDevKeyTest = false;
            }
#endif
            // The TCA8418 lost events: treat every key as released.
            gShift = gFn = gCtrl = gOpt = gAlt = false;
            pushLog("^ (all keys released)");
            gDirty = true;
            continue;
        }
        onKey(e);
    }

    if (gDirty && now - gLastDrawMs >= 16) {
        draw();
        gDirty = false;
        gLastDrawMs = now;
    }
    if (now - gLastStatusMs >= 1000) {
        drawStatus();
        gLastStatusMs = now;
        // Refresh the visible 429 countdown once per second, but not while a
        // blocking sync owns the loop. The last tick also clears the display
        // when the gate expires.
        static uint32_t lastRateWait = 0;
        const uint32_t rateWait = kk::wkRateLimitWaitSeconds();
        if (!gSyncing && gSyncIssue == SyncIssue::RateLimit &&
            (rateWait > 0 || lastRateWait > 0))
            gDirty = true;
        lastRateWait = rateWait;
    }
    // Home's battery and clock otherwise freeze until the next keypress.
    if (gShowHome && now - gLastHomeRefreshMs >= 30000) {
        gLastHomeRefreshMs = now;
        gDirty = true;
    }

    // G0: a short press wakes the screen; a long press is the future power
    // action. For M0 it just logs, so the button can be checked too.
    if (tgc::board::wasPressed(tgc::board::Button::Select)) {
        Serial.println("[key] G0 pressed");
    }
    if (tgc::board::wasHeld(tgc::board::Button::Select)) {
        Serial.println("[key] G0 held");
    }

    const uint32_t dimMs = static_cast<uint32_t>(kk::settingsDimSeconds()) * 1000u;
    const uint32_t blankMs = static_cast<uint32_t>(kk::settingsBlankSeconds()) * 1000u;
    const uint32_t sleepMs = static_cast<uint32_t>(kk::settingsSleepSeconds()) * 1000u;
    if (!gPortalActive && dimMs && !gDimmed && now - gLastInputAt > dimMs) {
        tgc::board::setBrightness(20);
        gDimmed = true;
    }
    if (!gPortalActive && blankMs && !gScreenOff && now - gLastInputAt > blankMs) {
        tgc::board::setDisplayOn(false);
        gScreenOff = true;
    }
    if (!gPortalActive && !gNoticeActive && !gReviewActive && !gReviewSummary && !gFeedback &&
        !gLessonActive && !gLessonListActive && !gLessonEmptyActive && !gLessonQuizActive &&
        !gPauseActive && !gSettingsActive && !gAboutActive && !gOutboxScreen &&
        !gStatsActive && !gStudyMenuActive && !gCriticalListActive && !gFirmwareActive &&
        gFirmwareStep == FirmwareStep::Idle &&
        // A deep-sleep wake is a reset, so sleeping while the new image is
        // pending would roll it back; the 10-minute window bounds the wait.
        !gOtaPending && !gLowBatteryPrompt && !gAssetServerActive && sleepMs &&
        now - gLastInputAt > sleepMs &&
        (devForceBattery() || !tgc::board::externalPower())) {
        Serial.println("[power] idle sleep");
        saveCacheSnapshot();
        firmwareBeforeSleep();
        tgc::board::deepSleep();
    }

    static uint32_t lastBeat = 0;
    if (now - lastBeat > 5000) {
        lastBeat = now;
        Serial.printf("[kk] alive heap=%u\n",
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    }
    delay(4);
}

}  // namespace kk::app

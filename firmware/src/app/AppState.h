#pragma once
// The application's shared state. App.cpp and every controller work on one
// AppState instance returned by state(); the fields keep the names they had
// when the code lived in one translation unit, so a move stays mechanical.
#include <Arduino.h>
#include <WiFi.h>

#include <cstdint>
#include <string>
#include <vector>

#include "app/StatsScreen.h"
#include "app/StudyScreens.h"
#include "app/SyncScreen.h"
#include "platform/GlyphPack.h"
#include "platform/Ota.h"
#include "platform/SetupPortal.h"
#include "platform/WkLessons.h"
#include "platform/WkReview.h"
#include "wk/AnswerChecker.h"
#include "wk/CriticalItems.h"
#include "wk/LessonQuiz.h"
#include "wk/LessonSession.h"
#include "wk/LevelProgress.h"
#include "wk/Outbox.h"
#include "wk/ReviewQueue.h"
#include "wk/RomajiIme.h"
#include "wk/SrsCounts.h"
#include "wk/Summary.h"
#include "wk/SyncSteps.h"
#include "wk/Types.h"
#include "wk/User.h"

namespace kk::app {

struct DisplayState {
    lgfx::LGFX_Device* gDisp = nullptr;
    lgfx::LGFX_Sprite* gCanvas = nullptr;
    bool gDirty = true;
    uint32_t gLastStatusMs = 0;
    uint32_t gLastDrawMs = 0;
    uint32_t gLastHomeRefreshMs = 0;
};

struct StorageState {
    bool gSdOk = false;
    bool gFsOk = false;
    bool gFsFormatPrompt = false;
#ifdef KANIKEY_DEV
    // Dev-only simulation: pretend the device is on battery so the idle-sleep
    // path can run while USB is attached for the serial log.
    bool gDevForceBattery = false;
#endif
    char gSdLine[48] = "SD: not mounted";
    kk::GlyphPack gPack;
    WiFiServer gAssetServer{8080};
    bool gAssetServerActive = false;
    // A portal "Check pack" that needs an open is deferred to loop(): a File
    // opened inside the HTTP request can lose its descriptor when the client
    // closes.
    bool gPackRecheckPending = false;
};

struct SessionState {
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
};

struct CacheState {
    kk::ReviewBatch gBatch;
    // The cached review batch is loaded on demand: Home and lessons do not need
    // its subjects, and keeping ~20 subjects alive during a lesson fetch left
    // the TLS handshake with less than 4 KB of free heap. When the batch lives
    // only on disk, the snapshot's assignment list is preserved on the next
    // save.
    bool gBatchEvicted = false;
    size_t gCachedBatchCount = 0;
    bool gOffline = false;
    uint32_t gCacheSavedAt = 0;
    // Last successful contact of each kind; mirrored into the snapshot so a
    // wake can pick quick, full or skip (SPEED-02).
    uint32_t gLastFullAt = 0;
    uint32_t gLastQuickAt = 0;
    uint32_t gLastUserAt = 0;
};

struct StageChange {
    bool valid = false;
    uint8_t from = 0;
    uint8_t to = 0;
};

struct ReviewState {
    kk::ReviewQueue gQueue;
    bool gReviewActive = false;
    bool gReviewSummary = false;
    bool gFeedback = false;
    // REV-06: millis() when correct feedback appeared; 0 when the auto-advance
    // timer is off.
    uint32_t gFeedbackCorrectAt = 0;
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
    uint32_t gDetailSubjectId = 0;
    bool gDetailFromCritical = false;
    uint32_t gPendingJournalHandle = 0;
};

struct OutboxState {
    std::vector<kk::OutboxItem> gOutbox;
    uint32_t gOutboxSelectedSeq = 0;
    char gOutboxNote[40] = "";
};

struct LessonState {
    kk::LessonBatch gLessonBatch;
    std::vector<kk::Subject> gLessonComponents;
    kk::LessonBrowse gLessonBrowse;
    size_t gLessonBatchLimit = 0;  // N for this list, clamped by the user setting
    kk::LessonQuiz gLessonQuiz;
    std::string gLessonQuizInput;
#ifdef KANIKEY_DEV
    std::string gLessonQuizDirectReading;
    int gDevLessonStartCode = -1;  // <0 = use the real PUT /start
    bool gDevKeyTest = false;      // log the order of the injected key events
#endif
    kk::RomajiIme gLessonQuizIme;
    // Batch quiz: the same queue as reviews, but every completion is local.
    kk::ReviewQueue gLessonBatchQueue;
    kk::AnswerCheck gLessonBatchCheck;
    kk::ReviewPart gLessonBatchPart = kk::ReviewPart::None;
    uint32_t gLessonBatchSubjectId = 0;
    std::string gLessonBatchInput;
    kk::RomajiIme gLessonBatchIme;
    bool gLessonBatchFeedback = false;
#ifdef KANIKEY_DEV
    std::string gLessonBatchDirectReading;
#endif
    // Confirmation, offline message and failed-items frame before/after start.
    kk::LessonStartPlan gLessonStartPlan;
    bool gLessonStartActive = false;
    bool gLessonStartFailed = false;
    bool gLessonStartOffline = false;
    bool gLessonActive = false;
    bool gLessonListActive = false;
    bool gLessonEmptyActive = false;
    bool gLessonQuizActive = false;       // single-item Q practice
    bool gLessonBatchQuizActive = false;  // quiz over the whole selected batch
    char gLessonNote[40] = "";
};

struct StatsState {
    bool gStatsActive = false;
    int gStatsPage = 0;
    kk::app::StatsHistoryView gStatsHistory;
    bool gStatsHistoryLoaded = false;
};

struct StudyState {
    std::vector<kk::CriticalItem> gCritical;
    bool gCriticalLoaded = false;
    std::vector<kk::Subject> gPracticeSubjects;
    std::vector<kk::Assignment> gPracticePool;
    bool gPracticePoolLoaded = false;
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
};

enum class SyncIssue : uint8_t { None, Network, Auth, Forbidden, RateLimit, Server, Other };

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

// One non-modal or modal sync in progress (SPEED-03). Each loop() pass performs
// at most one request from this state. `sub` is the request index inside a
// coarse step (Outbox item, Level count, SRS bucket).
struct SyncJobState {
    kk::SyncKind kind = kk::SyncKind::Skip;
    kk::SyncStep step = kk::SyncStep::Idle;
    uint8_t sub = 0;
    bool active = false;
    bool modal = false;
    bool interrupted = false;
    bool startReviewPending = false;
    bool needsUser = false;
    bool coldBoot = false;
    uint32_t startedAt = 0;
    uint32_t timeStartedAt = 0;
    uint32_t outboxLastSeq = 0;
    uint16_t levelTotal = 0;
    uint16_t levelPassed = 0;
    uint32_t srs[5] = {};
};

struct SyncState {
    bool gSyncing = false;
    bool gSyncAttempted = false;
    SyncIssue gSyncIssue = SyncIssue::None;
    // An outbox 401/403 must survive a successful /user or /summary later in
    // the same sync; it is reapplied before the sync screen closes.
    SyncIssue gDeferredAuthIssue = SyncIssue::None;
    // True when the current Auth/Forbidden gSyncIssue came from the outbox, so
    // a later drain can clear it without hiding a failed probe/summary.
    bool gOutboxAuthIssue = false;
    SyncRow gSyncRows[kSyncRowCount];
    uint8_t gSyncCompleted = 0;
    bool gSyncScreen = false;
    bool gSyncFinished = false;
    bool gSyncCancelRequested = false;
    SyncJobState gSyncJob;
};

enum class SettingsOrigin : uint8_t { Home, Pause };

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

struct LogLine {
    char text[44];
};

struct UiState {
    uint32_t gLastInputAt = 0;
    bool gDimmed = false;
    bool gScreenOff = false;
    char gNetLine[48] = "NET: not tested";
    String gSerialLine;
    bool gPauseActive = false;
    int gPauseIndex = 0;
    bool gSettingsActive = false;
    // -1 shows the section list; otherwise the index into settingsSectionName().
    int gSettingsSection = -1;
    int gSettingsRow = 0;
    bool gSettingsResetConfirm = false;
    SettingsOrigin gSettingsOrigin = SettingsOrigin::Home;
    bool gAboutActive = false;
    bool gSignOutConfirm = false;
    bool gOutboxScreen = false;
    bool gOutboxConfirm = false;
    bool gLowBatteryPrompt = false;
    int gLowBatteryAction = 0;  // 1 = review, 2 = lessons
    bool gLowBatteryConfirmed = false;
    bool gNoticeActive = false;
    NoticeAction gNoticeAction = NoticeAction::None;
    char gNoticeLine[64] = "";
    char gNoticeLine2[64] = "";
    char gSettingsNote[48] = "";
    uint16_t gSettingsNoteColor = kk::ui::kFaint;
    bool gShift = false;
    bool gFn = false;
    bool gCtrl = false;
    bool gOpt = false;
    bool gAlt = false;
    LogLine gLog[3];
    // TLS needs one large block; the canvas is freed only for the request.
    bool gFreeCanvasForRequest = false;
};

enum class FirmwareStep : uint8_t { Idle, Checking, Installing, Restarting };

struct OtaState {
    kk::ota::SavedState gOtaState;
    bool gOtaPending = false;
    uint32_t gOtaLoopAt = 0;
    bool gFirmwareActive = false;
    int gFirmwareIndex = 0;
    FirmwareStep gFirmwareStep = FirmwareStep::Idle;
    bool gFirmwareChecked = false;
    bool gFirmwareConfirm = false;
    kk::ota::Manifest gFirmwareManifest;
    kk::ota::Offer gFirmwareOffer;
    char gFirmwareStatus[120] = "";
    uint16_t gFirmwareStatusColor = kk::ui::kDim;
    char gFirmwareNote[96] = "";
    uint16_t gFirmwareNoteColor = kk::ui::kFaint;
    int gFirmwareProgress = -1;
};

struct SetupState {
    kk::SetupPortal gSetupPortal;
    kk::SetupPortalHooks gSetupHooks;
    bool gPortalActive = false;
};

struct DevState {
#ifdef KANIKEY_DEV
    bool gShowDebug = false;
    bool gShowRadicalTest = false;
    uint32_t gRadicalTestId = 1;
#endif
};

struct AppState {
    DisplayState display;
    StorageState storage;
    SessionState session;
    CacheState cache;
    ReviewState review;
    OutboxState outbox;
    LessonState lesson;
    StatsState stats;
    StudyState study;
    SyncState sync;
    UiState ui;
    OtaState ota;
    SetupState setup;
    DevState dev;
};

namespace detail {
extern AppState gAppState;
}  // namespace detail

// Inline so the field accesses compile to the same direct loads and stores the
// old file-static globals had.
inline AppState& state() { return detail::gAppState; }

}  // namespace kk::app

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
#include "wk/HelpTable.h"
#include "wk/LessonQuiz.h"
#include "wk/LessonSession.h"
#include "wk/LevelProgress.h"
#include "wk/Outbox.h"
#include "wk/ReviewQueue.h"
#include "wk/RomajiIme.h"
#include "wk/ScreenModel.h"
#include "wk/SrsCounts.h"
#include "wk/SubmitPolicy.h"
#include "wk/Summary.h"
#include "wk/SyncSteps.h"
#include "wk/Types.h"
#include "wk/User.h"
#include "wk/WifiSetup.h"
#include "wk/WorkloadGuard.h"

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
    // ONB-01: /kanikey/glyphs.kgp (font, no WaniKani content) and
    // /kanikey/radicals.kgp (personal, token-generated). A legacy v1 combined
    // pack at glyphs.kgp still serves both roles.
    kk::GlyphPack gFontPack;
    kk::GlyphPack gRadicalPack;
    WiFiServer gAssetServer{8080};
    bool gAssetServerActive = false;
    // A portal "Check pack" that needs an open is deferred to loop(): a File
    // opened inside the HTTP request can lose its descriptor when the client
    // closes.
    bool gPackRecheckPending = false;
    // A pack just installed over HTTP already passed its transfer check
    // (SHA-256 for the font, structure/kind for the radical upload). Skip the
    // full-payload CRC reopen then: on a 9 MB font pack it takes seconds and
    // cannot show progress. "Check pack again" still runs the CRC.
    bool gPackRecheckSkipCrc = false;
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

// REV-11: one typed wrong answer for the summary rows. The text is capped at
// kMaxAnswerBytes; the vector keeps at most kSummaryWrongAnswers entries.
struct WrongAnswer {
    uint32_t assignmentId = 0;
    kk::ReviewPart part = kk::ReviewPart::None;
    std::string text;
};

// REV-11: the SRS pair of one completed assignment, recorded when its POST is
// answered. The stage tracker forgets successful sends, so the summary keeps
// its own bounded copy for the rows.
struct SummaryStage {
    uint32_t assignmentId = 0;
    uint8_t from = 0;
    uint8_t to = 0;
};

constexpr size_t kSummaryWrongAnswers = kk::kOutboxMax;
constexpr size_t kSummaryStages = kk::kOutboxMax;

struct ReviewState {
    kk::ReviewQueue gQueue;
    // Sub-state of Screen::Review. The screen itself lives in ScreenState.
    bool feedback = false;
    // REV-05: the user wrapped up; the header shows the pruned count until the
    // summary. Reset by startReview() and endReview().
    bool gWrapUp = false;
    // REV-06: millis() when correct feedback appeared; 0 when the auto-advance
    // timer is off.
    uint32_t feedbackCorrectAt = 0;
    // REV-01/03: Up/Down offset of the feedback body in px. Reset by each new
    // answer (checkCurrent); the draw clamps it to the content.
    int feedbackScroll = 0;
    // Meaning feedback while the item's reading is still to be asked: the
    // notes leave out any line with kana so the reading is not given away.
    bool feedbackHideKana = false;
    bool gNeedsSubmit = false;
    // Outbox record queued by the last completion; keeps its journal patch
    // paired with the POST (SPEED-01 sends tracked records in a loop).
    uint32_t gPendingSeq = 0;
    bool feedbackCompleted = false;
    // SPEED-01: pre-review SRS stages for every record queued this session,
    // and the live progress of a send loop (summary or review hint bar).
    kk::StageTracker gStageTracker;
    bool gSending = false;
    uint32_t gSendSent = 0;
    uint32_t gSendTotal = 0;
    bool detail = false;
    int detailPage = 0;
    int detailPages = 5;
    kk::AnswerCheck gCheck;
    kk::ReviewPart feedbackPart = kk::ReviewPart::None;
    uint32_t feedbackSubjectId = 0;
    std::string feedbackInput;
    kk::SessionItem gCompletedItem;
    StageChange gStageChange;
    // REV-11: summary rows and their per-item data. The wrong answers keep the
    // last typed text of each failed part; the stages keep the POST response.
    std::vector<WrongAnswer> gWrongAnswers;
    std::vector<SummaryStage> gSummaryStages;
    int gSummaryFocus = 0;     // 0 stats, 1 mistake list
    int gSummarySelected = 0;  // row inside the mistake list
    uint32_t gStageUps = 0;        // successful upward moves in the current session
    uint32_t gReviewStartMs = 0;   // millis() when the current session started
    uint32_t gReviewElapsedMs = 0; // latched when the summary is entered
    std::string gMeaningInput;
#ifdef KANIKEY_DEV
    std::string gDirectReading;  // test/serial reading input, bypasses the romaji IME
#endif
    kk::RomajiIme gIme;
    uint32_t detailSubjectId = 0;
    bool detailFromCritical = false;
    uint32_t gPendingJournalHandle = 0;
};

struct OutboxState {
    std::vector<kk::OutboxItem> gOutbox;
    uint32_t gOutboxSelectedSeq = 0;
    char gOutboxNote[40] = "";
    // Opening the outbox from Settings must return there on Esc, as it did
    // before the screen state machine.
    kk::ScreenState returnScreen;
};

struct LessonState {
    kk::LessonBatch gLessonBatch;
    std::vector<kk::Subject> gLessonComponents;
    kk::LessonBrowse gLessonBrowse;
    size_t gLessonBatchLimit = 0;  // N for this list, clamped by the user setting
    kk::LessonQuiz gLessonQuiz;
    int gLessonQuizScroll = 0;  // REV-01/03 feedback body offset (px)
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
    int gLessonBatchScroll = 0;  // REV-01/03 feedback body offset (px)
#ifdef KANIKEY_DEV
    std::string gLessonBatchDirectReading;
#endif
    // Confirmation, offline message and failed-items frame before/after start.
    // These are sub-state of Screen::LessonList (the start frame replaces the
    // list without leaving the lesson list state).
    kk::LessonStartPlan gLessonStartPlan;
    bool startConfirm = false;
    bool gLessonStartFailed = false;
    bool gLessonStartOffline = false;
    bool quizBatch = false;  // Screen::LessonQuiz: whole batch vs one item (Q)
    // LES-05 workload guard. It shares startConfirm with the start
    // confirmation so the existing key/draw dispatch reaches lessonStartKey().
    bool gLessonWorkloadActive = false;
    bool gLessonWorkloadConfirmed = false;
    uint8_t gLessonWorkloadReasons = 0;
    uint32_t gLessonWorkloadApprentice = 0;
    uint32_t gLessonWorkloadDue = 0;
    uint32_t gLessonWorkloadStartedToday = 0;
    kk::WorkloadThresholds gLessonWorkloadLimits;
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
    bool gSyncFinished = false;
    bool gSyncCancelRequested = false;
    SyncJobState gSyncJob;
};

// SETUP-09: the on-device Wi-Fi flow can replace the phone portal from Settings
// or from the boot "Wi-Fi needed" state.
enum class WifiSetupOrigin : uint8_t { Settings, Boot };
enum class WifiSetupStep : uint8_t { List, ManualSsid, Password, Joining };

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
    NeedsKeyboard,  // PD-03: a two-button board refused a keyboard-only action
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
    int gPauseIndex = 0;
    // -1 shows the section list; otherwise the index into settingsSectionName().
    int gSettingsSection = -1;
    int gSettingsRow = 0;
    bool gSettingsResetConfirm = false;
    int gLowBatteryAction = 0;  // 1 = review, 2 = lessons
    bool gLowBatteryConfirmed = false;
    // PD-03: the focused Home destination on a two-button board (Lessons, Stats,
    // Sync, Settings; see App.cpp homeFocusOpen()).
    uint8_t gHomeFocus = 0;
    bool gNoticeActive = false;
    NoticeAction gNoticeAction = NoticeAction::None;
    char gNoticeLine[64] = "";
    char gNoticeLine2[64] = "";
    char gSettingsNote[48] = "";
    uint16_t gSettingsNoteColor = kk::ui::kFaint;
    // CFG-04: the key help overlay. gHelpScreen picks the static table; Esc
    // closes it and every other key is swallowed.
    bool gHelpActive = false;
    kk::HelpScreen gHelpScreen = kk::HelpScreen::None;
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
    // The screen that opened Firmware (normally Settings above Home or Pause).
    // Esc restores it instead of guessing an origin flag.
    kk::ScreenState firmwareReturn;
    int gFirmwareIndex = 0;
    FirmwareStep gFirmwareStep = FirmwareStep::Idle;
    bool gFirmwareChecked = false;
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
};

// SETUP-09: the Cardputer's own scan list and password field. The password
// lives only in RAM and is cleared on success, cancel and leaving Settings.
struct WifiState {
    bool gWifiActive = false;
    WifiSetupOrigin gWifiOrigin = WifiSetupOrigin::Settings;
    WifiSetupStep gWifiStep = WifiSetupStep::List;
    std::vector<kk::WifiNetwork> gWifiNetworks;
    int gWifiIndex = 0;
    std::string gWifiSsid;
    std::string gWifiPassword;
    std::string gWifiManual;
    kk::WifiSecurity gWifiSecurity = kk::WifiSecurity::Unknown;
    bool gWifiShowPassword = false;
    kk::WifiJoinFailure gWifiFailure = kk::WifiJoinFailure::None;
    char gWifiNote[64] = "";
    uint16_t gWifiNoteColor = kk::ui::kFaint;
};

struct DevState {
#ifdef KANIKEY_DEV
    uint32_t gRadicalTestId = 1;
    // Tab and R are overlays on top of the real screen; remember what to show
    // again when the dev screen closes.
    kk::Screen debugReturn = kk::Screen::Home;
    kk::Screen radicalReturn = kk::Screen::Home;
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
    WifiState wifi;
    DevState dev;
    kk::ScreenState screen;
};

namespace detail {
extern AppState gAppState;
}  // namespace detail

// Inline so the field accesses compile to the same direct loads and stores the
// old file-static globals had.
inline AppState& state() { return detail::gAppState; }

}  // namespace kk::app

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
#include "app/HomeScreen.h"
#include "app/LessonScreens.h"
#include "app/LoadingScreen.h"
#include "app/ModalScreens.h"
#include "app/OutboxScreen.h"
#include "app/ReviewScreens.h"
#include "app/SettingsScreen.h"
#include "app/SetupScreen.h"
#include "app/SyncScreen.h"
#include "board/Board.h"
#include "Version.h"
#include "platform/CacheStore.h"
#include "platform/DeviceSettings.h"
#include "platform/DevToken.h"
#include "platform/DevWifi.h"
#include "platform/GlyphPack.h"
#include "platform/SetupPortal.h"
#include "platform/WkProbe.h"
#include "platform/OutboxStore.h"
#include "platform/WkClient.h"
#include "platform/WkLessons.h"
#include "platform/WkLevelProgress.h"
#include "platform/WkReview.h"
#include "platform/WkSrsCounts.h"
#include "wk/AnswerChecker.h"
#include "wk/LessonQuiz.h"
#include "wk/Outbox.h"
#include "wk/ReviewQueue.h"
#include "wk/ReviewResponse.h"
#include "wk/RomajiIme.h"
#include "wk/SessionStats.h"
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

bool gSdOk = false;
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
char gNextReview[12] = "--:--";
char gSyncClock[12] = "--:--";

kk::ReviewBatch gBatch;
kk::ReviewQueue gQueue;
bool gReviewActive = false;
bool gReviewSummary = false;
bool gFeedback = false;
bool gNeedsSubmit = false;
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
std::string gDirectReading;  // test/serial reading input, bypasses the romaji IME
kk::RomajiIme gIme;
std::vector<kk::OutboxItem> gOutbox;
kk::LessonBatch gLessonBatch;
std::vector<kk::Subject> gLessonComponents;
kk::LessonQuiz gLessonQuiz;
std::string gLessonQuizInput;
std::string gLessonQuizDirectReading;
kk::RomajiIme gLessonQuizIme;
kk::GlyphPack gPack;
WiFiServer gAssetServer(8080);
bool gAssetServerActive = false;
bool gLessonActive = false;
bool gLessonListActive = false;
bool gLessonEmptyActive = false;
bool gLessonQuizActive = false;
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
uint32_t gOutboxSelectedSeq = 0;
char gOutboxNote[40] = "";
bool gLowBatteryPrompt = false;
int gLowBatteryAction = 0;  // 1 = review, 2 = lessons
bool gLowBatteryConfirmed = false;
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
void drawSettings();
void drawAbout();
void drawSignOutConfirm();
void drawLowBattery();
void drawOutbox();
void openPause();

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

void applySummary(uint32_t now) {
    gDueReviews = gSummary.dueReviews(now);
    gLessons = gSummary.totalLessons();
    const uint32_t nextAt = gSummary.nextReviewAt(now);
    if (nextAt)
        localHm(nextAt, gNextReview, sizeof gNextReview);
    else if (gDueReviews > 0)
        snprintf(gNextReview, sizeof gNextReview, "now");
    else
        snprintf(gNextReview, sizeof gNextReview, "all clear");
}

bool outboxHasAssignment(uint32_t assignmentId) {
    for (const kk::OutboxItem& it : gOutbox)
        if (it.assignmentId == assignmentId) return true;
    return false;
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
    if (!gHaveSummary || !kk::DevToken::has()) return;
    kk::CacheSnapshot snapshot;
    snapshot.meta.savedAt = gSyncedAt;
    snapshot.meta.accountKey = currentAccountKey();
    snapshot.summary = gSummary;
    if (gHaveUser) snapshot.user = gUser;
    snapshot.levelProgress = gLevelProgress;
    snapshot.srs = gSrs;
    if (gBatch.ok) snapshot.assignments = gBatch.assignments;
    if (!kk::cacheSaveSnapshot(snapshot)) {
        Serial.println("[cache] snapshot save failed");
        return;
    }
    gCacheSavedAt = snapshot.meta.savedAt;
    Serial.printf("[cache] snapshot saved: %u assignments, %u due\n",
                  static_cast<unsigned>(snapshot.assignments.size()),
                  static_cast<unsigned>(gDueReviews));
}

void saveSubjectCache() {
    if (!gSdOk || !gBatch.ok || gBatch.subjects.empty() || !kk::DevToken::has()) return;
    kk::CacheMeta meta;
    meta.savedAt = gBatch.fetchedAt ? gBatch.fetchedAt : gSyncedAt;
    meta.accountKey = currentAccountKey();
    if (kk::cacheSaveSubjects(SD, meta, gBatch.subjects)) {
        Serial.printf("[cache] subjects saved: %u\n",
                      static_cast<unsigned>(gBatch.subjects.size()));
    } else {
        Serial.println("[cache] subjects save failed");
    }
}

void saveCacheAll() {
    saveCacheSnapshot();
    saveSubjectCache();
}

bool loadCache() {
    if (!gSdOk || !kk::DevToken::has()) return false;
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

    gBatch = kk::ReviewBatch{};
    if (!snapshot.assignments.empty()) {
        kk::SubjectCache subjects;
        if (kk::cacheLoadSubjects(SD, &subjects) && subjects.meta.accountKey == key) {
            for (const kk::Assignment& a : snapshot.assignments) {
                const kk::Subject* match = nullptr;
                for (const kk::Subject& s : subjects.subjects)
                    if (s.id == a.subjectId) {
                        match = &s;
                        break;
                    }
                if (!match) continue;
                gBatch.assignments.push_back(a);
                gBatch.subjects.push_back(*match);
            }
            // Keep component subjects too, for the offline detail card.
            for (const kk::Subject& s : subjects.subjects) {
                if (std::any_of(gBatch.subjects.begin(), gBatch.subjects.end(),
                                [&s](const kk::Subject& x) { return x.id == s.id; }))
                    continue;
                gBatch.subjects.push_back(s);
            }
            gBatch.fetchedAt = snapshot.meta.savedAt;
            gBatch.ok = !gBatch.assignments.empty();
        }
    }

    gCacheSavedAt = snapshot.meta.savedAt;
    gSyncedAt = snapshot.meta.savedAt;
    if (gSyncedAt) localHm(gSyncedAt, gSyncClock, sizeof gSyncClock);
    pruneCacheOutbox();
    applySummary(effectiveNow());
    gOffline = true;
    gShowHome = true;
    Serial.printf("[cache] loaded: %u assignments, %u subjects, %u due, saved %u\n",
                  static_cast<unsigned>(gBatch.assignments.size()),
                  static_cast<unsigned>(gBatch.subjects.size()),
                  static_cast<unsigned>(gDueReviews), static_cast<unsigned>(gCacheSavedAt));
    if (gSrs.valid)
        Serial.printf("[cache] srs A %u, G %u, M %u, E %u, B %u\n",
                      static_cast<unsigned>(gSrs.apprentice), static_cast<unsigned>(gSrs.guru),
                      static_cast<unsigned>(gSrs.master),
                      static_cast<unsigned>(gSrs.enlightened),
                      static_cast<unsigned>(gSrs.burned));
    return true;
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

bool runBatch() {
    if (!gHaveSummary) {
        Serial.println("[review] no summary yet");
        return false;
    }
    const size_t limit = static_cast<size_t>(kk::settingsReviewBatchSize());
    const uint32_t now = effectiveNow();
    const std::vector<uint32_t> ids = gSummary.dueSubjectIds(now, limit);
    Serial.printf("[review] %u due subject ids\n", static_cast<unsigned>(ids.size()));
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
    saveCacheAll();
    gDirty = true;
    return true;
}

const kk::Subject* subjectById(uint32_t id) {
    for (const kk::Subject& s : gBatch.subjects)
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
    gDirectReading.clear();
    gIme.clear();
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
        view.typed = part == kk::ReviewPart::Meaning
                         ? gMeaningInput
                         : (!gDirectReading.empty() ? gDirectReading : gIme.text());
        view.romaji = gIme.raw();
    } else {
        view.typed = gFeedbackInput;
        view.status = gCheck.status;
        view.message = gCheck.message;
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
    uint32_t subjectId = gFeedbackSubjectId;
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
                Serial.printf("[review] assignment %u SRS %u -> %u\n",
                              static_cast<unsigned>(assignmentId),
                              static_cast<unsigned>(gStageChange.from),
                              static_cast<unsigned>(gStageChange.to));
            }
        }
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

void queueReview(const kk::SessionItem& item) {
    gPendingStageAssignment = item.assignmentId;
    gPendingStageFrom = item.srsStage;
    uint32_t created = static_cast<uint32_t>(time(nullptr));
    if (created < 1700000000u) created = 0;  // clock not trusted; let the server stamp it
    const uint32_t seq = kk::outboxAdd(gOutbox, item.assignmentId, item.subjectId,
                                       item.incorrectMeaning, item.incorrectReading, created);
    if (!seq) {
        Serial.println("[outbox] full; review not queued");
        return;
    }
    saveOutbox();
    Serial.printf("[outbox] queued seq %u assignment %u\n", static_cast<unsigned>(seq),
                  static_cast<unsigned>(item.assignmentId));
    sendOutboxItem(seq);
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
    if (!kk::outboxSave(next)) {
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

void checkCurrent() {
    const kk::SessionItem* item = gQueue.current();
    if (!item) return;
    const kk::Subject* s = subjectById(item->subjectId);
    if (!s) return;
    const bool meaning = gQueue.part() == kk::ReviewPart::Meaning;
    const std::string input = meaning ? gMeaningInput
                                     : (!gDirectReading.empty() ? gDirectReading : gIme.text());
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
            gNeedsSubmit = true;
            gFeedbackCompleted = true;
            if (gDueReviews > 0) --gDueReviews;
            markCacheCompleted(gCompletedItem.subjectId, gCompletedItem.assignmentId);
        }
    }
    gDirty = true;
}

void advanceFeedback() {
    if (gCheck.status == kk::AnswerStatus::Warning) {
        clearReviewInput();
        gFeedback = false;
        gDirty = true;
        return;
    }
    if (gNeedsSubmit) {
        queueReview(gCompletedItem);
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
        queueReview(gCompletedItem);
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
    if (!gHaveSummary) return;
    gLessonQuizActive = false;
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
    const size_t reviewLimit = static_cast<size_t>(kk::settingsReviewBatchSize());
    if (gBatch.assignments.empty()) {
        char subtitle[48];
        snprintf(subtitle, sizeof subtitle, "%u due items, please wait",
                 static_cast<unsigned>(reviewLimit));
        drawLoading("Fetching reviews...", subtitle);
        flushOutbox();
        if (!runBatch()) {
            if (gBatch.httpCode == 401 || gBatch.httpCode == 403)
                setSyncIssue(gBatch.httpCode, true, 0, "Reviews");
            else if (kk::wkAuthBlocked())
                setSyncIssue(401, true, 0, "Reviews");
            drawLoading("Could not load reviews", gNetLine);
            return;
        }
    }
    if (kk::wkAuthBlocked()) {
        // Optional study materials/statistics/components can arm the gate after
        // the main batch succeeded; do not start a session with a rejected token.
        setSyncIssue(401, true, 0, "Reviews");
        drawLoading("Could not load reviews", gNetLine);
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
    gQueue.build(due, gBatch.subjects, now, reviewLimit);
    Serial.printf("[review] order %s, first levels",
                  gQueue.lowerLevelsFirst() ? "lower_levels_first" : "shuffled");
    for (size_t i = 0; i < gQueue.queue().size() && i < 8; ++i)
        Serial.printf(" %u", static_cast<unsigned>(gQueue.queue()[i].level));
    Serial.println();
    if (gQueue.empty()) {
        Serial.println("[review] no reviewable items");
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
        else if (e.key == Key::Tab) {
            const kk::Subject* detail = subjectById(gFeedbackSubjectId);
            gReviewDetailPages = detail ? detailPageCount(*detail) : 5;
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
        } else if (!gDirectReading.empty()) {
            gDirectReading.pop_back();
        } else {
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
            gDirectReading.clear();
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
    gLessonQuizDirectReading.clear();
    gLessonQuizIme.clear();
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
    const std::string input =
        meaning ? gLessonQuizInput
                : (!gLessonQuizDirectReading.empty() ? gLessonQuizDirectReading
                                                    : gLessonQuizIme.text());
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
        view.typed = meaning ? gLessonQuizInput
                             : (!gLessonQuizDirectReading.empty() ? gLessonQuizDirectReading
                                                                 : gLessonQuizIme.text());
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
            gLessonQuiz.advance();
            clearLessonQuizInput();
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
        } else if (!gLessonQuizDirectReading.empty()) {
            gLessonQuizDirectReading.pop_back();
        } else {
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
            gLessonQuizDirectReading.clear();
            gLessonQuizIme.feed(c);
        }
        gDirty = true;
    }
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
    gLessonEmptyActive = false;
    size_t lessonLimit = static_cast<size_t>(kk::settingsLessonBatchSize());
    if (gHaveUser && gUser.lessonsBatchSize > 0)
        lessonLimit = std::min(lessonLimit, static_cast<size_t>(gUser.lessonsBatchSize));
    if (lessonLimit == 0) lessonLimit = 1;
    const size_t candidateLimit = std::max<size_t>(lessonLimit, 10);
    char subtitle[48];
    snprintf(subtitle, sizeof subtitle, "up to %u, please wait",
             static_cast<unsigned>(candidateLimit));
    drawLoading("Fetching lessons...", subtitle);
    gFreeCanvasForRequest = true;
    gLessonBatch = kk::fetchLessonBatch(kk::DevToken::load(),
                                        gSummary.lessonSubjectIds(candidateLimit), candidateLimit);
    gFreeCanvasForRequest = false;
    if (!gLessonBatch.ok) {
        Serial.printf("[lesson] failed: %s\n", gLessonBatch.error.c_str());
        if (gLessonBatch.error == "no due lessons" || gLessonBatch.error == "no lessons") {
            gLessonEmptyActive = true;
            gShowHome = false;
            gDirty = true;
            return;
        }
        const char* lessonError = gLessonBatch.error.c_str();
        if (gLessonBatch.httpCode == 401 || gLessonBatch.httpCode == 403) {
            setSyncIssue(gLessonBatch.httpCode, true, 0, "Lessons");
            lessonError = gNetLine;
        } else if (kk::wkAuthBlocked()) {
            setSyncIssue(401, true, 0, "Lessons");
            lessonError = gNetLine;
        }
        drawLoading("No lessons available", lessonError);
        gShowHome = true;
        gDirty = true;
        return;
    }
    // The summary order is not the lesson order. Sort the candidates by level
    // and lesson_position, then keep the user's batch size.
    std::sort(gLessonBatch.assignments.begin(), gLessonBatch.assignments.end(),
              [](const kk::Assignment& a, const kk::Assignment& b) {
                  const kk::Subject* sa = lessonSubjectById(a.subjectId);
                  const kk::Subject* sb = lessonSubjectById(b.subjectId);
                  const int la = sa ? sa->level : 99;
                  const int lb = sb ? sb->level : 99;
                  if (la != lb) return la < lb;
                  if (a.lessonPosition != b.lessonPosition)
                      return a.lessonPosition < b.lessonPosition;
                  return a.id < b.id;
              });
    if (gLessonBatch.assignments.size() > lessonLimit)
        gLessonBatch.assignments.resize(lessonLimit);
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
        gShowHome = true;
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
    gFreeCanvasForRequest = true;
    const int httpCode = kk::startAssignment(kk::DevToken::load(), a.id,
                                             now >= 1700000000u ? now : 0);
    gFreeCanvasForRequest = false;
    if (httpCode >= 200 && httpCode < 300) {
        if (gLessons > 0) --gLessons;
        saveCacheSnapshot();
        ++gLessonIndex;
        gLessonPage = 0;
        if (gLessonIndex >= gLessonBatch.assignments.size()) {
            gLessonActive = false;
            gShowHome = true;
        }
    } else if (httpCode == 401 || httpCode == 403) {
        setSyncIssue(httpCode, true, 0, "Lesson");
        drawLoading("Could not start lesson", gNetLine);
        gLessonActive = false;
        gLessonListActive = false;
        gShowHome = true;
    }
    gDirty = true;
}

void lessonListKey(const KeyEvent& e) {
    if (!e.down) return;
    const size_t total = gLessonBatch.assignments.size();
    if (total == 0) {
        gLessonListActive = false;
        gShowHome = true;
        gDirty = true;
        return;
    }
    if (e.key == Key::Esc) {
        gLessonListActive = false;
        gShowHome = true;
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
        else
            gShowHome = true;
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

constexpr int kSettingsRows = 12;

void setSettingsNote(const char* text, uint16_t color = kFaint) {
    snprintf(gSettingsNote, sizeof gSettingsNote, "%s", text ? text : "");
    gSettingsNoteColor = color;
}

void openSettings(SettingsOrigin origin) {
    gSettingsActive = true;
    gAboutActive = false;
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
    gSignOutConfirm = false;
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
        case 7:
            if (gReviewActive) {
                setSettingsNote("End the review session first", kWarn);
                break;
            }
            gSettingsActive = false;
            gShowHome = false;
            startSetupPortal();
            break;
        case 8:
            if (!gSdOk || !gPack.ready() || !kk::DevToken::has() || !kk::DevWifi::hasSaved()) {
                setSettingsNote("Configure Wi-Fi, token and pack", kWarn);
                break;
            }
            gSettingsActive = false;
            gShowHome = false;
            startSync();
            break;
        case 9:
            gAboutActive = true;
            gSettingsNote[0] = 0;
            break;
        case 10:
            kk::cacheClearSnapshot();
            if (gSdOk) kk::cacheClearSubjects(SD);
            gCacheSavedAt = 0;
            setSettingsNote("Cache cleared; next boot is cold", kOk);
            break;
        case 11:
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
            kk::cacheClearSnapshot();
            if (gSdOk) kk::cacheClearSubjects(SD);
            gOutbox.clear();
            saveOutbox();
            gUser = kk::User{};
            gSummary = kk::Summary{};
            gLevelProgress = kk::LevelProgress{};
            clearLevelUpNotice();
            gSrs = kk::SrsCounts{};
            gHaveUser = false;
            gHaveSummary = false;
            gBatch = kk::ReviewBatch{};
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
        if (gShowHome && gHaveSummary)
            startReview();
        else
            retryBoot();
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
    static SPIClass spi(HSPI);
    spi.begin(pins.sck, pins.miso, pins.mosi, pins.cs);
    const size_t heapBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const uint32_t t0 = millis();
    if (!SD.begin(pins.cs, spi, 20000000, "/sd", 2)) {
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
    static const char* kLabels[kSyncRowCount] = {"Wi-Fi", "Outbox", "Account",
                                                 "Summary", "Level", "SRS"};
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

    kk::settingsApplyTimeZone();
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
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

    applyDeferredAuthIssue();
    gSyncing = false;
    gSyncFinished = true;
    gSyncScreen = false;
    Serial.println("[sync] complete");
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
    if (!gSdOk) gSdOk = mountSd();
    if (gSdOk) gPack.open(SD, "/kanikey/glyphs.kgp");
    return gPack.ready();
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
        gSetupHooks, "Wi-Fi connected. Enter your WaniKani token and press Finish when ready.");
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
                                      "Enter your WaniKani token and press Finish when ready.")) {
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
                                    "Review batch", "Order", "Lesson batch", "Setup portal",
                                    "Sync now", "About", "Clear cache", "Sign out"};
    String values[kSettingsRows];
    values[0] = String(kk::settingsBrightness());
    values[1] = secondsLabel(kk::settingsDimSeconds());
    values[2] = secondsLabel(kk::settingsBlankSeconds());
    values[3] = secondsLabel(kk::settingsSleepSeconds());
    values[4] = String(kk::settingsReviewBatchSize());
    values[5] = kk::settingsMeaningFirst() ? "Meaning" : "Reading";
    values[6] = String(kk::settingsLessonBatchSize());
    values[7] = "open";
    values[8] = "run";
    values[9] = "info";
    values[10] = "clear";
    values[11] = "out";

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

void drawAbout() {
    if (!gDisp) return;
    kk::app::AboutView view;
    view.version = kk::kFirmwareVersion;

    view.lines.push_back({"Build " __DATE__ " " __TIME__, kFaint, 52});
    view.lines.push_back({std::string("Pack: ") + gPack.statusLine(),
                          gPack.ready() ? kOk : kWarn, 63});
    char cache[64];
    snprintf(cache, sizeof cache, "Cache: %u batch, %u due",
             static_cast<unsigned>(gBatch.assignments.size()),
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
        kk::SummaryBucket{stamp - 60, 3, {1, 2, 3}});
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

void clearDevCache() {
    kk::cacheClearSnapshot();
    if (gSdOk) kk::cacheClearSubjects(SD);
    gHaveSummary = false;
    gHaveUser = false;
    gBatch = kk::ReviewBatch{};
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
        gSettingsActive || gPauseActive || gSyncScreen || gShowHome || gReviewActive ||
        gReviewSummary || gLessonListActive || gLessonEmptyActive || gLessonActive ||
        gOutboxScreen)
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
    if (SD.exists(path)) SD.remove(path);
    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        Serial.printf("[asset] cannot open %s\n", path.c_str());
        assetRespond(client, 500, "Cannot Open");
        stopAssetServer();
        return;
    }

    drawLoading("Wi-Fi upload", path.c_str());
    Serial.printf("[asset] writing %s (%d bytes)\n", path.c_str(), contentLength);
    uint8_t buf[1024];
    uint32_t got = 0;
    uint32_t lastData = millis();
    while (got < static_cast<uint32_t>(contentLength)) {
        const int avail = client.available();
        if (avail > 0) {
            const size_t take = std::min<size_t>(
                sizeof buf, std::min<size_t>(static_cast<size_t>(avail),
                                            contentLength - static_cast<int>(got)));
            const int n = client.read(buf, take);
            if (n > 0) {
                f.write(buf, n);
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
    if (got != static_cast<uint32_t>(contentLength)) SD.remove(path);
    Serial.printf("[asset] done %u/%d\n", got, contentLength);
    if (got == static_cast<uint32_t>(contentLength))
        assetRespond(client, 201, "Created");
    else
        assetRespond(client, 500, "Short Body");

    if (isPack && got == static_cast<uint32_t>(contentLength)) {
        gPack.open(SD, "/kanikey/glyphs.kgp");
        Serial.printf("[pack] %s\n", gPack.statusLine());
    }
    stopAssetServer();
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
    if (SD.exists(path)) SD.remove(path);
    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        Serial.printf("[up] cannot open %s\n", path.c_str());
        return;
    }

    drawLoading("Writing SD...", path.c_str());
    Serial.printf("[up] ready %s %u\n", path.c_str(), static_cast<unsigned>(size));
    static uint8_t buf[8192];
    uint32_t got = 0;
    uint32_t lastLog = millis();
    while (got < size) {
        const size_t want = std::min<size_t>(sizeof buf, size - got);
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
        f.write(buf, have);
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
    if (got != size) SD.remove(path);
    Serial.printf("[up] done %u/%u\n", static_cast<unsigned>(got),
                  static_cast<unsigned>(size));
    if (isPack && got == size) {
        if (gPack.open(SD, "/kanikey/glyphs.kgp"))
            Serial.printf("[pack] %s\n", gPack.statusLine());
        else
            Serial.printf("[pack] %s\n", gPack.statusLine());
    }
    gDirty = true;
}

void handleSerial() {
    if (Serial.available()) {
        gLastInputAt = millis();
        wakeScreen();
    }
    while (Serial.available()) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\r') continue;
        if (c != '\n') {
            if (gSerialLine.length() < 80) gSerialLine += c;
            continue;
        }
        gSerialLine.trim();
        if (gSerialLine.startsWith("up ")) {
            uploadSdFile(gSerialLine);
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
            if (gBatch.assignments.empty() && !kk::DevWifi::connected() &&
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
            if (gLessonEmptyActive) {
                gLessonEmptyActive = false;
                gShowHome = true;
                gDirty = true;
            } else if (gLessonListActive) {
                gLessonListActive = false;
                gShowHome = true;
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
                else
                    gShowHome = true;
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
        } else if (gSerialLine == "p") {
            dumpCanvas();
        } else if (gSerialLine == "h") {
            Serial.printf("[heap] free %u, largest %u, min %u\n",
                          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                          static_cast<unsigned>(heap_caps_get_largest_free_block(
                              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                          static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
#ifdef KANIKEY_DEV
        } else if (gSerialLine == "C") {
            seedDevCache();
        } else if (gSerialLine == "X") {
            clearDevCache();
        } else if (gSerialLine == "J") {
            seedDevRejectedOutbox();
        } else if (gSerialLine.startsWith("R")) {
            String arg = gSerialLine.substring(1);
            arg.trim();
            gRadicalTestId = arg.length() ? static_cast<uint32_t>(arg.toInt()) : 1;
            gShowRadicalTest = true;
            gShowDebug = false;
            gDirty = true;
            Serial.printf("[test] radical %u\n", static_cast<unsigned>(gRadicalTestId));
#endif
        } else if (gSerialLine == "i") {
            Serial.printf("[pack] %s\n", gPack.statusLine());
        } else if (gSerialLine == "S") {
            startAssetServer();
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
        } else if (gSerialLine == "r") {
            Serial.println("[kk] restart");
            delay(100);
            ESP.restart();
        } else if (!gSerialLine.isEmpty()) {
#ifdef KANIKEY_DEV
            const char* rejectedCmd = " J=attention-outbox";
#else
            const char* rejectedCmd = "";
#endif
            Serial.printf("[kk] commands: w=wifi w <ssid>|<pass> t=tls u=user m=summary b=batch v=review l=lessons g=page D=review-detail Q=lesson-quiz y<text> e=enter q=quit d=wifi-off o=outbox f=flush x=test-queue z=clear%s k <token> c=clear h=heap i=pack S=asset-server p=picture s=scan r=restart up <bytes> <path>=sd-upload\n", rejectedCmd);
        }
        gSerialLine = "";
        gDirty = true;
    }
}

}  // namespace

void setup() {
    Serial.setRxBufferSize(16384);
    Serial.begin(115200);
    delay(250);

    tgc::board::begin();
    gDisp = &tgc::board::display();
    gDisp->setRotation(1);
    gDisp->setBrightness(kk::settingsBrightness());
    kk::settingsApplyTimeZone();

    ensureCanvas();
    kk::wkSetRequestHooks(requestBefore, requestAfter);

    Serial.printf("\n[kk] KaniKey boot\n");
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

    if (!LittleFS.begin(true)) {
        Serial.println("[kk] LittleFS mount failed");
    } else {
        kk::outboxLoad(gOutbox);
        Serial.printf("[outbox] pending %u\n",
                      static_cast<unsigned>(kk::outboxPendingCount(gOutbox)));
    }

    gSdOk = mountSd();
    if (gSdOk) {
        gPack.open(SD, "/kanikey/glyphs.kgp");
        Serial.printf("[pack] %s\n", gPack.statusLine());
    }
    const bool cacheLoaded = gSdOk && gPack.ready() && loadCache();
    gLastInputAt = millis();
    gDirty = true;
    Serial.println("[kk] first draw");
    draw();
    Serial.println("[kk] first draw done");
    gDirty = false;
    gLastDrawMs = millis();

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

void loop() {
    tgc::board::update();
    handleSerial();
    if (gPortalActive) gSetupPortal.poll();
    if (!gPortalActive) serviceAssetServer();

    const uint32_t now = millis();
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
    if (!gPortalActive && !gReviewActive && !gReviewSummary && !gFeedback && !gLessonActive &&
        !gLessonListActive && !gLessonEmptyActive && !gPauseActive && !gSettingsActive &&
        !gAboutActive && !gOutboxScreen &&
        !gLowBatteryPrompt && !gAssetServerActive && sleepMs &&
        now - gLastInputAt > sleepMs && !tgc::board::externalPower()) {
        Serial.println("[power] idle sleep");
        saveCacheSnapshot();
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

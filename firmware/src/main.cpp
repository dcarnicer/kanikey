// KaniKey — first hardware milestone.
//
// What this build proves on a Cardputer ADV:
//   - the board layer starts (display, TCA8418 keyboard, battery);
//   - the keyboard matrix delivers down/up events and grid positions;
//   - the microSD mounts on SPI3 and reports its size;
//   - the internal heap is measured before any WaniKani/TLS work.
//
// It does not talk to WaniKani yet. The next milestone adds the Wi-Fi setup and
// a TLS GET /user with heap before/after, as described in docs/design.md.

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
#include "wk/Utf8.h"

namespace {

using tgc::Key;
using tgc::KeyEvent;

constexpr uint16_t kBg = 0x0000;
constexpr uint16_t kSurface = 0x0108;   // 8-bit: very dark teal-slate
constexpr uint16_t kSurfaceHi = 0x2128; // 8-bit: dark navy
constexpr uint16_t kLine = 0x2208;      // 8-bit: muted teal line
constexpr uint16_t kText = 0xFFFF;
constexpr uint16_t kDim = 0xA577;
constexpr uint16_t kFaint = 0x636F;
constexpr uint16_t kBrand = 0xE308;  // #FF7A59, 8-bit coral
constexpr uint16_t kOk = 0x5EF1;
constexpr uint16_t kOkBg = 0x0183;
constexpr uint16_t kAccent = 0x2B1B;
constexpr uint16_t kAccentHi = 0x7D5F;
constexpr uint16_t kReading = 0x2D14;
constexpr uint16_t kErr = 0xC945;
constexpr uint16_t kErrBg = 0x3882;
constexpr uint16_t kWarn = 0xFE26;

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
constexpr int kSyncRowCount = 6;
enum SyncRowIndex : uint8_t {
    SyncWifi,
    SyncOutbox,
    SyncAccount,
    SyncSummary,
    SyncLevel,
    SyncSrs,
};
enum class SyncRowState : uint8_t { Pending, Active, Done, Warn, Failed };
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
void drawReviewDetail();
std::string subjectMainMeaning(const kk::Subject& s);
void drawLesson();
void drawLessonList();
void drawLessonEmpty();
void drawLessonQuiz();
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
    snprintf(gNetLine, sizeof gNetLine, "API: probing...");
    gDirty = true;
    const kk::WkProbeResult r = kk::probeWaniKaniUser(token);
    if (r.parsed) {
        gUser = r.user;
        gHaveUser = true;
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

void runLevelProgress() {
    if (!gHaveUser) return;
    const kk::LevelProgress p = kk::fetchLevelProgress(kk::DevToken::load(), gUser.level);
    if (!p.valid) return;
    gLevelProgress = p;
    saveCacheSnapshot();
    char date[16];
    localDate(gLevelProgress.unlockedAt ? gLevelProgress.unlockedAt : gLevelProgress.startedAt,
              date, sizeof date);
    Serial.printf("[level] %u since %s\n", static_cast<unsigned>(gLevelProgress.level), date);
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

void drawPill(lgfx::LovyanGFX& d, int x, int y, const char* text, uint16_t fg, uint16_t bg) {
    d.setTextFont(0);
    d.setTextSize(1);
    const int w = d.textWidth(text) + 8;
    d.fillRect(x, y, w, 13, bg);
    d.setTextColor(fg, bg);
    d.drawString(text, x + 4, y + 2);
}

void drawWrapped(lgfx::LovyanGFX& d, const std::string& text, int x, int y, int w, int maxLines,
                 uint16_t fg, uint16_t bg) {
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(fg, bg);
    size_t start = 0;
    for (int line = 0; line < maxLines && start < text.size(); ++line) {
        size_t end = text.size();
        while (end > start && d.textWidth(text.substr(start, end - start).c_str()) > w) --end;
        if (end < text.size()) {
            const size_t sp = text.rfind(' ', end);
            if (sp != std::string::npos && sp > start) end = sp;
        }
        d.drawString(text.substr(start, end - start).c_str(), x, y + line * 8);
        start = end;
        while (start < text.size() && text[start] == ' ') ++start;
    }
}

void trimFrontToWidth(lgfx::LovyanGFX& d, std::string& text, int maxWidth) {
    while (text.size() > 1 && d.textWidth(text.c_str()) > maxWidth) {
        const char* p = text.c_str();
        kk::utf8Next(&p);
        const size_t n = static_cast<size_t>(p - text.c_str());
        if (n == 0 || n >= text.size()) break;
        text.erase(0, n);
    }
}

// Radicals use their own bitmap; everything else uses the character pack. The
// caller draws an explicit missing-glyph state when this returns false.
bool drawSubjectPrompt(lgfx::LovyanGFX& d, const kk::Subject& s, int cx, int cy,
                       int maxWidth, int maxHeight, int radicalMaxSide, uint16_t fg,
                       uint16_t bg, const kk::ClipRect& clip) {
    if (s.type == kk::SubjectType::Radical) {
        const int side = std::min({maxWidth, maxHeight, radicalMaxSide});
        if (gPack.drawRadical(d, s.id, cx, cy, side, side, fg, bg, clip)) return true;
    }
    if (!s.characters.empty() &&
        gPack.drawTextCentered(d, s.characters, 64, cx, cy, maxWidth, maxHeight, fg, bg,
                               clip))
        return true;
    return false;
}

void drawMissingGlyph(lgfx::LovyanGFX& d, int cx, int cy) {
    d.setTextDatum(lgfx::middle_center);
    d.setTextFont(4);
    d.setTextSize(1);
    d.setTextColor(kWarn, kSurface);
    d.drawString("?", cx, cy - 8);
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(kFaint, kSurface);
    d.drawString(gPack.ready() ? "no glyph" : "no pack", cx, cy + 12);
    d.drawString("check SD", cx, cy + 21);
    d.setTextDatum(lgfx::top_left);
}

// 20 px text that fits, or right-aligned when the reading is too wide. A
// missing glyph is different from a long reading; only the former says
// "no glyph".
bool drawJapaneseFit(lgfx::LovyanGFX& d, const std::string& text, int x, int right, int y,
                     int maxWidth, uint16_t fg, uint16_t bg, const kk::ClipRect& clip) {
    if (gPack.drawTextFit(d, text, x, y, 20, maxWidth, fg, bg, clip)) return true;
    if (gPack.covers(text, 20))
        return gPack.drawTextRight(d, text, right, y, 20, fg, bg, clip);
    return false;
}

void drawReview() {
    if (!gDisp) return;
    const kk::SessionItem* item = gQueue.current();
    const kk::Subject* s = gFeedback ? subjectById(gFeedbackSubjectId)
                                     : (item ? subjectById(item->subjectId) : nullptr);
    if (!s) return;
    const kk::ReviewPart part = gFeedback ? gFeedbackPart : gQueue.part();

    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(kDim, kBg);
    d.drawString("REVIEWS", 6, 3);
    const uint8_t srsStage =
        gFeedbackCompleted ? gCompletedItem.srsStage : (item ? item->srsStage : 0);
    char srs[24];
    if (s->hasReviewStats)
        snprintf(srs, sizeof srs, "SRS %u  %u%%", static_cast<unsigned>(srsStage),
                 static_cast<unsigned>(s->percentageCorrect));
    else
        snprintf(srs, sizeof srs, "SRS %u", static_cast<unsigned>(srsStage));
    d.setTextFont(0);
    d.setTextColor(kFaint, kBg);
    d.drawString(srs, 62, 5);
    const int total = static_cast<int>(gQueue.completedCount() + gQueue.remaining());
    int pos = static_cast<int>(gQueue.completedCount()) + 1;
    if (gFeedback && gFeedbackCompleted) pos = static_cast<int>(gQueue.completedCount());
    if (pos > total) pos = total;
    char right[24];
    snprintf(right, sizeof right, "%d/%d", pos, total);
    d.setTextFont(0);
    d.setTextDatum(lgfx::top_right);
    d.drawString(right, 233, 5);
    d.setTextDatum(lgfx::top_left);
    d.fillRect(0, 14, 240, 2, kSurfaceHi);
    if (total > 0) d.fillRect(0, 14, 240 * (pos - 1) / total, 2, kAccentHi);

    d.fillRect(4, 20, 88, 88, kSurface);
    d.drawRect(4, 20, 88, 88, kLine);
    if (!drawSubjectPrompt(d, *s, 48, 56, 78, 80, 64, kText, kSurface,
                           kk::ClipRect{6, 22, 90, 106}))
        drawMissingGlyph(d, 48, 56);
    char type[24];
    const char* kindName = s->type == kk::SubjectType::Vocabulary ? "VOCAB"
                           : s->type == kk::SubjectType::Radical ? "RADICAL"
                                                                  : "KANJI";
    snprintf(type, sizeof type, "%s L%d", kindName, s->level);
    d.setTextFont(0);
    d.setTextSize(1);
    drawPill(d, 48 - (d.textWidth(type) + 8) / 2, 92, type, kDim, kSurfaceHi);

    if (!gFeedback) {
        const bool meaning = part == kk::ReviewPart::Meaning;
        drawPill(d, 100, 20, meaning ? "MEANING" : "READING",
                 meaning ? kAccentHi : kReading, kSurfaceHi);
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(kDim, kBg);
        d.drawString(meaning ? "Type the meaning" : "Type the reading", 100, 38);

        d.fillRect(100, 52, 136, 32, kSurfaceHi);
        d.drawRect(100, 52, 136, 32, kLine);
        std::string shown = meaning ? gMeaningInput
                                     : (!gDirectReading.empty() ? gDirectReading : gIme.text());
        int caret = 106;
        if (shown.empty()) {
            d.setTextFont(1);
            d.setTextSize(1);
            d.setTextColor(kFaint, kSurfaceHi);
            d.drawString(meaning ? "answer" : "kana", 106, 60);
        } else if (meaning) {
            d.setTextFont(1);
            d.setTextSize(1);
            trimFrontToWidth(d, shown, 128);
            d.setTextColor(kText, kSurfaceHi);
            d.drawString(shown.c_str(), 106, 60);
            caret = 106 + d.textWidth(shown.c_str());
        } else if (gPack.drawTextRight(d, shown, 230, 80, 32, kText, kSurfaceHi,
                                       kk::ClipRect{102, 54, 234, 86}) ||
                   gPack.drawTextRight(d, shown, 230, 80, 20, kText, kSurfaceHi,
                                       kk::ClipRect{102, 54, 234, 86})) {
            caret = 230;
        } else {
            d.setTextFont(1);
            d.setTextSize(1);
            d.setTextColor(kWarn, kSurfaceHi);
            d.drawString("no glyph", 106, 60);
        }
        if (caret < 232) d.fillRect(caret, 58, 2, 16, kAccent);

        if (!meaning) {
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(kFaint, kBg);
            std::string raw = "romaji: " + gIme.raw();
            d.drawString(raw.c_str(), 100, 88);
        }
    } else {
        const bool ok = gCheck.status == kk::AnswerStatus::Correct;
        const bool warn = gCheck.status == kk::AnswerStatus::Warning;
        drawPill(d, 100, 20, ok ? "CORRECT" : warn ? "CHECK" : "NOT QUITE",
                 ok ? kOk : warn ? kWarn : kErr, ok ? kOkBg : warn ? kSurfaceHi : kErrBg);
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(kFaint, kBg);
        const bool japanese = part == kk::ReviewPart::Reading;
        std::string typed = gFeedbackInput;
        std::string answer = correctAnswer(*s, part);
        if (japanese) {
            // One-letter side labels leave the full panel width for long kana.
            d.drawString("Y", 100, 40);
            bool typedDrawn = false;
            if (!typed.empty())
                typedDrawn = drawJapaneseFit(d, typed, 108, 232, 56, 128, kDim, kBg,
                                             kk::ClipRect{108, 38, 236, 58});
            if (!typedDrawn) {
                d.setTextFont(1);
                d.setTextSize(1);
                d.setTextColor(kWarn, kBg);
                d.drawString(typed.empty() ? "-" : "no glyph", 108, 44);
            }
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(kFaint, kBg);
            d.drawString("A", 100, 72);
            bool answerDrawn = drawJapaneseFit(
                d, answer, 108, 232, 86, 128, ok ? kOk : warn ? kWarn : kErr, kBg,
                kk::ClipRect{108, 68, 236, 88});
            if (!answerDrawn) {
                d.setTextFont(1);
                d.setTextSize(1);
                d.setTextColor(kWarn, kBg);
                d.drawString("no glyph", 108, 74);
            }
            if (gCheck.status == kk::AnswerStatus::Correct) {
                if (!s->readingNote.empty())
                    drawWrapped(d, "Reading note: " + s->readingNote, 100, 96, 136, 2, kFaint,
                                kBg);
            } else {
                drawWrapped(d, gCheck.message, 100, 96, 136, 2, kFaint, kBg);
            }
        } else {
            d.drawString("You typed", 100, 38);
            d.setTextFont(1);
            d.setTextSize(1);
            d.setTextColor(kDim, kBg);
            while (typed.size() > 1 && d.textWidth(typed.c_str()) > 136) typed.erase(0, 1);
            d.drawString(typed.empty() ? "-" : typed.c_str(), 100, 47);
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(kFaint, kBg);
            d.drawString("Answer", 100, 65);
            d.setTextFont(1);
            d.setTextSize(1);
            d.setTextColor(ok ? kOk : warn ? kWarn : kErr, kBg);
            if (d.textWidth(answer.c_str()) > 136) d.setTextFont(0);
            while (answer.size() > 1 && d.textWidth(answer.c_str()) > 136) answer.pop_back();
            if (answer.size() < correctAnswer(*s, part).size()) answer += "...";
            d.drawString(answer.c_str(), 100, 74);
            if (gCheck.status == kk::AnswerStatus::Correct) {
                if (!s->meaningNote.empty())
                    drawWrapped(d, "Note: " + s->meaningNote, 100, 92, 136, 2, kFaint, kBg);
            } else {
                drawWrapped(d, gCheck.message, 100, 92, 136, 2, kFaint, kBg);
            }
        }
    }

    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(kFaint, kBg);
    d.drawString(gFeedback ? "Enter: next  Tab: details" : "Enter: check", 6, 125);
    d.setTextDatum(lgfx::top_right);
    d.drawString("Esc: quit", 233, 125);
    d.setTextDatum(lgfx::top_left);
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

void drawReviewDetail() {
    if (!gDisp) return;
    uint32_t subjectId = gFeedbackSubjectId;
    if (!subjectId && gQueue.current()) subjectId = gQueue.current()->subjectId;
    const kk::Subject* s = subjectById(subjectId);
    if (!s) return;

    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(kDim, kBg);
    d.drawString("ITEM DETAIL", 6, 3);
    char right[16];
    snprintf(right, sizeof right, "%d/%d", gReviewDetailPage + 1, gReviewDetailPages);
    d.setTextFont(0);
    d.setTextDatum(lgfx::top_right);
    d.drawString(right, 233, 5);
    d.setTextDatum(lgfx::top_left);
    d.fillRect(0, 14, 240, 2, kSurfaceHi);

    d.fillRect(4, 20, 74, 64, kSurface);
    d.drawRect(4, 20, 74, 64, kLine);
    if (!drawSubjectPrompt(d, *s, 41, 52, 70, 62, 60, kText, kSurface,
                           kk::ClipRect{6, 22, 76, 82}))
        drawMissingGlyph(d, 41, 52);
    const char* kind = s->type == kk::SubjectType::Vocabulary ? "VOCAB"
                       : s->type == kk::SubjectType::Radical ? "RADICAL"
                                                              : "KANJI";
    char type[24];
    snprintf(type, sizeof type, "%s L%d", kind, s->level);
    d.setTextFont(0);
    d.setTextSize(1);
    drawPill(d, 86, 20, type, kDim, kSurfaceHi);

    switch (detailPageKind(gReviewDetailPage, *s)) {
        case 0: {
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(kFaint, kBg);
            d.drawString("MEANINGS", 86, 38);
            std::string meanings;
            for (const kk::Meaning& m : s->meanings)
                if (m.accepted) {
                    if (!meanings.empty()) meanings += " / ";
                    meanings += m.text;
                }
            if (!s->meaningSynonyms.empty()) {
                meanings += "  + ";
                for (size_t i = 0; i < s->meaningSynonyms.size(); ++i) {
                    if (i) meanings += ", ";
                    meanings += s->meaningSynonyms[i];
                }
            }
            if (meanings.empty()) meanings = "-";
            drawWrapped(d, meanings, 86, 48, 150, 3, kText, kBg);
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(kFaint, kBg);
            d.drawString("R", 86, 82);
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
            if (reading.empty()) {
                d.drawString("none", 94, 88);
            } else if (!drawJapaneseFit(d, reading, 94, 232, 98, 142, kReading, kBg,
                                        kk::ClipRect{94, 80, 236, 100})) {
                d.setTextFont(1);
                d.setTextSize(1);
                d.setTextColor(kWarn, kBg);
                d.drawString("no glyph", 94, 88);
            }
            break;
        }
        case 1:
        case 2: {
            const bool meaning = gReviewDetailPage == 1;
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(kFaint, kBg);
            d.drawString(meaning ? "MEANING MNEMONIC" : "READING MNEMONIC", 86, 38);
            std::string text = meaning ? s->meaningMnemonic : s->readingMnemonic;
            if (text.empty()) text = "No mnemonic cached.";
            const std::string hint = meaning ? s->meaningHint : s->readingHint;
            if (!hint.empty()) text += "  Hint: " + hint;
            drawWrapped(d, text, 86, 48, 150, 6, kText, kBg);
            break;
        }
        case 3: {
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(kFaint, kBg);
            d.drawString("NOTES", 86, 38);
            std::string notes;
            if (!s->meaningNote.empty()) notes += "Meaning: " + s->meaningNote + " ";
            if (!s->readingNote.empty()) notes += "Reading: " + s->readingNote;
            if (notes.empty()) notes = "No study notes.";
            drawWrapped(d, notes, 86, 48, 150, 6, kText, kBg);
            break;
        }
        case 4: {
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(kFaint, kBg);
            d.drawString("COMPONENTS", 86, 38);
            int row = 0;
            for (uint32_t id : s->componentSubjectIds) {
                const kk::Subject* c = subjectById(id);
                if (!c) continue;
                if (row >= 4) break;
                const int y = 50 + row * 18;
                if (!c->characters.empty())
                    gPack.drawTextFit(d, c->characters, 86, y + 15, 20, 24, kText, kBg,
                                      kk::ClipRect{86, y, 112, y + 17});
                else {
                    d.setTextFont(0);
                    d.setTextSize(1);
                    d.setTextColor(kFaint, kBg);
                    d.drawString("RAD", 86, y + 4);
                }
                d.setTextFont(1);
                d.setTextSize(1);
                d.setTextColor(kDim, kBg);
                std::string meaning = subjectMainMeaning(*c);
                while (meaning.size() > 1 && d.textWidth(meaning.c_str()) > 116) meaning.pop_back();
                d.drawString(meaning.c_str(), 116, y + 3);
                ++row;
            }
            if (row == 0) {
                d.setTextFont(0);
                d.setTextSize(1);
                d.setTextColor(kFaint, kBg);
                d.drawString("No components cached.", 86, 50);
            }
            break;
        }
        case 5: {
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(kFaint, kBg);
            d.drawString("PARTS OF SPEECH", 86, 38);
            std::string parts;
            for (const std::string& p : s->partsOfSpeech) {
                if (!parts.empty()) parts += ", ";
                parts += p;
            }
            if (parts.empty()) parts = "No parts of speech.";
            drawWrapped(d, parts, 86, 48, 150, 2, kText, kBg);
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(kFaint, kBg);
            d.drawString("CONTEXT", 86, 68);
            std::string context;
            for (size_t i = 0; i < s->contextSentences.size() && i < 2; ++i) {
                const std::string& en = s->contextSentences[i].en;
                if (en.empty()) continue;
                if (!context.empty()) context += " ";
                context += en;
            }
            if (context.empty()) context = "No context sentences.";
            drawWrapped(d, context, 86, 78, 150, 3, kDim, kBg);
            break;
        }
        case 6: {
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(kFaint, kBg);
            d.drawString("RELATED", 86, 38);
            auto relatedLine = [&](const char* label, const std::vector<uint32_t>& ids,
                                   int y) {
                char head[40];
                snprintf(head, sizeof head, "%s: %u", label, static_cast<unsigned>(ids.size()));
                d.setTextFont(0);
                d.setTextSize(1);
                d.setTextColor(kDim, kBg);
                d.drawString(head, 86, y);
                std::string names;
                for (size_t i = 0; i < ids.size() && i < 2; ++i) {
                    const kk::Subject* r = subjectById(ids[i]);
                    if (!r) continue;
                    const std::string meaning = subjectMainMeaning(*r);
                    if (meaning.empty()) continue;
                    if (!names.empty()) names += ", ";
                    names += meaning;
                }
                if (!names.empty())
                    drawWrapped(d, names, 86, y + 10, 150, 1, kFaint, kBg);
            };
            relatedLine("Similar", s->visuallySimilarSubjectIds, 48);
            relatedLine("Amalgamations", s->amalgamationSubjectIds, 78);
            break;
        }
    }

    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(kFaint, kBg);
    d.drawString("Space: page  Tab/Esc: back", 5, 125);
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

void drawReviewSummary() {
    if (!gDisp) return;
    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(kDim, kBg);
    d.drawString("SESSION", 6, 3);

    const kk::SessionStats stats = kk::summarizeSession(gQueue.completed());
    char reviews[16];
    snprintf(reviews, sizeof reviews, "%u", static_cast<unsigned>(gQueue.completedCount()));
    char accuracy[16];
    snprintf(accuracy, sizeof accuracy, "%u%%", static_cast<unsigned>(stats.accuracy));
    char duration[16];
    kk::formatSessionDuration(gReviewElapsedMs, duration, sizeof duration);

    // 32 px values; a value that is too wide for its column falls back to 16 px
    // so the three columns never collide.
    auto value = [&d](const char* text, int x, int maxWidth) {
        d.setTextFont(2);
        d.setTextSize(2);
        if (d.textWidth(text) > maxWidth) d.setTextSize(1);
        d.setTextColor(kText, kBg);
        d.drawString(text, x, 30);
    };
    value(reviews, 8, 72);
    value(accuracy, 84, 74);
    value(duration, 164, 72);

    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(kDim, kBg);
    d.drawString("REVIEWS", 8, 66);
    d.drawString("ACCURACY", 84, 66);
    d.drawString("TIME", 164, 66);

    d.drawFastHLine(8, 77, 224, kLine);

    char line[48];
    if (gStageUps) {
        snprintf(line, sizeof line, "%u item%s moved up", static_cast<unsigned>(gStageUps),
                 gStageUps == 1 ? "" : "s");
        d.setTextColor(kOk, kBg);
    } else {
        snprintf(line, sizeof line, "No stage changes");
        d.setTextColor(kFaint, kBg);
    }
    d.drawString(line, 8, 84);

    snprintf(line, sizeof line, "%u mistake%s", static_cast<unsigned>(stats.mistakes),
             stats.mistakes == 1 ? "" : "s");
    d.setTextColor(kDim, kBg);
    d.drawString(line, 8, 98);

    const size_t pending = kk::outboxPendingCount(gOutbox);
    const size_t attention = kk::outboxAttentionCount(gOutbox);
    char outbox[40];
    if (attention)
        snprintf(outbox, sizeof outbox, "%u need attention", static_cast<unsigned>(attention));
    else if (pending)
        snprintf(outbox, sizeof outbox, "%u pending", static_cast<unsigned>(pending));
    else
        snprintf(outbox, sizeof outbox, "All reviews sent");
    d.setTextColor(attention ? kErr : pending ? kWarn : kFaint, kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString(outbox, 232, 98);
    d.setTextDatum(lgfx::top_left);

    const kk::Subject* lastSubject =
        gCompletedItem.subjectId ? subjectById(gCompletedItem.subjectId) : nullptr;
    if (gStageChange.valid || (lastSubject && lastSubject->hasReviewStats)) {
        char srs[48];
        if (gStageChange.valid && lastSubject && lastSubject->hasReviewStats) {
            if (gStageChange.from && gStageChange.from != gStageChange.to)
                snprintf(srs, sizeof srs, "SRS %u -> %u  %u%%",
                         static_cast<unsigned>(gStageChange.from),
                         static_cast<unsigned>(gStageChange.to),
                         static_cast<unsigned>(lastSubject->percentageCorrect));
            else
                snprintf(srs, sizeof srs, "SRS %u  %u%%",
                         static_cast<unsigned>(gStageChange.to),
                         static_cast<unsigned>(lastSubject->percentageCorrect));
        } else if (gStageChange.valid) {
            if (gStageChange.from && gStageChange.from != gStageChange.to)
                snprintf(srs, sizeof srs, "SRS %u -> %u",
                         static_cast<unsigned>(gStageChange.from),
                         static_cast<unsigned>(gStageChange.to));
            else
                snprintf(srs, sizeof srs, "SRS stage %u",
                         static_cast<unsigned>(gStageChange.to));
        } else {
            snprintf(srs, sizeof srs, "History %u reviews, %u%%",
                     static_cast<unsigned>(lastSubject->reviewCount()),
                     static_cast<unsigned>(lastSubject->percentageCorrect));
        }
        d.setTextColor(kOk, kBg);
        d.drawString(srs, 8, 112);
    }
    d.setTextColor(kFaint, kBg);
    d.drawString("Enter: home", 8, 125);
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

void drawLessonList() {
    if (!gDisp) return;
    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(kDim, kBg);
    d.drawString("LESSONS", 6, 3);

    const size_t total = gLessonBatch.assignments.size();
    if (total == 0 || gLessonIndex >= total) {
        d.endWrite();
        present();
        return;
    }
    char pos[20];
    snprintf(pos, sizeof pos, "%u/%u", static_cast<unsigned>(gLessonIndex + 1),
             static_cast<unsigned>(total));
    d.setTextFont(0);
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(kFaint, kBg);
    d.drawString(pos, 233, 5);
    d.setTextDatum(lgfx::top_left);
    d.fillRect(0, 14, 240, 2, kSurfaceHi);

    constexpr int kVisible = 5;
    int top = static_cast<int>(gLessonIndex) - kVisible / 2;
    if (top < 0) top = 0;
    if (total > static_cast<size_t>(kVisible) && top > static_cast<int>(total) - kVisible)
        top = static_cast<int>(total) - kVisible;
    if (total <= static_cast<size_t>(kVisible)) top = 0;
    for (int row = 0; row < kVisible && top + row < static_cast<int>(total); ++row) {
        const int i = top + row;
        const kk::Assignment& a = gLessonBatch.assignments[i];
        const kk::Subject* s = lessonSubjectById(a.subjectId);
        const int y = 18 + row * 20;
        if (i == static_cast<int>(gLessonIndex)) {
            d.fillRect(4, y - 1, 228, 19, kSurfaceHi);
            d.fillRect(4, y - 1, 2, 19, kAccent);
        }
        d.fillRect(9, y, 22, 20, kSurface);
        d.drawRect(9, y, 22, 20, kLine);
        bool promptDrawn = false;
        if (s) {
            if (s->type == kk::SubjectType::Radical) {
                promptDrawn = gPack.drawRadical(d, s->id, 20, y + 10, 20, 18, kText, kSurface,
                                                kk::ClipRect{10, y + 1, 30, y + 19});
            } else if (!s->characters.empty()) {
                std::string first = s->characters;
                const char* p = first.c_str();
                kk::utf8Next(&p);
                first.resize(static_cast<size_t>(p - first.c_str()));
                promptDrawn = gPack.drawTextCentered(d, first, 20, 20, y + 10, 20, 20, kText,
                                                     kSurface,
                                                     kk::ClipRect{10, y + 1, 30, y + 19});
            }
        }
        if (!promptDrawn) {
            d.setTextFont(1);
            d.setTextSize(1);
            d.setTextColor(kWarn, kSurface);
            d.setTextDatum(lgfx::middle_center);
            d.drawString("?", 20, y + 10);
            d.setTextDatum(lgfx::top_left);
        }

        const char* kind = !s ? "?"
                           : s->type == kk::SubjectType::Vocabulary ? "VOCAB"
                           : s->type == kk::SubjectType::Radical  ? "RAD"
                                                                  : "KANJI";
        char label[72];
        const std::string meaning = s ? subjectMainMeaning(*s) : std::string();
        snprintf(label, sizeof label, "%s L%d  %s", kind, s ? s->level : 0, meaning.c_str());
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(i == static_cast<int>(gLessonIndex) ? kText : kDim,
                       i == static_cast<int>(gLessonIndex) ? kSurfaceHi : kBg);
        char* end = label + strlen(label);
        while (end > label + 1 && d.textWidth(label) > 194) *--end = '\0';
        d.drawString(label, 34, y + 1);
        d.setTextColor(i == static_cast<int>(gLessonIndex) ? kAccentHi : kFaint,
                       i == static_cast<int>(gLessonIndex) ? kSurfaceHi : kBg);
        d.setTextDatum(lgfx::top_right);
        d.drawString(">", 230, y + 1);
        d.setTextDatum(lgfx::top_left);
    }

    if (total > kVisible) {
        constexpr int kTrackY = 18;
        constexpr int kTrackH = 100;
        d.fillRect(236, kTrackY, 2, kTrackH, kLine);
        const int thumbH = std::max(10, kTrackH * kVisible / static_cast<int>(total));
        const int thumbY =
            kTrackY + (kTrackH - thumbH) * top / static_cast<int>(total - kVisible);
        d.fillRect(236, thumbY, 2, thumbH, kAccentHi);
    }

    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(kFaint, kBg);
    d.drawString("Enter: open  Esc: home", 6, 125);
    d.endWrite();
    present();
}

void drawLessonEmpty() {
    if (!gDisp) return;
    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(kDim, kBg);
    d.drawString("LESSONS", 6, 3);
    d.setTextFont(4);
    d.setTextSize(1);
    d.setTextColor(kText, kBg);
    d.drawString("No lessons now", 8, 38);
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(kDim, kBg);
    String next = "Next review  ";
    next += gNextReview;
    d.drawString(next.c_str(), 8, 82);
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(kFaint, kBg);
    d.drawString("New lessons unlock as you level up.", 8, 102);
    d.drawString("Esc: home", 8, 125);
    d.endWrite();
    present();
}

void drawLesson() {
    if (!gDisp) return;
    const kk::Subject* s = currentLessonSubject();
    if (!s) return;
    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

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
    const std::vector<std::string> lines = wrapLines(d, mnemonic, 232);
    const size_t pages = (lines.size() + 2) / 3;
    if (gLessonPage >= pages) gLessonPage = pages - 1;

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(kDim, kBg);
    d.drawString("LESSON", 6, 3);
    const size_t total = gLessonBatch.assignments.size();
    char pos[20];
    snprintf(pos, sizeof pos, "%u/%u", static_cast<unsigned>(gLessonIndex + 1),
             static_cast<unsigned>(total));
    char page[20];
    snprintf(page, sizeof page, "p%u/%u", static_cast<unsigned>(gLessonPage + 1),
             static_cast<unsigned>(pages));
    // Keep both counters in the header row: the page is right-aligned and the
    // item position sits to its left. In the 6 px font, "p12/12" plus "10/10"
    // and the 8 px gap still leave the header's left side free.
    constexpr int kHeaderRight = 233;
    constexpr int kHeaderGap = 8;
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(kFaint, kBg);
    d.drawString(page, kHeaderRight, 5);
    d.setTextColor(kDim, kBg);
    d.drawString(pos, kHeaderRight - d.textWidth(page) - kHeaderGap, 5);
    d.setTextDatum(lgfx::top_left);

    d.fillRect(0, 14, 240, 2, kSurfaceHi);
    if (total > 0) {
        const int progressWidth = static_cast<int>(240u * gLessonIndex / total);
        d.fillRect(0, 14, progressWidth, 2, kAccentHi);
    }

    d.fillRect(4, 20, 74, 64, kSurface);
    d.drawRect(4, 20, 74, 64, kLine);
    if (!drawSubjectPrompt(d, *s, 41, 52, 70, 62, 60, kText, kSurface,
                           kk::ClipRect{6, 22, 76, 82}))
        drawMissingGlyph(d, 41, 52);

    const char* kind = s->type == kk::SubjectType::Vocabulary ? "VOCAB"
                       : s->type == kk::SubjectType::Radical ? "RADICAL"
                                                              : "KANJI";
    char type[24];
    snprintf(type, sizeof type, "%s L%d", kind, s->level);
    d.setTextFont(0);
    d.setTextSize(1);
    drawPill(d, 86, 20, type, kDim, kSurfaceHi);

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(kText, kBg);
    while (meaning.size() > 1 && d.textWidth(meaning.c_str()) > 148) meaning.pop_back();
    d.drawString(meaning.c_str(), 86, 38);

    if (!reading.empty()) {
        if (!gPack.drawTextFit(d, reading, 86, 80, 32, 148, kReading, kBg,
                               kk::ClipRect{86, 50, 236, 88}) &&
            !drawJapaneseFit(d, reading, 86, 232, 80, 148, kReading, kBg,
                             kk::ClipRect{86, 50, 236, 88})) {
            d.setTextFont(1);
            d.setTextSize(1);
            d.setTextColor(kWarn, kBg);
            d.drawString("no glyph", 86, 58);
        }
    }

    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(kDim, kBg);
    for (size_t i = 0; i < 3; ++i) {
        const size_t idx = gLessonPage * 3 + i;
        if (idx >= lines.size()) break;
        d.drawString(lines[idx].c_str(), 5, 92 + static_cast<int>(i) * 9);
    }

    d.setTextColor(kFaint, kBg);
    d.drawString("Space p  Q quiz", 5, 125);
    d.setTextDatum(lgfx::top_right);
    d.drawString("Enter start  Esc list", 233, 125);
    d.setTextDatum(lgfx::top_left);
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

void drawLessonQuiz() {
    if (!gDisp) return;
    const kk::Subject& s = gLessonQuiz.subject();
    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(kDim, kBg);
    d.drawString("LESSON QUIZ", 6, 3);
    const int total = gLessonQuiz.needsReading() ? 2 : 1;
    int pos = 1;
    if (gLessonQuiz.complete())
        pos = total;
    else if (gLessonQuiz.part() == kk::AnswerKind::Reading)
        pos = 2;
    char right[24];
    snprintf(right, sizeof right, "%d/%d", pos, total);
    d.setTextFont(0);
    d.setTextDatum(lgfx::top_right);
    d.drawString(right, 233, 5);
    d.setTextDatum(lgfx::top_left);
    d.fillRect(0, 14, 240, 2, kSurfaceHi);

    d.fillRect(4, 20, 74, 64, kSurface);
    d.drawRect(4, 20, 74, 64, kLine);
    if (!drawSubjectPrompt(d, s, 41, 52, 70, 62, 60, kText, kSurface,
                           kk::ClipRect{6, 22, 76, 82}))
        drawMissingGlyph(d, 41, 52);

    const bool meaning = gLessonQuiz.part() == kk::AnswerKind::Meaning;
    const kk::AnswerCheck& result = gLessonQuiz.result();
    if (gLessonQuiz.complete()) {
        drawPill(d, 86, 20, "DONE", kOk, kOkBg);
        d.setTextFont(1);
        d.setTextSize(1);
        d.setTextColor(kText, kBg);
        d.drawString("Practice complete", 86, 40);
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(kDim, kBg);
        char mistakes[32];
        snprintf(mistakes, sizeof mistakes, "%u mistake%s",
                 static_cast<unsigned>(gLessonQuiz.mistakes()),
                 gLessonQuiz.mistakes() == 1 ? "" : "s");
        d.drawString(mistakes, 86, 58);
        d.setTextColor(kFaint, kBg);
        d.drawString("No /start sent.", 86, 76);
        d.drawString("Enter: lesson card  Esc: card", 5, 125);
    } else if (gLessonQuiz.feedback()) {
        const bool ok = result.status == kk::AnswerStatus::Correct;
        const bool warn = result.status == kk::AnswerStatus::Warning;
        drawPill(d, 86, 20, ok ? "CORRECT" : warn ? "CHECK" : "NOT QUITE",
                 ok ? kOk : warn ? kWarn : kErr, ok ? kOkBg : warn ? kSurfaceHi : kErrBg);
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(kFaint, kBg);
        const bool japanese = !meaning;
        std::string typed = japanese
                                ? (!gLessonQuizDirectReading.empty() ? gLessonQuizDirectReading
                                                                    : gLessonQuizIme.text())
                                : gLessonQuizInput;
        const kk::ReviewPart part =
            meaning ? kk::ReviewPart::Meaning : kk::ReviewPart::Reading;
        std::string answer = correctAnswer(s, part);
        if (japanese) {
            d.drawString("Y", 86, 40);
            bool typedDrawn = false;
            if (!typed.empty())
                typedDrawn = drawJapaneseFit(d, typed, 94, 232, 56, 142, kDim, kBg,
                                             kk::ClipRect{94, 38, 236, 58});
            if (!typedDrawn) {
                d.setTextFont(1);
                d.setTextSize(1);
                d.setTextColor(kWarn, kBg);
                d.drawString(typed.empty() ? "-" : "no glyph", 94, 44);
            }
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(kFaint, kBg);
            d.drawString("A", 86, 72);
            bool answerDrawn = drawJapaneseFit(
                d, answer, 94, 232, 86, 142, ok ? kOk : warn ? kWarn : kErr, kBg,
                kk::ClipRect{94, 68, 236, 88});
            if (!answerDrawn) {
                d.setTextFont(1);
                d.setTextSize(1);
                d.setTextColor(kWarn, kBg);
                d.drawString("no glyph", 94, 74);
            }
            if (!ok)
                drawWrapped(d, result.message, 86, 96, 150, 2, kFaint, kBg);
            else if (!s.readingNote.empty())
                drawWrapped(d, "Reading note: " + s.readingNote, 86, 96, 150, 2, kFaint, kBg);
        } else {
            d.drawString("You typed", 86, 38);
            d.setTextFont(1);
            d.setTextSize(1);
            d.setTextColor(kDim, kBg);
            while (typed.size() > 1 && d.textWidth(typed.c_str()) > 150) typed.erase(0, 1);
            d.drawString(typed.empty() ? "-" : typed.c_str(), 86, 47);
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(kFaint, kBg);
            d.drawString("Answer", 86, 65);
            d.setTextFont(1);
            d.setTextSize(1);
            d.setTextColor(ok ? kOk : warn ? kWarn : kErr, kBg);
            while (answer.size() > 1 && d.textWidth(answer.c_str()) > 150) answer.pop_back();
            d.drawString(answer.c_str(), 86, 74);
            if (!ok)
                drawWrapped(d, result.message, 86, 96, 150, 2, kFaint, kBg);
            else if (!s.meaningNote.empty())
                drawWrapped(d, "Note: " + s.meaningNote, 86, 96, 150, 2, kFaint, kBg);
        }
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(kFaint, kBg);
        d.drawString("Enter: continue  Esc: card", 5, 125);
    } else {
        drawPill(d, 86, 20, meaning ? "MEANING" : "READING",
                 meaning ? kAccentHi : kReading, kSurfaceHi);
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(kDim, kBg);
        d.drawString(meaning ? "Type the meaning" : "Type the reading", 86, 38);

        d.fillRect(86, 52, 150, 32, kSurfaceHi);
        d.drawRect(86, 52, 150, 32, kLine);
        std::string shown = meaning
                                ? gLessonQuizInput
                                : (!gLessonQuizDirectReading.empty() ? gLessonQuizDirectReading
                                                                    : gLessonQuizIme.text());
        int caret = 92;
        if (shown.empty()) {
            d.setTextFont(1);
            d.setTextSize(1);
            d.setTextColor(kFaint, kSurfaceHi);
            d.drawString(meaning ? "answer" : "kana", 92, 60);
        } else if (meaning) {
            d.setTextFont(1);
            d.setTextSize(1);
            trimFrontToWidth(d, shown, 138);
            d.setTextColor(kText, kSurfaceHi);
            d.drawString(shown.c_str(), 92, 60);
            caret = 92 + d.textWidth(shown.c_str());
        } else if (gPack.drawTextRight(d, shown, 230, 80, 32, kText, kSurfaceHi,
                                       kk::ClipRect{88, 54, 234, 86}) ||
                   gPack.drawTextRight(d, shown, 230, 80, 20, kText, kSurfaceHi,
                                       kk::ClipRect{88, 54, 234, 86})) {
            caret = 230;
        } else {
            d.setTextFont(1);
            d.setTextSize(1);
            d.setTextColor(kWarn, kSurfaceHi);
            d.drawString("no glyph", 92, 60);
        }
        if (caret < 232) d.fillRect(caret, 58, 2, 16, kAccent);
        if (!meaning) {
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(kFaint, kBg);
            std::string raw = "romaji: " + gLessonQuizIme.raw();
            d.drawString(raw.c_str(), 86, 88);
        }
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(kFaint, kBg);
        d.drawString("Enter: check  Esc: card", 5, 125);
    }

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
    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(kBrand, kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(kText, kBg);
    d.drawString(title, 8, 56);
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(kDim, kBg);
    d.drawString(subtitle, 8, 76);
    d.setTextColor(kFaint, kBg);
    d.drawString("The first request can take a few seconds", 8, 98);
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
        char date[16];
        localDate(gLevelProgress.unlockedAt ? gLevelProgress.unlockedAt
                                            : gLevelProgress.startedAt,
                  date, sizeof date);
        char level[40];
        if (date[0])
            snprintf(level, sizeof level, "L%u since %s",
                     static_cast<unsigned>(gLevelProgress.level), date);
        else
            snprintf(level, sizeof level, "Level %u",
                     static_cast<unsigned>(gLevelProgress.level));
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
    kk::wkClearAuthBlock();
    gFreeCanvasForRequest = true;
    const kk::WkProbeResult r = kk::probeWaniKaniUser(token);
    gFreeCanvasForRequest = false;
    if (r.parsed) {
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

void drawHome() {
    if (!gDisp) return;
    static bool logged = false;
    if (!logged) {
        logged = true;
        Serial.println("[kk] home drawn");
    }
    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(kBrand, kBg);
    d.drawString("KaniKey", 6, 2);

    char sync[24];
    snprintf(sync, sizeof sync, "Synced %s", gSyncClock);
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(gOffline ? kWarn : kDim, kBg);
    d.drawString(sync, 198, 5);

    const int bat = tgc::board::batteryPercent();
    char right[20];
    if (bat >= 0)
        snprintf(right, sizeof right, "%d%%", bat);
    else
        snprintf(right, sizeof right, "USB");
    d.setTextColor(bat >= 0 && bat < 20 ? kWarn : kDim, kBg);
    d.drawString(right, 233, 5);

    d.setTextDatum(lgfx::top_left);
    d.setTextColor(GOLD, kBg);
    char lvl[16];
    snprintf(lvl, sizeof lvl, "LEVEL %d", gHaveUser ? gUser.level : 0);
    d.drawString(lvl, 7, 21);
    d.setTextColor(kDim, kBg);
    d.drawString(gHaveUser ? gUser.username.c_str() : "not signed in", 80, 21);

    d.setTextFont(2);
    d.setTextSize(2);
    d.setTextColor(kText, kBg);
    char due[16];
    snprintf(due, sizeof due, "%u", static_cast<unsigned>(gDueReviews));
    d.drawString(due, 8, 36);
    char lessons[16];
    snprintf(lessons, sizeof lessons, "%u", static_cast<unsigned>(gLessons));
    d.drawString(lessons, 164, 36);

    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(kDim, kBg);
    d.drawString("REVIEWS DUE", 8, 73);
    d.drawString("LESSONS", 164, 73);

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(gOffline ? kWarn : kDim, kBg);
    d.drawString("Next review", 8, 86);
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(kText, kBg);
    d.drawString(gNextReview, 232, 86);
    d.setTextDatum(lgfx::top_left);

    // SRS distribution: one segment per stage group, then A/G/M/E/B counts.
    d.setTextFont(0);
    d.setTextSize(1);
    if (gSrs.valid) {
        const uint32_t bucket[5] = {gSrs.apprentice, gSrs.guru, gSrs.master,
                                    gSrs.enlightened, gSrs.burned};
        const uint16_t colors[5] = {kErr, kWarn, kAccentHi, kReading, kFaint};
        const char label[5] = {'A', 'G', 'M', 'E', 'B'};
        const uint32_t total = gSrs.total();
        if (total > 0) {
            int x = 8;
            for (int i = 0; i < 5; ++i) {
                int w = 0;
                if (i == 4) {
                    w = 232 - x;
                } else {
                    w = static_cast<int>((static_cast<uint64_t>(bucket[i]) * 224u + total / 2u) /
                                         total);
                    if (bucket[i] > 0 && w < 3) w = 3;
                }
                if (w > 232 - x) w = 232 - x;
                if (w > 0) d.fillRect(x, 100, w - 1, 3, colors[i]);
                x += w;
            }
        }
        int x = 8;
        for (int i = 0; i < 5; ++i) {
            d.fillRect(x, 107, 4, 4, colors[i]);
            d.setTextColor(kDim, kBg);
            char count[16];
            snprintf(count, sizeof count, "%c %u", label[i], static_cast<unsigned>(bucket[i]));
            d.drawString(count, x + 5, 106);
            x += 43;
        }
    } else {
        d.setTextColor(kFaint, kBg);
        d.drawString("SRS: sync for counts", 8, 106);
    }

    d.setTextFont(0);
    d.setTextSize(1);
    const size_t pending = kk::outboxPendingCount(gOutbox);
    const size_t attention = kk::outboxAttentionCount(gOutbox);
    if (gSyncIssue == SyncIssue::Auth) {
        d.setTextColor(kErr, kBg);
        d.drawString("Auth: token rejected", 8, 117);
    } else if (gSyncIssue == SyncIssue::Forbidden) {
        d.setTextColor(kErr, kBg);
        d.drawString("Auth: missing permissions", 8, 117);
    } else if (gSyncIssue == SyncIssue::RateLimit) {
        d.setTextColor(kWarn, kBg);
        const uint32_t wait = kk::wkRateLimitWaitSeconds();
        if (wait) {
            char line[40];
            snprintf(line, sizeof line, "Rate limited; retry in %us",
                     static_cast<unsigned>(wait));
            d.drawString(line, 8, 117);
        } else {
            d.drawString("Rate limited; retry later", 8, 117);
        }
    } else if (gSyncIssue == SyncIssue::Server || gSyncIssue == SyncIssue::Other) {
        d.setTextColor(kWarn, kBg);
        d.drawString("WaniKani error; retry", 8, 117);
    } else if (attention) {
        char line[40];
        snprintf(line, sizeof line, "Outbox: %u need attention",
                 static_cast<unsigned>(attention));
        d.setTextColor(kErr, kBg);
        d.drawString(line, 8, 117);
    } else if (pending) {
        char line[40];
        snprintf(line, sizeof line, "Outbox: %u pending", static_cast<unsigned>(pending));
        d.setTextColor(kWarn, kBg);
        d.drawString(line, 8, 117);
    } else if (gOffline) {
        char line[40];
        cacheStatusLine(line, sizeof line);
        d.setTextColor(cacheIsStale() ? kErr : kWarn, kBg);
        d.drawString(line, 8, 117);
    } else if (gHaveUser && gUser.vacationStartedAt) {
        d.setTextColor(kWarn, kBg);
        d.drawString("Vacation mode", 8, 117);
    } else if (gLevelProgress.valid) {
        char line[40];
        char date[16];
        localDate(gLevelProgress.unlockedAt ? gLevelProgress.unlockedAt
                                            : gLevelProgress.startedAt,
                  date, sizeof date);
        if (date[0])
            snprintf(line, sizeof line, "L%u since %s",
                     static_cast<unsigned>(gLevelProgress.level), date);
        else
            snprintf(line, sizeof line, "Level %u", static_cast<unsigned>(gLevelProgress.level));
        d.setTextColor(kFaint, kBg);
        d.drawString(line, 8, 117);
    }
    d.setTextColor(kFaint, kBg);
    const char* hint = "Enter: reviews  L: lessons  S: sync";
    if (attention) {
        hint = "O: outbox  Enter: reviews  S: sync";
        if (d.textWidth(hint) > 224) hint = "O: outbox  Enter  L  S";
        if (d.textWidth(hint) > 224) hint = "O: outbox  S: sync";
    } else if (d.textWidth(hint) > 224) {
        hint = "Enter  L  S";
    }
    d.drawString(hint, 8, 125);
    d.endWrite();
    present();
}

void drawPause() {
    if (!gDisp) return;
    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(kBrand, kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextColor(kDim, kBg);
    d.drawString("paused", 7, 22);

    char progress[24];
    const int total = static_cast<int>(gQueue.completedCount() + gQueue.remaining());
    snprintf(progress, sizeof progress, "%u/%u", static_cast<unsigned>(gQueue.completedCount()),
             static_cast<unsigned>(total));
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(kFaint, kBg);
    d.drawString(progress, 232, 5);
    d.setTextDatum(lgfx::top_left);

    static const char* kRows[] = {"Continue", "End session", "Settings"};
    for (int i = 0; i < 3; ++i) {
        const int y = 40 + i * 20;
        if (i == gPauseIndex) {
            d.fillRect(4, y - 2, 232, 16, kSurfaceHi);
            d.fillRect(4, y - 2, 2, 16, kAccent);
        }
        d.setTextFont(1);
        d.setTextSize(1);
        d.setTextColor(i == gPauseIndex ? kText : kDim, i == gPauseIndex ? kSurfaceHi : kBg);
        d.drawString(kRows[i], 12, y);
    }
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(kFaint, kBg);
    d.drawString("Enter: select  Esc: continue", 8, 125);
    d.endWrite();
    present();
}

void drawSettings() {
    if (!gDisp) return;
    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(kBrand, kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextColor(kDim, kBg);
    d.drawString("settings", 7, 22);
    d.setTextFont(0);
    d.setTextColor(kFaint, kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString("Esc: back", 232, 5);
    d.setTextDatum(lgfx::top_left);
    d.drawFastHLine(4, 36, 232, kLine);

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

    constexpr int kVisibleRows = 7;
    int top = gSettingsIndex - kVisibleRows / 2;
    if (top < 0) top = 0;
    if (top > kSettingsRows - kVisibleRows) top = kSettingsRows - kVisibleRows;
    for (int row = 0; row < kVisibleRows; ++row) {
        const int i = top + row;
        const int y = 42 + row * 11;
        if (i == gSettingsIndex) {
            d.fillRect(4, y - 2, 228, 11, kSurfaceHi);
            d.fillRect(4, y - 2, 2, 11, kAccent);
        }
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(i == gSettingsIndex ? kText : kDim,
                       i == gSettingsIndex ? kSurfaceHi : kBg);
        d.drawString(kLabels[i], 10, y);
        d.setTextDatum(lgfx::top_right);
        d.setTextColor(i == gSettingsIndex ? kAccentHi : kFaint,
                       i == gSettingsIndex ? kSurfaceHi : kBg);
        d.drawString(values[i].c_str(), 228, y);
        d.setTextDatum(lgfx::top_left);
    }
    // Scroll thumb when there are more rows than fit.
    constexpr int kTrackY = 42;
    constexpr int kTrackH = 66;
    d.fillRect(235, kTrackY, 2, kTrackH, kLine);
    const int thumbH = std::max(10, kTrackH * kVisibleRows / kSettingsRows);
    const int thumbY = kTrackY + (kTrackH - thumbH) * top / (kSettingsRows - kVisibleRows);
    d.fillRect(235, thumbY, 2, thumbH, kAccentHi);

    d.setTextFont(0);
    d.setTextColor(gSettingsNote[0] ? gSettingsNoteColor : kFaint, kBg);
    d.drawString(gSettingsNote[0] ? gSettingsNote : "Enter: change  Esc: back", 8, 123);
    d.endWrite();
    present();
}

void drawOutbox() {
    if (!gDisp) return;
    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    auto fitBack = [&d](std::string& text, int maxWidth) {
        while (text.size() > 1 && d.textWidth(text.c_str()) > maxWidth) {
            const char* begin = text.c_str();
            const char* last = begin;
            const char* p = begin;
            while (*p) {
                last = p;
                kk::utf8Next(&p);
            }
            if (last == begin) break;
            text.erase(static_cast<size_t>(last - begin));
        }
    };

    auto drawNote = [&d]() {
        if (!gOutboxNote[0]) return;
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(kErr, kBg);
        d.drawString(gOutboxNote, 8, 112);
    };

    const size_t total = kk::outboxAttentionCount(gOutbox);
    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(kBrand, kBg);
    d.drawString("OUTBOX", 6, 2);
    char count[32];
    snprintf(count, sizeof count, "%u need attention", static_cast<unsigned>(total));
    d.setTextFont(0);
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(kErr, kBg);
    d.drawString(count, 232, 5);
    d.setTextDatum(lgfx::top_left);

    const kk::OutboxItem* selected = selectedOutboxItem();
    if (!selected) {
        d.drawFastHLine(4, 22, 232, kLine);
        d.setTextFont(1);
        d.setTextColor(kDim, kBg);
        d.drawString("No reviews need attention", 8, 52);
        d.setTextFont(0);
        d.setTextColor(kFaint, kBg);
        d.drawString("Esc: back", 8, 124);
        d.endWrite();
        present();
        return;
    }

    if (gOutboxConfirm) {
        d.setTextFont(1);
        d.setTextColor(kErr, kBg);
        d.drawString("discard review", 7, 22);
        d.drawFastHLine(4, 36, 232, kLine);

        std::string label = outboxItemLabel(*selected);
        fitBack(label, 224);
        d.setTextFont(1);
        d.setTextColor(kText, kBg);
        d.drawString(label.c_str(), 8, 42);

        char reason[128];
        outboxReasonDetail(*selected, reason, sizeof reason);
        drawWrapped(d, reason, 8, 60, 224, 3, kDim, kBg);

        drawWrapped(d,
                    "Dismissing only removes the local unsent review and does not change "
                    "WaniKani.",
                    8, 88, 224, 3, kFaint, kBg);
        drawNote();

        d.setTextFont(0);
        d.setTextColor(kFaint, kBg);
        d.drawString("Enter: discard  Esc: cancel", 8, 124);
        d.endWrite();
        present();
        return;
    }

    d.drawFastHLine(4, 22, 232, kLine);

    const std::vector<const kk::OutboxItem*> items = attentionOutboxItems();
    int selectedIndex = 0;
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i]->seq == gOutboxSelectedSeq) selectedIndex = static_cast<int>(i);

    constexpr int kVisibleRows = 7;
    constexpr int kRowY = 28;
    constexpr int kRowH = 12;
    int top = selectedIndex - kVisibleRows / 2;
    if (top < 0) top = 0;
    const int maxTop = std::max(0, static_cast<int>(items.size()) - kVisibleRows);
    if (top > maxTop) top = maxTop;

    for (int row = 0; row < kVisibleRows; ++row) {
        const int index = top + row;
        if (index >= static_cast<int>(items.size())) break;
        const kk::OutboxItem& item = *items[static_cast<size_t>(index)];
        const bool focus = index == selectedIndex;
        const int y = kRowY + row * kRowH;
        if (focus) {
            d.fillRect(4, y - 2, 228, kRowH, kSurfaceHi);
            d.fillRect(4, y - 2, 2, kRowH, kAccent);
        }

        char reason[16];
        outboxReasonShort(item, reason, sizeof reason);
        const int reasonWidth = d.textWidth(reason);
        std::string label = outboxItemLabel(item);
        fitBack(label, 212 - reasonWidth);

        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(focus ? kText : kDim, focus ? kSurfaceHi : kBg);
        d.drawString(label.c_str(), 10, y);
        d.setTextDatum(lgfx::top_right);
        const uint16_t reasonColor = item.state == kk::OutboxState::Stuck
                                         ? kWarn
                                         : (item.httpCode ? kErr : kFaint);
        d.setTextColor(reasonColor, focus ? kSurfaceHi : kBg);
        d.drawString(reason, 228, y);
        d.setTextDatum(lgfx::top_left);
    }

    if (items.size() > kVisibleRows) {
        constexpr int kTrackY = kRowY;
        constexpr int kTrackH = kVisibleRows * kRowH - 4;
        d.fillRect(235, kTrackY, 2, kTrackH, kLine);
        const int thumbH =
            std::max(10, kTrackH * kVisibleRows / static_cast<int>(items.size()));
        const int thumbY = kTrackY + (kTrackH - thumbH) * top /
                                         (static_cast<int>(items.size()) - kVisibleRows);
        d.fillRect(235, thumbY, 2, thumbH, kAccentHi);
    }

    drawNote();
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(kFaint, kBg);
    d.drawString("Enter: review  D: discard  Esc: back", 8, 124);
    d.endWrite();
    present();
}

void drawLowBattery() {
    if (!gDisp) return;
    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(kBrand, kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextColor(kWarn, kBg);
    d.drawString("battery low", 7, 22);

    const int battery = tgc::board::batteryPercent();
    char pct[16];
    snprintf(pct, sizeof pct, "%d%%", battery >= 0 ? battery : 0);
    d.setTextFont(4);
    d.setTextSize(1);
    d.setTextColor(kWarn, kBg);
    d.setTextDatum(lgfx::middle_center);
    d.drawString(pct, 120, 62);
    d.setTextDatum(lgfx::top_left);

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(kText, kBg);
    d.drawString(gLowBatteryAction == 2 ? "A lesson batch may not finish." : "A review session may not finish.",
                 8, 86);
    d.setTextFont(0);
    d.setTextColor(kDim, kBg);
    d.drawString("Connect power or continue anyway.", 8, 102);
    d.setTextColor(kFaint, kBg);
    d.drawString("Enter: continue  Esc: cancel", 8, 125);
    d.endWrite();
    present();
}

void drawSignOutConfirm() {
    if (!gDisp) return;
    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(kBrand, kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextColor(kErr, kBg);
    d.drawString("sign out", 7, 22);
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(kText, kBg);
    d.drawString("Token, cache and", 8, 46);
    d.drawString("pending reviews will", 8, 60);
    d.drawString("be removed.", 8, 74);
    d.setTextFont(0);
    d.setTextColor(kDim, kBg);
    d.drawString("Wi-Fi stays saved.", 8, 92);
    d.setTextColor(kFaint, kBg);
    d.drawString("Enter: sign out", 8, 112);
    d.drawString("Esc: cancel", 8, 125);
    d.endWrite();
    present();
}

void drawAbout() {
    if (!gDisp) return;
    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(kBrand, kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextColor(kDim, kBg);
    d.drawString("about", 7, 22);
    d.setTextFont(0);
    d.setTextColor(kFaint, kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString("Esc: back", 232, 5);
    d.setTextDatum(lgfx::top_left);

    auto line = [&d](const String& text, int y, uint16_t fg) {
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(fg, kBg);
        String s = text;
        while (s.length() > 1 && d.textWidth(s.c_str()) > 226) s.remove(s.length() - 1);
        d.drawString(s.c_str(), 7, y);
    };

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(kText, kBg);
    d.drawString(String("KaniKey ") + kk::kFirmwareVersion, 7, 34);

    line(String("Build ") + __DATE__ + " " + __TIME__, 52, kFaint);
    line(String("Pack: ") + gPack.statusLine(), 63, gPack.ready() ? kOk : kWarn);
    char cache[64];
    snprintf(cache, sizeof cache, "Cache: %u batch, %u due", static_cast<unsigned>(gBatch.assignments.size()),
             static_cast<unsigned>(gDueReviews));
    line(cache, 74, kDim);
    line(String("Last sync: ") + gSyncClock + (gOffline ? " (offline)" : ""), 85,
         gOffline ? kWarn : kDim);
    String wifi = kk::DevWifi::hasSaved() ? kk::DevWifi::savedSsid() : String("not configured");
    if (kk::DevWifi::connected()) wifi += " " + kk::DevWifi::ip();
    line(String("Wi-Fi: ") + wifi, 96, kDim);
    line(String("Token: ") + (kk::DevToken::has() ? "configured" : "missing"), 107,
         kk::DevToken::has() ? kDim : kWarn);
    String zone = kk::settingsTimeZoneName();
    if (zone.isEmpty()) zone = kk::settingsTimeZoneRule();
    line(String("Zone: ") + zone, 118, kFaint);
    char heap[48];
    snprintf(heap, sizeof heap, "Heap: %u free / %u largest",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                    MALLOC_CAP_8BIT)));
    line(heap, 126, kFaint);
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

void drawBootLine(lgfx::LovyanGFX& d, const char* text, int x, int y, uint16_t fg) {
    if (!text || !text[0]) return;
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(fg, kBg);
    std::string s = text;
    while (s.size() > 1 && d.textWidth(s.c_str()) > 224) s.pop_back();
    d.drawString(s.c_str(), x, y);
}

void drawBoot() {
    if (!gDisp) return;
    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(kBrand, kBg);
    d.drawString("KaniKey", 6, 2);

    const int bat = tgc::board::batteryPercent();
    char right[24];
    if (bat >= 0)
        snprintf(right, sizeof right, "%d%%", bat);
    else
        snprintf(right, sizeof right, "USB");
    d.setTextFont(1);
    d.setTextColor(bat >= 0 && bat < 20 ? kWarn : kDim, kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString(right, 233, 5);
    d.setTextDatum(lgfx::top_left);

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

    const bool warn = st == BootState::NoSd || st == BootState::NoPack ||
                      st == BootState::AuthError || st == BootState::Forbidden ||
                      st == BootState::RateLimited || st == BootState::ServerError ||
                      st == BootState::Failed;
    d.setTextFont(4);
    d.setTextSize(1);
    d.setTextColor(warn ? kWarn : kText, kBg);
    d.drawString(title, 8, 38);
    drawBootLine(d, line1, 8, 82, kDim);
    drawBootLine(d, line2, 8, 96, kDim);
    if (action[0]) {
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(kFaint, kBg);
        d.drawString(action, 8, 120);
    }
    d.endWrite();
    present();
}

void drawSyncText(lgfx::LovyanGFX& d, const char* text, int x, int y, uint16_t fg,
                  int maxWidth) {
    if (!text || !text[0]) return;
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(fg, kBg);
    std::string s = text;
    while (s.size() > 1 && d.textWidth(s.c_str()) > maxWidth) s.pop_back();
    d.drawString(s.c_str(), x, y);
}

void drawSync() {
    if (!gDisp) return;
    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(kBrand, kBg);
    d.drawString("KaniKey", 6, 2);

    const int bat = tgc::board::batteryPercent();
    char right[16];
    if (bat >= 0)
        snprintf(right, sizeof right, "%d%%", bat);
    else
        snprintf(right, sizeof right, "USB");
    d.setTextFont(1);
    d.setTextColor(bat >= 0 && bat < 20 ? kWarn : kDim, kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString(right, 233, 5);
    d.setTextDatum(lgfx::top_left);

    d.setTextFont(2);
    d.setTextSize(1);
    const bool allDone = gSyncCompleted >= kSyncRowCount;
    d.setTextColor(gSyncFinished ? kWarn : allDone ? kOk : kText, kBg);
    d.drawString(gSyncFinished ? "Sync failed" : allDone ? "Synced" : "Syncing...", 8, 24);
    char count[12];
    snprintf(count, sizeof count, "%u/%u", static_cast<unsigned>(gSyncCompleted),
             static_cast<unsigned>(kSyncRowCount));
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(kFaint, kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString(count, 233, 32);
    d.setTextDatum(lgfx::top_left);

    for (int i = 0; i < kSyncRowCount; ++i) {
        const SyncRow& row = gSyncRows[i];
        const int y = 46 + i * 11;
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(kDim, kBg);
        d.drawString(row.label, 8, y);

        uint16_t valueColor = kFaint;
        const char* status = "--";
        uint16_t statusColor = kFaint;
        switch (row.state) {
            case SyncRowState::Pending: break;
            case SyncRowState::Active:
                valueColor = kText;
                status = "..";
                statusColor = kAccentHi;
                break;
            case SyncRowState::Done:
                valueColor = kText;
                status = "ok";
                statusColor = kOk;
                break;
            case SyncRowState::Warn:
                valueColor = kWarn;
                status = "!";
                statusColor = kWarn;
                break;
            case SyncRowState::Failed:
                valueColor = kErr;
                status = "X";
                statusColor = kErr;
                break;
        }
        if (row.value[0]) drawSyncText(d, row.value, 64, y, valueColor, 140);
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(statusColor, kBg);
        d.setTextDatum(lgfx::top_right);
        d.drawString(status, 233, y);
        d.setTextDatum(lgfx::top_left);
    }

    if (gSyncFinished && gSyncIssue != SyncIssue::None) {
        if (gSyncIssue == SyncIssue::RateLimit) {
            char rateLine[48];
            const uint32_t wait = kk::wkRateLimitWaitSeconds();
            if (wait)
                snprintf(rateLine, sizeof rateLine, "Rate limited; retry in %us",
                         static_cast<unsigned>(wait));
            else
                snprintf(rateLine, sizeof rateLine, "Rate limited; retry later");
            drawSyncText(d, rateLine, 8, 112, kWarn, 224);
        } else {
            drawSyncText(d, gNetLine, 8, 112, kWarn, 224);
        }
    } else {
        d.fillRect(8, 113, 224, 3, kSurfaceHi);
        if (gSyncCompleted)
            d.fillRect(8, 113, 224 * gSyncCompleted / kSyncRowCount, 3, kAccentHi);
    }

    const char* hint = "Esc: cancel";
    if (gSyncFinished)
        hint = (gSyncIssue == SyncIssue::Auth || gSyncIssue == SyncIssue::Forbidden)
                   ? "Enter: setup  Esc: back"
                   : "Enter: retry  Esc: back";
    else if (allDone)
        hint = "";
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(kFaint, kBg);
    d.drawString(hint, 8, 125);
    d.endWrite();
    present();
}

void drawSetupPortal() {
    if (!gDisp) return;
    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(kBrand, kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextColor(kDim, kBg);
    d.drawString(gSetupPortal.mode() == kk::SetupPortal::Mode::Ap ? "setup" : "setup · Wi-Fi",
                 7, 22);

    const int bat = tgc::board::batteryPercent();
    char right[16];
    if (bat >= 0)
        snprintf(right, sizeof right, "%d%%", bat);
    else
        snprintf(right, sizeof right, "USB");
    d.setTextFont(1);
    d.setTextColor(bat >= 0 && bat < 20 ? kWarn : kDim, kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString(right, 233, 5);
    d.setTextDatum(lgfx::top_left);

    auto line = [&d](const String& text, int y, uint16_t fg, uint8_t font = 1) {
        d.setTextFont(font);
        d.setTextSize(1);
        d.setTextColor(fg, kBg);
        String s = text;
        while (s.length() > 1 && d.textWidth(s.c_str()) > 228) s.remove(s.length() - 1);
        d.drawString(s.c_str(), 8, y);
    };

    if (gSetupPortal.mode() == kk::SetupPortal::Mode::Ap) {
        line("Join this Wi-Fi", 38, kDim);
        line(gSetupPortal.ssid(), 52, kText, 2);
        line("Password", 74, kDim);
        line(gSetupPortal.apPassword(), 86, kText);
        line("Open", 100, kDim);
        line(gSetupPortal.url(), 110, kAccentHi);
        line("Esc: cancel", 125, kFaint, 0);
    } else {
        line("Connected", 38, kOk);
        line(gSetupPortal.ssid(), 52, kText, 2);
        line("Open on this network", 74, kDim);
        line(gSetupPortal.url(), 84, kAccentHi);
        if (gSetupPortal.tokenValidated())
            line("Token OK: " + String(gSetupPortal.tokenUser()) + " L" +
                     String(gSetupPortal.tokenLevel()),
                 100, kOk);
        else if (kk::DevToken::has())
            line("Token stored: validate on the page", 100, kDim);
        else
            line("Token needed: enter it on the page", 100, kWarn);
        const String message = gSetupPortal.message();
        line(message.length() ? message : String("Pack: ") + gSetupPortal.packStatus(), 114,
             kFaint, 0);
        line("Finish on the web page  Esc: cancel", 125, kFaint, 0);
    }
    d.endWrite();
    present();
}

#ifdef KANIKEY_DEV
void drawDebug() {
    if (!gDisp) return;
    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(top_left);
    d.setTextWrap(false);
    d.setTextFont(2);
    d.setTextSize(2);
    d.setTextColor(kBrand, kBg);
    d.drawString("KaniKey", 6, 2);

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(kDim, kBg);
    d.drawString("debug build", 7, 38);

    const int bat = tgc::board::batteryPercent();
    char right[24];
    if (bat >= 0)
        snprintf(right, sizeof right, "%d%%", bat);
    else
        snprintf(right, sizeof right, "USB");
    d.setTextColor(bat >= 0 && bat < 20 ? kWarn : kDim, kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString(right, 233, 7);
    d.setTextDatum(lgfx::top_left);

    d.setTextColor(kText, kBg);
    d.setTextFont(1);
    d.drawString("M0 hardware check", 7, 54);
    d.setTextColor(kFaint, kBg);
    d.drawString(gNetLine, 7, 66);

    d.fillRect(4, 78, 232, 34, kSurface);
    d.drawRect(4, 78, 232, 34, kLine);
    d.setTextFont(1);
    for (int i = 0; i < 3; ++i) {
        d.setTextColor(i == 2 ? kText : kFaint, kSurface);
        d.drawString(gLog[i].text, 9, 81 + i * 10);
    }

    d.setTextFont(0);
    d.setTextColor(gSdOk ? kOk : kWarn, kBg);
    d.drawString(gSdLine, 5, 117);
    char heap[40];
    snprintf(heap, sizeof heap, "heap %u  largest %u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                     MALLOC_CAP_8BIT)));
    d.setTextColor(kFaint, kBg);
    d.drawString(heap, 5, 126);

    char mods[24] = "";
    if (gShift) strncat(mods, "S", sizeof mods - strlen(mods) - 1);
    if (gFn) strncat(mods, "F", sizeof mods - strlen(mods) - 1);
    if (gCtrl) strncat(mods, "C", sizeof mods - strlen(mods) - 1);
    if (gOpt) strncat(mods, "O", sizeof mods - strlen(mods) - 1);
    if (gAlt) strncat(mods, "A", sizeof mods - strlen(mods) - 1);
    if (mods[0]) {
        d.setTextColor(kDim, kBg);
        d.setTextDatum(lgfx::top_right);
        d.drawString(mods, 233, 126);
        d.setTextDatum(lgfx::top_left);
    }
    d.endWrite();
    present();
}

void drawRadicalTest() {
    if (!gDisp) return;
    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(kBrand, kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextColor(kDim, kBg);
    d.drawString("radical test", 7, 22);

    d.fillRect(4, 38, 232, 66, kSurface);
    d.drawRect(4, 38, 232, 66, kLine);
    const bool ok = gPack.drawRadical(d, gRadicalTestId, 120, 71, 96, 62, kText, kSurface,
                                      kk::ClipRect{6, 40, 234, 102});
    if (!ok) drawMissingGlyph(d, 120, 71);

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ok ? kOk : kWarn, kBg);
    char line[64];
    snprintf(line, sizeof line, "subject %u  %s", static_cast<unsigned>(gRadicalTestId),
             ok ? "bitmap" : "missing");
    d.drawString(line, 8, 110);
    d.setTextFont(0);
    d.setTextColor(kFaint, kBg);
    d.drawString("R <id>: load  Esc: back", 8, 124);
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
        drawRadicalTest();
        return;
    }
#endif
    if (gReviewDetail) {
        drawReviewDetail();
        return;
    }
    if (gReviewActive) {
        drawReview();
        return;
    }
    if (gReviewSummary) {
        drawReviewSummary();
        return;
    }
    if (gLessonEmptyActive) {
        drawLessonEmpty();
        return;
    }
    if (gLessonListActive) {
        drawLessonList();
        return;
    }
    if (gLessonQuizActive) {
        drawLessonQuiz();
        return;
    }
    if (gLessonActive) {
        drawLesson();
        return;
    }
#ifdef KANIKEY_DEV
    if (gShowDebug) {
        drawDebug();
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
    if (gShowDebug) drawDebug();
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
            if (kk::DevToken::save(token)) {
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

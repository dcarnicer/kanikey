// Session cache, the lazy review batch and the SD mount.
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
#include <new>

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

void removeSubjectFromReviews(kk::Summary& summary, uint32_t subjectId);
// Completed-but-unsent reviews are still in the outbox; they must not come
// back as due after a reboot.
void pruneCacheOutbox();
// A fresh summary is authoritative: drop cached assignments that are no
// longer due or that were reviewed in the meantime.
void saveSubjectCache();
uint32_t cacheAgeSeconds();

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
    if (state().outbox.gOutbox.empty()) return;
    for (size_t i = 0; i < state().cache.gBatch.assignments.size();) {
        if (outboxHasAssignment(state().cache.gBatch.assignments[i].id)) {
            removeSubjectFromReviews(state().session.gSummary, state().cache.gBatch.assignments[i].subjectId);
            state().cache.gBatch.assignments.erase(state().cache.gBatch.assignments.begin() + i);
        } else {
            ++i;
        }
    }
    for (const kk::OutboxItem& it : state().outbox.gOutbox)
        if (it.subjectId) removeSubjectFromReviews(state().session.gSummary, it.subjectId);
    state().cache.gBatch.ok = !state().cache.gBatch.assignments.empty() && !state().cache.gBatch.subjects.empty();
}

// A fresh summary is authoritative: drop cached assignments that are no
// longer due or that were reviewed in the meantime.
void saveSubjectCache() {
    if (!state().storage.gFsOk) return;  // no storage: keep everything in memory only
    if (!state().storage.gSdOk || !state().cache.gBatch.ok || state().cache.gBatch.subjects.empty() || !kk::DevToken::has()) return;
    kk::CacheMeta meta;
    meta.savedAt = state().cache.gBatch.fetchedAt ? state().cache.gBatch.fetchedAt : state().session.gSyncedAt;
    meta.accountKey = currentAccountKey();
    // cacheSaveSubjects() logs the count, size and time of a good save.
    if (!kk::cacheSaveSubjects(SD, meta, state().cache.gBatch.subjects))
        Serial.println("[cache] subjects save failed");
}

uint32_t cacheAgeSeconds() {
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    if (state().cache.gCacheSavedAt == 0 || now < 1700000000u || now < state().cache.gCacheSavedAt) return 0;
    return now - state().cache.gCacheSavedAt;
}

}  // namespace

void pruneBatchToSummary(uint32_t now) {
    if (state().cache.gBatch.assignments.empty()) return;
    const std::vector<uint32_t> due = state().session.gSummary.dueSubjectIds(now, 1000);
    for (size_t i = 0; i < state().cache.gBatch.assignments.size();) {
        const kk::Assignment& a = state().cache.gBatch.assignments[i];
        const bool stillDue = std::find(due.begin(), due.end(), a.subjectId) != due.end();
        if (!stillDue || outboxHasAssignment(a.id) || (a.availableAt && a.availableAt > now))
            state().cache.gBatch.assignments.erase(state().cache.gBatch.assignments.begin() + i);
        else
            ++i;
    }
    state().cache.gBatch.ok = !state().cache.gBatch.assignments.empty() && !state().cache.gBatch.subjects.empty();
}

void clearLevelUpNotice() {
    state().session.gLevelUpNotice = false;
    state().session.gLevelUpFrom = 0;
    state().session.gLevelUpTo = 0;
    state().session.gLevelUpAt = 0;
}

uint32_t currentAccountKey() {
    const String token = kk::DevToken::load();
    return kk::cacheAccountKey(std::string_view(token.c_str(), token.length()));
}

// Without SNTP there is no battery-backed clock. Use the last cache save as
// the reference and advance it with uptime so a trusted-clock cache can cross
// a due time while Home stays open (SPEED-07). gBootMs is anchored when the
// cache is loaded during setup(); a later sync that writes a newer savedAt
// re-anchors it so the elapsed interval is not counted twice.
uint32_t gBootMs = 0;
uint32_t gOfflineBaseSec = 0;
bool gOfflineClockReady = false;

uint32_t offlineBase() {
    return state().cache.gCacheSavedAt ? state().cache.gCacheSavedAt
                                       : state().cache.gBatch.fetchedAt;
}

void anchorOfflineClock() {
    const uint32_t base = offlineBase();
    if (gOfflineClockReady && base == gOfflineBaseSec) return;
    gOfflineBaseSec = base;
    gBootMs = millis();
    gOfflineClockReady = base != 0;
    if (gOfflineClockReady)
        Serial.printf("[time] offline clock %u + uptime\n",
                      static_cast<unsigned>(gOfflineBaseSec));
}

uint32_t effectiveNow() {
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    if (now >= 1700000000u) return now;
    const uint32_t base = offlineBase();
    if (!base) {
        gOfflineClockReady = false;
        return now;
    }
    anchorOfflineClock();
    return gOfflineBaseSec + (millis() - gBootMs) / 1000u;
}

void applySummary(uint32_t now) {
    state().session.gDueReviews = state().session.gSummary.dueReviews(now);
    state().session.gLessons = state().session.gSummary.totalLessons();
    char hhmm[8] = "--:--";
    const uint32_t nextAt = state().session.gSummary.nextReviewAt(now);
    if (nextAt) localHm(nextAt, hhmm, sizeof hhmm);
    kk::formatNextReview(state().session.gSummary, now, hhmm, state().session.gNextReview, sizeof state().session.gNextReview);
}

void saveCacheSnapshot() {
    if (!state().storage.gFsOk) return;  // no storage: keep everything in memory only
    if (!state().session.gHaveSummary || !kk::DevToken::has()) return;
    kk::CacheSnapshot snapshot;
    snapshot.meta.savedAt = state().session.gSyncedAt;
    snapshot.meta.accountKey = currentAccountKey();
    snapshot.summary = state().session.gSummary;
    if (state().session.gHaveUser) snapshot.user = state().session.gUser;
    snapshot.levelProgress = state().session.gLevelProgress;
    snapshot.srs = state().session.gSrs;
    snapshot.lastFullAt = state().cache.gLastFullAt;
    snapshot.lastQuickAt = state().cache.gLastQuickAt;
    snapshot.lastUserAt = state().cache.gLastUserAt;
    if (state().cache.gBatch.ok) {
        snapshot.assignments = state().cache.gBatch.assignments;
    } else if (state().cache.gBatchEvicted) {
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
    state().cache.gCacheSavedAt = snapshot.meta.savedAt;
    state().cache.gCachedBatchCount = snapshot.assignments.size();
    Serial.printf("[cache] snapshot saved: %u assignments, %u due\n",
                  static_cast<unsigned>(snapshot.assignments.size()),
                  static_cast<unsigned>(state().session.gDueReviews));
}

void saveCacheAll() {
    saveCacheSnapshot();
    saveSubjectCache();
}

bool loadCache() {
    if (!state().storage.gFsOk || !state().storage.gSdOk || !kk::DevToken::has()) return false;
    kk::CacheSnapshot snapshot;
    if (!kk::cacheLoadSnapshot(&snapshot)) return false;
    const uint32_t key = currentAccountKey();
    if (snapshot.meta.accountKey != key) {
        Serial.println("[cache] token/account changed; discarding cache");
        kk::cacheClearSnapshot();
        kk::cacheClearSubjects(SD);
        return false;
    }
    state().session.gSummary = std::move(snapshot.summary);
    state().session.gHaveSummary = true;
    state().session.gHaveUser = !snapshot.user.username.empty() && snapshot.user.level > 0;
    if (state().session.gHaveUser) state().session.gUser = std::move(snapshot.user);
    state().session.gLevelProgress = snapshot.levelProgress;
    state().session.gSrs = snapshot.srs;
    state().cache.gLastFullAt = snapshot.lastFullAt;
    state().cache.gLastQuickAt = snapshot.lastQuickAt;
    state().cache.gLastUserAt = snapshot.lastUserAt;

    // The assignment list is cheap and only a few hundred bytes; the subjects
    // are not. Keep the count for About and let startReview() load the batch
    // from disk when it is actually needed.
    state().cache.gBatch = kk::ReviewBatch{};
    state().cache.gCachedBatchCount = snapshot.assignments.size();
    state().cache.gBatchEvicted = state().cache.gCachedBatchCount > 0;

    state().cache.gCacheSavedAt = snapshot.meta.savedAt;
    state().session.gSyncedAt = snapshot.meta.savedAt;
    if (state().session.gSyncedAt) localHm(state().session.gSyncedAt, state().session.gSyncClock, sizeof state().session.gSyncClock);
    anchorOfflineClock();
    pruneCacheOutbox();
    applySummary(effectiveNow());
    state().cache.gOffline = true;
    // Boot and retry start at Boot; the dev seed command may call this from an
    // active screen, which must stay where it is (the old Home flag only
    // affected the fallback behind other screens).
    if (kk::screenBase(state().screen) == kk::Screen::Boot)
        state().screen.screen = kk::Screen::Home;
    Serial.printf("[cache] loaded: %u cached assignments (batch lazy), %u due, saved %u\n",
                  static_cast<unsigned>(state().cache.gCachedBatchCount),
                  static_cast<unsigned>(state().session.gDueReviews), static_cast<unsigned>(state().cache.gCacheSavedAt));
    Serial.printf("[cache] last quick %u, full %u, user %u\n",
                  static_cast<unsigned>(state().cache.gLastQuickAt),
                  static_cast<unsigned>(state().cache.gLastFullAt),
                  static_cast<unsigned>(state().cache.gLastUserAt));
    if (state().session.gSrs.valid)
        Serial.printf("[cache] srs A %u, G %u, M %u, E %u, B %u\n",
                      static_cast<unsigned>(state().session.gSrs.apprentice), static_cast<unsigned>(state().session.gSrs.guru),
                      static_cast<unsigned>(state().session.gSrs.master),
                      static_cast<unsigned>(state().session.gSrs.enlightened),
                      static_cast<unsigned>(state().session.gSrs.burned));
    return true;
}

// Loads the cached review batch (assignments + subjects) on demand. The
// snapshot is authoritative for the assignment list; the subject cache on SD
// supplies the data used by the review screens.
bool loadCachedBatch() {
    if (!state().storage.gFsOk || !state().storage.gSdOk || !kk::DevToken::has()) return false;
    try {
        kk::CacheSnapshot snapshot;
        if (!kk::cacheLoadSnapshot(&snapshot) || snapshot.assignments.empty()) return false;
        const uint32_t key = currentAccountKey();
        if (snapshot.meta.accountKey != key) return false;

        kk::SubjectCache subjects;
        if (!kk::cacheLoadSubjects(SD, &subjects) || subjects.meta.accountKey != key) return false;

        // Move the subjects instead of copying them: a 52-subject cache has many
        // small strings, and the copy doubled the heap peak enough to abort
        // after joining Wi-Fi. The used flags keep component subjects for the
        // offline detail card.
        kk::ReviewBatch batch;
        batch.assignments.reserve(snapshot.assignments.size());
        batch.subjects.reserve(subjects.subjects.size());
        std::vector<bool> used(subjects.subjects.size(), false);
        for (const kk::Assignment& a : snapshot.assignments) {
            size_t match = subjects.subjects.size();
            for (size_t i = 0; i < subjects.subjects.size(); ++i)
                if (subjects.subjects[i].id == a.subjectId) {
                    match = i;
                    break;
                }
            if (match == subjects.subjects.size()) continue;
            batch.assignments.push_back(a);
            if (used[match]) {
                // Duplicate subject ids are not expected, but keep a valid row
                // rather than moving the same object twice.
                batch.subjects.push_back(subjects.subjects[match]);
            } else {
                batch.subjects.push_back(std::move(subjects.subjects[match]));
                used[match] = true;
            }
        }
        for (size_t i = 0; i < subjects.subjects.size(); ++i) {
            if (used[i]) continue;
            batch.subjects.push_back(std::move(subjects.subjects[i]));
        }
        if (batch.assignments.empty()) return false;
        batch.fetchedAt = snapshot.meta.savedAt;
        batch.ok = true;
        state().cache.gBatch = std::move(batch);
        state().cache.gBatchEvicted = false;
        state().cache.gCachedBatchCount = state().cache.gBatch.assignments.size();
        pruneCacheOutbox();
        pruneBatchToSummary(effectiveNow());
        state().cache.gCachedBatchCount = state().cache.gBatch.assignments.size();
        state().cache.gBatch.ok = !state().cache.gBatch.assignments.empty() && !state().cache.gBatch.subjects.empty();
        Serial.printf("[cache] batch loaded: %u assignments, %u subjects\n",
                      static_cast<unsigned>(state().cache.gBatch.assignments.size()),
                      static_cast<unsigned>(state().cache.gBatch.subjects.size()));
        return state().cache.gBatch.ok;
    } catch (const std::bad_alloc&) {
        Serial.println("[cache] batch load out of memory");
        state().cache.gBatch = kk::ReviewBatch{};
        state().cache.gBatchEvicted = false;
        return false;
    }
}

// Drops the batch's subject strings when the app leaves review mode or starts
// a lesson fetch. The assignment list and subjects stay on disk, so the next
// session reloads them without a network round trip.
void evictCachedBatch(bool save) {
    if (state().cache.gBatch.assignments.empty() && state().cache.gBatch.subjects.empty() && !state().cache.gBatchEvicted) return;
    if (save) saveCacheSnapshot();
    if (!state().cache.gBatch.assignments.empty()) state().cache.gCachedBatchCount = state().cache.gBatch.assignments.size();
    state().cache.gBatch = kk::ReviewBatch{};
    state().cache.gBatchEvicted = state().cache.gCachedBatchCount > 0;
    Serial.printf("[cache] batch evicted, %u assignments remain on disk\n",
                  static_cast<unsigned>(state().cache.gCachedBatchCount));
}

void markCacheCompleted(uint32_t subjectId, uint32_t assignmentId, bool save) {
    if (!state().session.gHaveSummary) return;
    removeSubjectFromReviews(state().session.gSummary, subjectId);
    for (size_t i = 0; i < state().cache.gBatch.assignments.size();) {
        if (state().cache.gBatch.assignments[i].id == assignmentId)
            state().cache.gBatch.assignments.erase(state().cache.gBatch.assignments.begin() + i);
        else
            ++i;
    }
    state().cache.gBatch.ok = !state().cache.gBatch.assignments.empty() && !state().cache.gBatch.subjects.empty();
    state().cache.gCachedBatchCount = state().cache.gBatch.assignments.size();
    if (save) saveCacheSnapshot();
}

bool mountSd() {
    tgc::board::SdPins pins;
    if (!tgc::board::sdPins(&pins)) {
        snprintf(state().storage.gSdLine, sizeof state().storage.gSdLine, "SD: no slot");
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
        snprintf(state().storage.gSdLine, sizeof state().storage.gSdLine, "SD: no card / not FAT32");
        Serial.printf("[sd] mount failed in %u ms, heap %u -> %u\n",
                      static_cast<unsigned>(millis() - t0),
                      static_cast<unsigned>(heapBefore),
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
        return false;
    }
    const uint64_t mb = SD.cardSize() / (1024ull * 1024ull);
    snprintf(state().storage.gSdLine, sizeof state().storage.gSdLine, "SD: %llu MB", static_cast<unsigned long long>(mb));
    Serial.printf("[sd] mounted in %u ms, %llu MB, heap %u -> %u, largest %u\n",
                  static_cast<unsigned>(millis() - t0), static_cast<unsigned long long>(mb),
                  static_cast<unsigned>(heapBefore),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                         MALLOC_CAP_8BIT)));
    return true;
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

}  // namespace kk::app

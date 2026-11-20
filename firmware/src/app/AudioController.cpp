#include "app/AudioController.h"
#include "app/AppUi.h"

#include <SD.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <string>

#include "app/AppState.h"
#include "app/CacheController.h"
#include "platform/AudioCache.h"
#include "platform/AudioPlayer.h"
#include "platform/Sound.h"
#include "platform/WkClient.h"
#include "platform/DeviceSettings.h"
#include "platform/DevToken.h"
#include "wk/AudioQueue.h"

namespace kk::app {

namespace {

// True once the cache was prepared for the current visit to a review or lesson
// card screen. Cleared on leaving, so the next visit prepares again once.
bool gPreparedForVisit = false;

// AUD-02: the clips waiting to download. RAM only; a restart or a new sync
// refills it from the batch. Entries carry copies of the URLs, so the batch can
// be evicted from RAM meanwhile.
kk::AudioQueue gQueue;
bool gDrainAttempted = false;
uint32_t gDrainLastAttemptMs = 0;
bool gDrainRefused = false;  // the last attempt was refused for heap

// M6: one pending autoplay. Only the id and type are kept; the clip is looked
// up when it starts, so a stale subject pointer is never used.
struct AutoplayPending {
    bool active = false;
    uint32_t subjectId = 0;
    SubjectType type = SubjectType::Vocabulary;
};
AutoplayPending gAutoplay;

// M6: the lesson cards already asked for this visit, so a card autoplays once
// however often the user pages back to it. A lesson batch is at most 10 cards;
// past 16 the oldest entry is overwritten (only then could a card repeat).
constexpr size_t kLessonAskedMax = 16;
uint32_t gLessonAsked[kLessonAskedMax] = {};  // 0 = empty slot
size_t gLessonAskedNext = 0;

bool lessonAlreadyAsked(uint32_t id) {
    for (uint32_t asked : gLessonAsked)
        if (asked == id) return true;
    return false;
}

void forgetLessonsAsked() {
    for (uint32_t& asked : gLessonAsked) asked = 0;
    gLessonAskedNext = 0;
}

// A subject carrying only the id and type. audioPlayerPlay() and the hint
// read nothing else, and an empty Subject allocates nothing.
kk::Subject lightSubject(uint32_t id, SubjectType type) {
    kk::Subject s;
    s.id = id;
    s.type = type;
    return s;
}

// AUD-02 queue budgets. The TLS handshake of one clip gets at most 8 s; a
// lesson batch may drain inline within 8 s in total (its five clips took 3.8 s
// on hardware); the Home tick downloads one clip per call.
constexpr uint32_t kDrainHandshakeSec = 8;
constexpr uint32_t kInlineDrainBudgetMs = 8000;

enum class DrainResult : uint8_t { NotReady, Refused, Stored, Failed };

bool audioScreen() {
    const Screen base = kk::screenBase(state().screen);
    return base == Screen::Review || base == Screen::LessonCard;
}

// The Home tick may download only on an idle Home: no modal, not dimmed, no
// portal, and nothing playing (a download would stall the decoder).
bool drainScreenIdle() {
    if (kk::screenBase(state().screen) != Screen::Home) return false;
    if (kk::screenTop(state().screen) != kk::Modal::None) return false;
    if (state().ui.gDimmed) return false;
    if (state().setup.gSetupPortal.active()) return false;
    if (kk::audioPlayerIsPlaying() || kk::sound::soundBusy()) return false;
    return true;
}

// Downloads the head of the queue. The canvas is released only around the
// download, as the old batch hook did, and restored afterwards.
DrainResult drainOne(bool screenIdle, uint32_t spacingMs, uint32_t handshakeSec) {
    if (gQueue.empty()) return DrainResult::NotReady;
    // Every call past the spacing counts as an attempt, even one that finds a
    // rule false, so the token is read and the link checked at most once per
    // spacing window, not on every loop.
    const uint32_t sinceAttempt =
        gDrainAttempted ? millis() - gDrainLastAttemptMs : kk::kAudioDrainNever;
    if (sinceAttempt < spacingMs) return DrainResult::NotReady;
    gDrainAttempted = true;
    gDrainLastAttemptMs = millis();

    gQueue.useAccount(currentAccountKey());
    kk::AudioDrainState s;
    s.queued = !gQueue.empty();
    s.screenIdle = screenIdle;
    s.sessionActive = kk::wkSessionActive();
    s.syncRunning = state().sync.gSyncing || state().sync.gSyncJob.active;
    s.wifiUp = WiFi.status() == WL_CONNECTED;
    s.sdOk = state().storage.gSdOk;
    s.tokenUsable = kk::DevToken::has() && !kk::wkAuthBlocked() && kk::wkRateLimitWaitSeconds() == 0;
    s.msSinceAttempt = sinceAttempt;
    s.spacingMs = spacingMs;
    if (!kk::audioDrainAllowed(s)) return DrainResult::NotReady;

    const bool savedFree = state().ui.gFreeCanvasForRequest;
    state().ui.gFreeCanvasForRequest = true;
    requestBefore();
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t total = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    // Home does not need the review batch in RAM (a review reloads it from the
    // card), and a fresh 20-item batch kept the largest block at ~41 KB on
    // hardware. Put it on disk before giving up on the heap.
    if (!kk::audioDrainHeapOk(largest, total) && screenIdle &&
        (!state().cache.gBatch.subjects.empty() || !state().cache.gBatch.assignments.empty())) {
        evictCachedBatch(true);
        largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        total = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    DrainResult result = DrainResult::Refused;
    if (!kk::audioDrainHeapOk(largest, total)) {
        gDrainRefused = true;
        Serial.printf("[audio] drain waits for heap: largest %u, free %u\n",
                      static_cast<unsigned>(largest), static_cast<unsigned>(total));
    } else if (!kk::audioCacheOpen(SD, gQueue.accountKey())) {
        Serial.println("[audio] drain skipped: cache unavailable");
        result = DrainResult::NotReady;
    } else {
        const kk::AudioQueueItem& item = gQueue.front();
        const uint32_t subjectId = item.subjectId;
        const uint8_t voice = item.voice;
        const uint32_t t0 = millis();
        std::string error;
        const uint32_t bytes =
            kk::audioCacheFetchClip(SD, subjectId, voice, item.url, handshakeSec, &error);
        const uint32_t ms = millis() - t0;
        gDrainRefused = false;
        if (bytes) {
            Serial.printf("[audio] clip %u ok %u B %u ms\n", static_cast<unsigned>(subjectId),
                          static_cast<unsigned>(bytes), static_cast<unsigned>(ms));
            gQueue.popFront();
            result = DrainResult::Stored;
        } else {
            Serial.printf("[audio] clip %u failed: %s\n", static_cast<unsigned>(subjectId),
                          error.c_str());
            if (gQueue.failFront())
                Serial.printf("[audio] clip %u given up after %u failures\n",
                              static_cast<unsigned>(subjectId),
                              static_cast<unsigned>(kk::kAudioQueueMaxFailures));
            result = DrainResult::Failed;
        }
        if (gQueue.empty()) Serial.println("[audio] queue empty");
    }
    requestAfter();
    state().ui.gFreeCanvasForRequest = savedFree;
    state().display.gDirty = true;
    return result;
}

// Home tick: at most one clip per call, with the spacing after a clip, a
// failure, or (longer) a heap refusal.
void drainTick() {
    if (gQueue.empty()) return;
    if (!drainScreenIdle()) return;
    const uint32_t spacing = gDrainRefused ? kk::kAudioDrainRefusedSpacingMs : kk::kAudioDrainSpacingMs;
    drainOne(/*screenIdle=*/true, spacing, kDrainHandshakeSec);
}

// Lesson batch hook: drains the batch's clips in this call, within the budget,
// under the same link and heap rules. It stops at the first refusal, and the
// rest waits for the Home tick.
void drainInline() {
    const uint32_t start = millis();
    while (!gQueue.empty()) {
        const uint32_t elapsed = millis() - start;
        if (elapsed >= kInlineDrainBudgetMs) break;
        const uint32_t left = std::max<uint32_t>(2, (kInlineDrainBudgetMs - elapsed) / 1000);
        const DrainResult result = drainOne(/*screenIdle=*/true, /*spacingMs=*/0, left);
        if (result != DrainResult::Stored && result != DrainResult::Failed) break;
    }
}

// Starts the pending autoplay when every rule still holds. It re-checks the
// setting, the screen, the cache, sync and the portal at start time, so a
// change since the request cancels it. It waits while the answer cue plays
// (it never delays a key), and it tries once: a refused start is not retried.
void autoplayTick() {
    if (!gAutoplay.active) return;
    if (!audioScreen()) {
        gAutoplay.active = false;
        return;
    }
    int on = 0;
    kk::settingsGetInt("rm.autoplay", &on);
    const kk::Subject subject = lightSubject(gAutoplay.subjectId, gAutoplay.type);
    const bool wanted = kk::audioAutoplayWanted(
        on == 1, /*onReadingPage=*/true, subject.type,
        audioHintFor(subject) == kk::AudioHint::Available, state().sync.gSyncing,
        state().setup.gSetupPortal.active());
    if (!wanted) {
        gAutoplay.active = false;
        return;
    }
    if (kk::sound::soundBusy()) return;
    gAutoplay.active = false;
    kk::audioPlayerPlay(subject, audioVoice());
    state().display.gDirty = true;
}

}  // namespace

uint8_t audioVoice() {
    int voice = 0;
    if (kk::settingsGetInt("rm.voice", &voice) && voice == 1) return 1;
    return 0;
}

void audioBegin() { kk::audioPlayerBegin(SD); }

void audioAutoplayRequest(const kk::Subject& subject) {
    int on = 0;
    if (!kk::settingsGetInt("rm.autoplay", &on) || on != 1) return;  // Off: nothing changes
    gAutoplay.active = true;
    gAutoplay.subjectId = subject.id;
    gAutoplay.type = subject.type;
}

void audioLessonCardShown(const kk::Subject& subject, bool hasReading) {
    if (!hasReading || subject.id == 0 || lessonAlreadyAsked(subject.id)) return;
    gLessonAsked[gLessonAskedNext] = subject.id;
    gLessonAskedNext = (gLessonAskedNext + 1) % kLessonAskedMax;
    audioAutoplayRequest(subject);
}

void audioAutoplayCancel() { gAutoplay.active = false; }

void audioTick() {
    // M6: the asked-cards set lasts through a lesson's cards and their quiz, and
    // ends when the device leaves for any other screen.
    const Screen base = kk::screenBase(state().screen);
    if (base != Screen::LessonCard && base != Screen::LessonQuiz) forgetLessonsAsked();
    if (!audioScreen()) {
        gPreparedForVisit = false;
        audioAutoplayCancel();
        // AUD-03: no clip outlives its screen.
        if (kk::audioPlayerIsPlaying()) audioStop();
    } else if (!gPreparedForVisit) {
        gPreparedForVisit = true;
        // SD I/O on entry, not in a draw. A failure leaves the hint at `no audio`
        // for this visit; it does not retry.
        if (state().storage.gSdOk && kk::DevToken::has())
            kk::audioPlayerPrepare(currentAccountKey());
        state().display.gDirty = true;
    }
    autoplayTick();
    kk::audioPlayerTick();
    drainTick();
}

void audioStop() {
    audioAutoplayCancel();
    kk::audioPlayerStop();
}

kk::AudioHint audioHintFor(const kk::Subject& subject) {
    if (!kk::audioSubjectPlayable(subject.type)) return kk::AudioHint::None;
    if (!state().storage.gSdOk) return kk::AudioHint::Missing;
    return kk::audioPlayerHint(subject, audioVoice());
}

bool audioPlay(const kk::Subject& subject) {
    if (!kk::audioSubjectPlayable(subject.type)) return false;
    if (!state().storage.gSdOk) return false;
    if (!kk::audioPlayerReady()) {
        // The screen entry normally prepared it; prepare here if it did not.
        if (!kk::DevToken::has()) return false;
        kk::audioPlayerPrepare(currentAccountKey());
        if (!kk::audioPlayerReady()) return false;
    }
    const bool started = kk::audioPlayerPlay(subject, audioVoice());
    state().display.gDirty = true;
    return started;
}

void audioPrefetchBatch(const std::vector<kk::Subject>& subjects, bool drainNow) {
    if (!state().storage.gSdOk || !kk::DevToken::has()) return;
    const uint32_t key = currentAccountKey();
    if (!kk::audioCacheOpen(SD, key)) return;
    gQueue.useAccount(key);
    // The batch only queues: a review batch arrives right after a large fetch,
    // when the TLS heap is short, so it never downloads here (hardware
    // 2026-11-20: 0/17 clips). The Home tick drains the queue later.
    size_t queued = 0;
    size_t cached = 0;
    size_t full = 0;
    for (const kk::Subject& subject : subjects) {
        if (subject.type != SubjectType::Vocabulary) continue;
        size_t index = 0;
        if (!kk::audioPickDownload(subject.audios, audioVoice(), &index)) continue;
        const kk::AudioClip& clip = subject.audios[index];
        if (kk::audioCacheHasMp3(SD, subject.id, clip.voice)) {
            ++cached;
            continue;
        }
        switch (gQueue.enqueue(subject.id, clip.voice, clip.url, clip.contentType)) {
            case kk::AudioQueue::Add::Added: ++queued; break;
            case kk::AudioQueue::Add::Duplicate: break;
            case kk::AudioQueue::Add::Full: ++full; break;
        }
    }
    Serial.printf("[audio] queued %u clips (%u already cached)\n", static_cast<unsigned>(queued),
                  static_cast<unsigned>(cached));
    if (full)
        Serial.printf("[audio] queue full: %u clips not queued\n", static_cast<unsigned>(full));
    // The lesson hook asks for the inline drain: a lesson batch is small and
    // the lesson cards need their clips at once. The same heap and link rules
    // apply, so a short heap leaves the clips for the Home tick.
    if (drainNow) drainInline();
    state().display.gDirty = true;
}

}  // namespace kk::app

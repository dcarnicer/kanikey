#include "app/AudioController.h"
#include "app/AppUi.h"

#include <SD.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "app/AppState.h"
#include "app/CacheController.h"
#include "platform/AudioCache.h"
#include "platform/AudioPlayer.h"
#include "platform/Sound.h"
#include "platform/WkClient.h"
#include "platform/DeviceSettings.h"
#include "platform/DevToken.h"

namespace kk::app {

namespace {

// True once the cache was prepared for the current visit to a review or lesson
// card screen. Cleared on leaving, so the next visit prepares again once.
bool gPreparedForVisit = false;

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

// Batch prefetch budget (AUD-02: 8 s total per batch, the batch stops early
// when it is spent). The TLS need is the 32 KB handshake buffer: below that
// largest free block a clip is not started.
constexpr uint32_t kPrefetchBudgetMs = 8000;
constexpr size_t kTlsNeedBytes = 32 * 1024;

bool audioScreen() {
    const Screen base = kk::screenBase(state().screen);
    return base == Screen::Review || base == Screen::LessonCard;
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

namespace {

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

void audioPrefetchBatch(const std::vector<kk::Subject>& subjects) {
    if (!state().storage.gSdOk || !kk::DevToken::has()) return;
    if (kk::wkAuthBlocked() || kk::wkRateLimitWaitSeconds() > 0) {
        Serial.println("[audio] prefetch skipped: token blocked or rate limited");
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[audio] prefetch skipped: offline");
        return;
    }
    // The clip downloads are TLS requests outside WkClient, so they do not get
    // its request hooks: release the canvas the same way for the whole batch
    // (on the Cardputer it holds the block TLS needs; measured 27,636 B largest
    // with it, after a lesson fetch), and bring it back afterwards.
    const bool savedFree = state().ui.gFreeCanvasForRequest;
    state().ui.gFreeCanvasForRequest = true;
    requestBefore();
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (largest < kTlsNeedBytes) {
        Serial.printf("[audio] prefetch skipped: largest %u\n", static_cast<unsigned>(largest));
    } else if (kk::audioCacheOpen(SD, currentAccountKey())) {
        kk::audioCachePrefetch(SD, subjects, audioVoice(), kPrefetchBudgetMs, kTlsNeedBytes);
    }
    requestAfter();
    state().ui.gFreeCanvasForRequest = savedFree;
    state().display.gDirty = true;
}

}  // namespace kk::app

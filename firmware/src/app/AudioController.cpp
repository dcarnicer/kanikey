#include "app/AudioController.h"
#include "app/AppUi.h"

#include <SD.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "app/AppState.h"
#include "app/CacheController.h"
#include "platform/AudioCache.h"
#include "platform/AudioPlayer.h"
#include "platform/WkClient.h"
#include "platform/DeviceSettings.h"
#include "platform/DevToken.h"

namespace kk::app {

namespace {

// True once the cache was prepared for the current visit to a review or lesson
// card screen. Cleared on leaving, so the next visit prepares again once.
bool gPreparedForVisit = false;

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

void audioTick() {
    if (!audioScreen()) {
        gPreparedForVisit = false;
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
    kk::audioPlayerTick();
}

void audioStop() { kk::audioPlayerStop(); }

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

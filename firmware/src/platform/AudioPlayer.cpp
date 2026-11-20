#include "platform/AudioPlayer.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <soc/soc_memory_layout.h>

#include <atomic>
#include <cstring>

#include "audio/Minimp3.h"
#include "platform/AudioCache.h"
#include "platform/Sound.h"
#include "wk/AudioPlan.h"

namespace kk {

namespace {

// One virtual channel is enough for one clip at a time. Sound (REM-03) must
// stop a tone before playback and playback before a tone; play() stops every
// channel first so a dev tone cannot overlap the clip.
constexpr int kChannel = 0;
constexpr size_t kFileBytes = 4096;
constexpr size_t kFrameSamples = 1152;  // per channel, one MPEG Layer III frame
// The largest MPEG frame (including free-format) is 2,304 B; keep that much in
// the window so a frame is never split at the buffer boundary.
constexpr size_t kMinFrameWindow = 2304;
constexpr uint32_t kStopWaitMs = 40;
constexpr uint32_t kHeapCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

size_t roundUp8(size_t n) { return (n + 7) & ~static_cast<size_t>(7); }

// Static state only, except the pieces it points at. The release callback
// touches only these fields, so a late callback never writes into memory that
// was given back.
struct Player {
    fs::FS* sd = nullptr;
    File file;
    bool playing = false;
    bool eof = false;
    bool registered = false;
    uint32_t accountKey = 0;  // prepared account; 0 = not ready

    uint32_t subjectId = 0;
    uint8_t voice = 0;
    size_t pos = 0;
    size_t len = 0;

    // Heap pieces, each its own internal allocation (null while not held).
    uint8_t* state = nullptr;    // mp3dec_t (~6.7 KB)
    uint8_t* scratch = nullptr;  // mp3dec_scratch_t (~16.2 KB)
    uint8_t* window = nullptr;  // kFileBytes
    int16_t* pcm[2] = {nullptr, nullptr};
    int pcmChannels = 0;        // 1 or 2; sets the PCM buffer size
    // stop() discards queued requests without a release callback, so the flags
    // of those buffers may never set. The forced release covers that case.
    bool releaseForced = false;
    bool borrowed = false;  // the app lent a block for this clip (heap lending)
    std::atomic<bool> bufferFree[2] = {};

    // A failed clip stays "no audio" until another subject starts, so the hint
    // cannot promise a clip that will fail again on every R.
    uint32_t badSubjectId = 0;
    uint8_t badVoice = 0;

    bool piecesLogged = false;
};

Player gPlayer;

size_t pcmBytes(int channels) { return kFrameSamples * channels * sizeof(int16_t); }

bool waitForIdle(uint32_t timeoutMs) {
    const uint32_t start = millis();
    while (M5.Speaker.isPlaying() && millis() - start < timeoutMs) delay(1);
    return M5.Speaker.isPlaying() == 0;
}

void closeFile() {
    if (gPlayer.file) gPlayer.file.close();
    gPlayer.playing = false;
    gPlayer.eof = false;
    gPlayer.pos = 0;
    gPlayer.len = 0;
}

void markBad(uint32_t subjectId, uint8_t voice) {
    gPlayer.badSubjectId = subjectId;
    gPlayer.badVoice = voice;
}

void setBufferFree(bool free0, bool free1) {
    gPlayer.bufferFree[0].store(free0, std::memory_order_release);
    gPlayer.bufferFree[1].store(free1, std::memory_order_release);
}

// Runs on the playback task. Compares the released pointer with the buffer
// addresses and sets a static flag. A freed buffer's pointer is null first, so
// a callback after the free does nothing.
void onBufferRelease(void*, const void* data, uint8_t channel) {
    if (channel != kChannel) return;
    if (gPlayer.pcm[0] && data == gPlayer.pcm[0])
        gPlayer.bufferFree[0].store(true, std::memory_order_release);
    else if (gPlayer.pcm[1] && data == gPlayer.pcm[1])
        gPlayer.bufferFree[1].store(true, std::memory_order_release);
}

AudioHeapBorrow gBorrow = nullptr;
AudioHeapGiveBack gGiveBack = nullptr;

// Returns a lent block once our pieces are gone.
void giveBackBorrowed() {
    if (!gPlayer.borrowed) return;
    gPlayer.borrowed = false;
    if (gGiveBack) gGiveBack();
}

bool admit(const size_t* pieces, size_t count) {
    return kk::audioAdmitsPieces(heap_caps_get_free_size(kHeapCaps),
                                 heap_caps_get_largest_free_block(kHeapCaps), pieces, count);
}

bool holdsPieces();

void logPieces(size_t stateB, size_t scratchB) {
    if (gPlayer.piecesLogged) return;
    gPlayer.piecesLogged = true;
    Serial.printf("[audio] pieces: state %u, scratch %u, window %u, pcm %u per buffer (%u buffers)\n",
                  static_cast<unsigned>(stateB), static_cast<unsigned>(scratchB),
                  static_cast<unsigned>(kFileBytes), static_cast<unsigned>(pcmBytes(gPlayer.pcmChannels)),
                  2u);
}

// Frees every held piece. The PCM pointers are cleared first so a late release
// callback does nothing. Only called once the speaker can no longer read them.
void freePieces() {
    if (!holdsPieces()) {
        giveBackBorrowed();
        return;
    }
    int16_t* pcm0 = gPlayer.pcm[0];
    int16_t* pcm1 = gPlayer.pcm[1];
    gPlayer.pcm[0] = nullptr;
    gPlayer.pcm[1] = nullptr;
    if (pcm0) heap_caps_free(pcm0);
    if (pcm1) heap_caps_free(pcm1);
    if (gPlayer.window) heap_caps_free(gPlayer.window);
    if (gPlayer.state) heap_caps_free(gPlayer.state);
    if (gPlayer.scratch) heap_caps_free(gPlayer.scratch);
    gPlayer.window = nullptr;
    gPlayer.state = nullptr;
    gPlayer.scratch = nullptr;
    gPlayer.pcmChannels = 0;
    gPlayer.releaseForced = false;
    Serial.printf("[audio] freed, heap %u/%u\n",
                  static_cast<unsigned>(heap_caps_get_free_size(kHeapCaps)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(kHeapCaps)));
    giveBackBorrowed();
}

bool holdsPieces() {
    return gPlayer.state || gPlayer.scratch || gPlayer.window || gPlayer.pcm[0] || gPlayer.pcm[1];
}

// The only place pieces are freed on a natural end. It needs the channel idle
// and no clip open, and either both buffers released or a forced release from
// stop(). Then no request can still read the buffers.
void tryFreePieces() {
    if (!holdsPieces() || gPlayer.playing) return;
    if (M5.Speaker.isPlaying(kChannel)) return;
    if (!gPlayer.releaseForced && !(gPlayer.bufferFree[0] && gPlayer.bufferFree[1])) return;
    freePieces();
}

// Allocates the scratch, the state, the window and the PCM buffers for
// `channels`, biggest first. The caller has already passed admission. A failed
// allocation frees whatever this call took.
bool allocPieces(int channels) {
    const size_t stateB = roundUp8(kk::audio::mp3StateBytes());
    const size_t scratchB = roundUp8(kk::audio::mp3ScratchBytes());
    if (!gPlayer.scratch) {
        gPlayer.scratch = static_cast<uint8_t*>(heap_caps_malloc(scratchB, kHeapCaps));
        if (!gPlayer.scratch) return false;
    }
    if (!gPlayer.state) {
        gPlayer.state = static_cast<uint8_t*>(heap_caps_malloc(stateB, kHeapCaps));
        if (!gPlayer.state) return false;
    }
    if (!gPlayer.window) {
        // The SD card reads straight into the window, so it must be DMA capable
        // (the S3 heap also holds RTC RAM, which is not).
        gPlayer.window = static_cast<uint8_t*>(
            heap_caps_malloc(kFileBytes, kHeapCaps | MALLOC_CAP_DMA));
        if (!gPlayer.window) return false;
    }
    if (gPlayer.pcm[0] && gPlayer.pcmChannels != channels) {
        // Stereo upgrade: the mono buffers are not in use, so release them.
        heap_caps_free(gPlayer.pcm[0]);
        heap_caps_free(gPlayer.pcm[1]);
        gPlayer.pcm[0] = gPlayer.pcm[1] = nullptr;
    }
    for (int i = 0; i < 2; ++i) {
        if (gPlayer.pcm[i]) continue;
        gPlayer.pcm[i] = static_cast<int16_t*>(heap_caps_malloc(pcmBytes(channels), kHeapCaps));
        if (!gPlayer.pcm[i]) return false;
    }
    gPlayer.pcmChannels = channels;
    setBufferFree(true, true);
    logPieces(stateB, scratchB);
    return true;
}

void failClip(uint32_t subjectId, uint8_t voice, AudioFailure failure) {
    if (audioFailureMarksBad(failure)) markBad(subjectId, voice);
}

// Make at least one complete frame available after gPlayer.pos. Compacts the
// remaining bytes and reads more from the card; at EOF it returns whatever is
// left, which may still be a complete final frame.
bool refillForFrame() {
    if (gPlayer.len - gPlayer.pos >= kMinFrameWindow) return true;
    if (gPlayer.eof) return gPlayer.len > gPlayer.pos;
    if (gPlayer.pos > 0) {
        const size_t left = gPlayer.len - gPlayer.pos;
        std::memmove(gPlayer.window, gPlayer.window + gPlayer.pos, left);
        gPlayer.len = left;
        gPlayer.pos = 0;
    }
    while (gPlayer.len < kMinFrameWindow && gPlayer.len < kFileBytes) {
        const int got = gPlayer.file.read(gPlayer.window + gPlayer.len, kFileBytes - gPlayer.len);
        if (got <= 0) {
            gPlayer.eof = true;
            break;
        }
        gPlayer.len += static_cast<size_t>(got);
    }
    return gPlayer.len > gPlayer.pos;
}

int freeBuffer() {
    for (int i = 0; i < 2; ++i)
        if (gPlayer.bufferFree[i].load(std::memory_order_acquire)) return i;
    return -1;
}

void finishStream() {
    gPlayer.playing = false;
    if (gPlayer.file) gPlayer.file.close();
    gPlayer.pos = 0;
    gPlayer.len = 0;
    gPlayer.eof = false;
}

}  // namespace

void audioPlayerBegin(fs::FS& sd) {
    gPlayer.sd = &sd;
    if (!gPlayer.registered) {
        // Register before the first playRaw() call. M5Unified asks for this to
        // happen before the speaker task exists; setup() runs before any clip.
        M5.Speaker.setBufferReleaseCallback(nullptr, onBufferRelease);
        gPlayer.registered = true;
    }
}

bool audioPlayerPrepare(uint32_t accountKey) {
    if (gPlayer.playing) audioPlayerStop();
    gPlayer.accountKey = 0;
    if (!gPlayer.sd || accountKey == 0) return false;
    if (!audioCacheOpen(*gPlayer.sd, accountKey)) return false;
    gPlayer.accountKey = accountKey;
    return true;
}

bool audioPlayerReady() { return gPlayer.accountKey != 0; }

bool audioPlayerPlay(const Subject& subject, uint8_t voice) {
    if (!gPlayer.sd || !audioSubjectPlayable(subject.type) || !audioPlayerReady()) return false;

    // Stop our stream and every tone first. A request discarded by stop() does
    // not call back, so wait for the channel to drain (bounded) and only then
    // reuse the pieces. A short settle covers a late release for the last clip.
    const bool hadPieces = holdsPieces();
    // A REM-03 cue still stepping would start its next tone over the clip.
    kk::sound::soundStop();
    audioPlayerStop();
    if (M5.Speaker.isPlaying()) M5.Speaker.stop();
    if (!waitForIdle(kStopWaitMs) || holdsPieces()) {
        Serial.println("[audio] speaker busy; not starting");
        return false;
    }
    if (hadPieces) delay(2);

    const AudioPlan plan = audioCachePlan(subject.id, subject.type, voice);
    if (!plan.playable) {
        Serial.printf("[audio] %u: no mp3\n", static_cast<unsigned>(subject.id));
        return false;
    }
    char path[80];
    if (!audioCachePath(plan.entry, path, sizeof path)) {
        Serial.printf("[audio] %u: bad path\n", static_cast<unsigned>(subject.id));
        return false;
    }
    File file = gPlayer.sd->open(path, "r");
    if (!file || file.size() == 0) {
        if (file) file.close();
        Serial.printf("[audio] %u: missing %s\n", static_cast<unsigned>(subject.id), path);
        failClip(subject.id, voice, AudioFailure::Missing);
        return false;
    }
    // Skip a leading ID3v2 tag whatever its size: the first frame search only
    // sees one 4 KB window.
    {
        uint8_t header[10];
        const int got = file.read(header, sizeof header);
        size_t skip = got == static_cast<int>(sizeof header)
                          ? kk::mp3Id3v2TagBytes(header, sizeof header)
                          : 0;
        if (skip >= file.size()) skip = 0;  // let the frame search report it
        if (!file.seek(skip)) {
            file.close();
            failClip(subject.id, voice, AudioFailure::Corrupt);
            return false;
        }
        if (skip)
            Serial.printf("[audio] %u: skipped a %u B ID3 tag\n", static_cast<unsigned>(subject.id),
                          static_cast<unsigned>(skip));
    }

    // Admission on the mono set (the WaniKani clips). A refusal is transient:
    // the subject keeps `R: audio` and the next R tries again.
    const size_t monoPieces[] = {roundUp8(kk::audio::mp3StateBytes()),
                                 roundUp8(kk::audio::mp3ScratchBytes()), kFileBytes,
                                 pcmBytes(1), pcmBytes(1)};
    if (!admit(monoPieces, 5) && gBorrow) {
        const size_t freeBefore = heap_caps_get_free_size(kHeapCaps);
        const size_t largestBefore = heap_caps_get_largest_free_block(kHeapCaps);
        if (gBorrow()) {
            gPlayer.borrowed = true;
            Serial.printf("[audio] borrowed the canvas: %u/%u -> %u/%u (dma largest %u)\n",
                          static_cast<unsigned>(freeBefore), static_cast<unsigned>(largestBefore),
                          static_cast<unsigned>(heap_caps_get_free_size(kHeapCaps)),
                          static_cast<unsigned>(heap_caps_get_largest_free_block(kHeapCaps)),
                          static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));
        }
    }
    if (!admit(monoPieces, 5)) {
        file.close();
        Serial.printf("[audio] no heap: free %u, largest %u\n",
                      static_cast<unsigned>(heap_caps_get_free_size(kHeapCaps)),
                      static_cast<unsigned>(heap_caps_get_largest_free_block(kHeapCaps)));
        giveBackBorrowed();
        failClip(subject.id, voice, AudioFailure::NoHeap);
        return false;
    }
    gPlayer.file = file;
    gPlayer.pos = 0;
    gPlayer.len = 0;
    gPlayer.eof = false;
    if (!allocPieces(1)) {
        freePieces();
        gPlayer.file.close();
        Serial.println("[audio] no heap: alloc");
        failClip(subject.id, voice, AudioFailure::NoHeap);
        return false;
    }

    // The first window tells us the channel mode; a stereo clip needs stereo
    // buffers, admitted on what is free now.
    const int channels = refillForFrame()
                             ? kk::mp3FirstFrameChannels(gPlayer.window + gPlayer.pos,
                                                         gPlayer.len - gPlayer.pos)
                             : 0;
    if (channels == 0) {
        Serial.printf("[audio] %u: no MP3 frame: len %u pos %u eof %d window %p dma %d first %02x %02x %02x %02x\n",
                      static_cast<unsigned>(subject.id), static_cast<unsigned>(gPlayer.len),
                      static_cast<unsigned>(gPlayer.pos), gPlayer.eof ? 1 : 0, gPlayer.window,
                      esp_ptr_dma_capable(gPlayer.window) ? 1 : 0,
                      gPlayer.len > 0 ? gPlayer.window[0] : 0, gPlayer.len > 1 ? gPlayer.window[1] : 0,
                      gPlayer.len > 2 ? gPlayer.window[2] : 0, gPlayer.len > 3 ? gPlayer.window[3] : 0);
        freePieces();
        gPlayer.file.close();
        failClip(subject.id, voice, AudioFailure::Corrupt);
        return false;
    }
    if (channels == 2) {
        // Release the mono PCM buffers before the check, so admission sees the
        // real stereo set (state + scratch + window + 2 stereo buffers) and not
        // the mono buffers on top of it (seen refusing a stereo clip at 47 KB
        // free in a review, 2026-11-20).
        if (gPlayer.pcm[0]) heap_caps_free(gPlayer.pcm[0]);
        if (gPlayer.pcm[1]) heap_caps_free(gPlayer.pcm[1]);
        gPlayer.pcm[0] = gPlayer.pcm[1] = nullptr;
        const size_t stereoPcm[] = {pcmBytes(2), pcmBytes(2)};
        if (!admit(stereoPcm, 2) || !allocPieces(2)) {
            Serial.printf("[audio] no heap: stereo, free %u, largest %u\n",
                          static_cast<unsigned>(heap_caps_get_free_size(kHeapCaps)),
                          static_cast<unsigned>(heap_caps_get_largest_free_block(kHeapCaps)));
            freePieces();
            gPlayer.file.close();
            failClip(subject.id, voice, AudioFailure::NoHeap);
            return false;
        }
    }

    gPlayer.subjectId = subject.id;
    gPlayer.voice = plan.entry.voice;
    gPlayer.badSubjectId = 0;
    gPlayer.badVoice = 0;
    gPlayer.playing = true;
    kk::audio::mp3Reset(gPlayer.state);
    Serial.printf("[audio] alloc %u, heap %u/%u after\n",
                  static_cast<unsigned>(roundUp8(kk::audio::mp3StateBytes()) +
                                        roundUp8(kk::audio::mp3ScratchBytes()) + kFileBytes +
                                        2 * pcmBytes(gPlayer.pcmChannels)),
                  static_cast<unsigned>(heap_caps_get_free_size(kHeapCaps)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(kHeapCaps)));
    Serial.printf("[audio] play %u voice %u (%d ch) -> %s (%u bytes)\n",
                  static_cast<unsigned>(subject.id), static_cast<unsigned>(plan.entry.voice),
                  channels, path, static_cast<unsigned>(file.size()));

    // Decode the first frame now: a corrupt or non-MP3 file must leave the
    // player stopped, the pieces freed and the hint at "no audio".
    audioPlayerTick();
    if (!gPlayer.playing) {
        audioPlayerStop();
        failClip(subject.id, voice, AudioFailure::Corrupt);
        return false;
    }
    return true;
}

void audioPlayerStop() {
    gPlayer.playing = false;
    if (M5.Speaker.isPlaying(kChannel)) M5.Speaker.stop(kChannel);
    closeFile();
    if (holdsPieces()) {
        waitForIdle(kStopWaitMs);
        gPlayer.releaseForced = true;
        tryFreePieces();
    }
}

void audioPlayerTick() {
    tryFreePieces();
    if (!gPlayer.playing) return;
    if (!gPlayer.file) {
        closeFile();
        return;
    }

    while (M5.Speaker.isPlaying(kChannel) < 2) {
        const int index = freeBuffer();
        if (index < 0) break;
        if (!refillForFrame()) {
            finishStream();
            break;
        }

        const size_t available = gPlayer.len - gPlayer.pos;
        kk::audio::Mp3FrameInfo info;
        int16_t* pcm = gPlayer.pcm[index];
        const int samples = kk::audio::mp3DecodeFrame(gPlayer.state, gPlayer.scratch,
                                                      gPlayer.window + gPlayer.pos,
                                                      static_cast<int>(available), pcm,
                                                      gPlayer.pcmChannels, &info);
        if (info.channels > gPlayer.pcmChannels) {
            // A stereo frame inside a clip that was admitted as mono: refused
            // before the decoder wrote anything.
            Serial.printf("[audio] %u: channel change\n", static_cast<unsigned>(gPlayer.subjectId));
            failClip(gPlayer.subjectId, gPlayer.voice, AudioFailure::Corrupt);
            audioPlayerStop();
            break;
        }
        if (info.frameBytes <= 0) {
            // No sync in the whole window, or an incomplete frame at EOF:
            // discard the window rather than spin on the same bytes.
            if (available == 0) {
                finishStream();
                break;
            }
            gPlayer.pos += available;
            if (gPlayer.eof) {
                finishStream();
                break;
            }
            continue;
        }
        const size_t consumed = static_cast<size_t>(info.frameBytes);
        if (consumed > available) {
            finishStream();
            break;
        }
        gPlayer.pos += consumed;

        if (samples <= 0) {
            // Skipped bytes or an incomplete final frame. Keep going unless
            // the file is exhausted.
            if (gPlayer.eof && gPlayer.pos >= gPlayer.len) {
                finishStream();
                break;
            }
            continue;
        }
        if (info.channels < 1 || info.hz <= 0) {
            Serial.printf("[audio] %u: bad frame\n", static_cast<unsigned>(gPlayer.subjectId));
            failClip(gPlayer.subjectId, gPlayer.voice, AudioFailure::Corrupt);
            audioPlayerStop();
            break;
        }

        gPlayer.bufferFree[index].store(false, std::memory_order_release);
        const bool queued = M5.Speaker.playRaw(pcm, static_cast<size_t>(samples) * info.channels,
                                               static_cast<uint32_t>(info.hz), info.channels == 2,
                                               1, kChannel, false);
        if (!queued) {
            gPlayer.bufferFree[index].store(true, std::memory_order_release);
            Serial.printf("[audio] %u: speaker queue full\n",
                          static_cast<unsigned>(gPlayer.subjectId));
            audioPlayerStop();
            break;
        }
    }
}

void audioPlayerSetHeapLender(AudioHeapBorrow borrow, AudioHeapGiveBack giveBack) {
    gBorrow = borrow;
    gGiveBack = giveBack;
}

bool audioPlayerIsPlaying() { return gPlayer.playing || M5.Speaker.isPlaying(kChannel) != 0; }

AudioHint audioPlayerHint(const Subject& subject, uint8_t voice) {
    if (!audioSubjectPlayable(subject.type)) return AudioHint::None;
    // In-memory only: the index is opened on screen entry, never from a draw.
    if (!audioPlayerReady()) return AudioHint::Missing;
    if (gPlayer.badSubjectId == subject.id && gPlayer.badVoice == voice)
        return AudioHint::Missing;
    const AudioPlan plan = audioCachePlan(subject.id, subject.type, voice);
    return plan.playable ? AudioHint::Available : AudioHint::Missing;
}

}  // namespace kk

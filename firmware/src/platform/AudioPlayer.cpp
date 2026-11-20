#include "platform/AudioPlayer.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <esp_heap_caps.h>

#include <atomic>
#include <cstring>

#include "audio/Minimp3.h"
#include "platform/AudioCache.h"

namespace kk {

namespace {

// One virtual channel is enough for one clip at a time. Sound (REM-03) must
// stop a tone before playback and playback before a tone; play() stops every
// channel first so a dev tone cannot overlap the clip.
constexpr int kChannel = 0;
constexpr size_t kFileBytes = 4096;
// A stereo frame: 1152 samples per channel, interleaved int16.
constexpr size_t kPcmSamples = 1152 * 2;
constexpr size_t kPcmBytes = kPcmSamples * sizeof(int16_t);
// The largest MPEG frame (including free-format) is 2,304 B; keep that much in
// the window so a frame is never split at the buffer boundary.
constexpr size_t kMinFrameWindow = 2304;
// The block is only allocated when this much stays free beside it, so TLS and
// the loop keep their headroom.
constexpr size_t kHeapFloor = 16 * 1024;
constexpr uint32_t kStopWaitMs = 40;
constexpr uint32_t kHeapCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

size_t roundUp8(size_t n) { return (n + 7) & ~static_cast<size_t>(7); }
size_t workBytes() { return roundUp8(kk::audio::mp3WorkBytes()); }
// Layout: [decoder state + scratch | file window | pcm buffer 0 | pcm buffer 1]
size_t blockBytes() { return workBytes() + kFileBytes + 2 * kPcmBytes; }

// Static state only. The release callback touches nothing else, so a late
// callback can never write into memory the block has given back.
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

    // Heap block (null while none is allocated) and the pointers into it.
    uint8_t* block = nullptr;
    uint8_t* work = nullptr;
    uint8_t* window = nullptr;
    int16_t* pcm = nullptr;  // pcm buffer 0; buffer 1 follows it
    // stop() discards queued requests without a release callback, so the flags
    // of those buffers may never set. The forced release covers that case.
    bool releaseForced = false;
    std::atomic<bool> bufferFree[2] = {};

    // A failed clip stays "no audio" until another subject starts, so the hint
    // cannot promise a clip that will fail again on every R.
    uint32_t badSubjectId = 0;
    uint8_t badVoice = 0;
};

Player gPlayer;

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

// Runs on the playback task. Compares the released pointer with the two buffer
// addresses and sets a static flag. After freeBlock() pcm is null and the
// callback does nothing.
void onBufferRelease(void*, const void* data, uint8_t channel) {
    int16_t* pcm = gPlayer.pcm;
    if (channel != kChannel || !pcm) return;
    if (data == pcm)
        gPlayer.bufferFree[0].store(true, std::memory_order_release);
    else if (data == pcm + kPcmSamples)
        gPlayer.bufferFree[1].store(true, std::memory_order_release);
}

// One heap allocation for the whole decode path. Refuses when less than
// kHeapFloor would remain free beside it.
bool allocBlock() {
    if (gPlayer.block) return true;
    const size_t need = blockBytes();
    const size_t largest = heap_caps_get_largest_free_block(kHeapCaps);
    if (largest < need + kHeapFloor) {
        Serial.printf("[audio] no heap: need %u, largest %u\n", static_cast<unsigned>(need),
                      static_cast<unsigned>(largest));
        return false;
    }
    uint8_t* block = static_cast<uint8_t*>(heap_caps_malloc(need, kHeapCaps));
    if (!block) {
        Serial.println("[audio] alloc failed");
        return false;
    }
    gPlayer.block = block;
    gPlayer.work = block;
    gPlayer.window = block + workBytes();
    gPlayer.pcm = reinterpret_cast<int16_t*>(block + workBytes() + kFileBytes);
    setBufferFree(true, true);
    Serial.printf("[audio] alloc %u, heap %u/%u after\n", static_cast<unsigned>(need),
                  static_cast<unsigned>(heap_caps_get_free_size(kHeapCaps)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(kHeapCaps)));
    return true;
}

void freeBlock() {
    if (!gPlayer.block) return;
    // Null first: a callback that runs from here on does nothing.
    gPlayer.pcm = nullptr;
    gPlayer.work = nullptr;
    gPlayer.window = nullptr;
    heap_caps_free(gPlayer.block);
    gPlayer.block = nullptr;
    gPlayer.releaseForced = false;
    Serial.printf("[audio] freed, heap %u/%u\n",
                  static_cast<unsigned>(heap_caps_get_free_size(kHeapCaps)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(kHeapCaps)));
}

// The only place the block is freed. It needs the channel idle and no clip
// open, and either both buffers released (natural end) or a forced release
// from stop(). Either way no request can still read the buffers.
void tryFreeBlock() {
    if (!gPlayer.block || gPlayer.playing) return;
    if (M5.Speaker.isPlaying(kChannel)) return;
    if (!gPlayer.releaseForced && !(gPlayer.bufferFree[0] && gPlayer.bufferFree[1])) return;
    freeBlock();
}

int freeBuffer() {
    for (int i = 0; i < 2; ++i)
        if (gPlayer.bufferFree[i].load(std::memory_order_acquire)) return i;
    return -1;
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
    // reuse the block. A short settle covers a late release for the last clip.
    const bool hadBlock = gPlayer.block != nullptr;
    audioPlayerStop();
    if (M5.Speaker.isPlaying()) M5.Speaker.stop();
    if (!waitForIdle(kStopWaitMs) || gPlayer.block) {
        Serial.println("[audio] speaker busy; not starting");
        return false;
    }
    if (hadBlock) delay(2);

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
        markBad(subject.id, voice);
        return false;
    }
    if (!allocBlock()) {
        file.close();
        markBad(subject.id, voice);
        return false;
    }

    gPlayer.file = file;
    gPlayer.subjectId = subject.id;
    gPlayer.voice = plan.entry.voice;
    gPlayer.badSubjectId = 0;
    gPlayer.badVoice = 0;
    gPlayer.pos = 0;
    gPlayer.len = 0;
    gPlayer.eof = false;
    gPlayer.playing = true;
    kk::audio::mp3Reset(gPlayer.work);
    Serial.printf("[audio] play %u voice %u -> %s (%u bytes)\n",
                  static_cast<unsigned>(subject.id), static_cast<unsigned>(plan.entry.voice), path,
                  static_cast<unsigned>(file.size()));

    // Decode the first frame now: a corrupt or non-MP3 file must leave the
    // player stopped, the block freed and the hint at "no audio".
    audioPlayerTick();
    if (!gPlayer.playing) {
        audioPlayerStop();
        markBad(subject.id, voice);
        return false;
    }
    return true;
}

void audioPlayerStop() {
    gPlayer.playing = false;
    if (M5.Speaker.isPlaying(kChannel)) M5.Speaker.stop(kChannel);
    closeFile();
    if (gPlayer.block) {
        waitForIdle(kStopWaitMs);
        gPlayer.releaseForced = true;
        tryFreeBlock();
    }
}

void audioPlayerTick() {
    tryFreeBlock();
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
        int16_t* pcm = gPlayer.pcm + index * kPcmSamples;
        const int samples = kk::audio::mp3DecodeFrame(gPlayer.work, gPlayer.window + gPlayer.pos,
                                                      static_cast<int>(available), pcm, &info);
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
        if (info.channels < 1 || info.channels > 2 || info.hz <= 0) {
            Serial.printf("[audio] %u: bad frame\n", static_cast<unsigned>(gPlayer.subjectId));
            markBad(gPlayer.subjectId, gPlayer.voice);
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

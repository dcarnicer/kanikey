#include "platform/AudioPlayer.h"

#include <Arduino.h>
#include <M5Unified.h>

#include <atomic>
#include <cstdlib>
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
// MINIMP3_MAX_SAMPLES_PER_FRAME is 1152 per channel, 2304 interleaved. Two
// buffers of 2,304 int16 samples (4,608 B) cover a stereo frame; a mono frame
// uses the first 2,304 B.
constexpr size_t kPcmSamples = 1152 * 2;
// The largest MPEG frame (including free-format) is 2,304 B. Keep that much in
// the file window so a frame is never skipped at the buffer boundary.
constexpr size_t kMinFrameWindow = 2304;

struct Player {
    fs::FS* sd = nullptr;
    File file;
    bool playing = false;
    bool eof = false;
    bool registered = false;

    uint32_t subjectId = 0;
    uint8_t voice = 0;
    size_t pos = 0;
    size_t len = 0;
    uint8_t fileBytes[kFileBytes] = {};

    int16_t* pcm = nullptr;  // two buffers of kPcmSamples int16 each
    std::atomic<bool> bufferFree[2];

    // A corrupt clip stays "no audio" until another subject starts, so the
    // hint cannot promise a clip that will fail again on every R.
    uint32_t badSubjectId = 0;
    uint8_t badVoice = 0;
};

Player gPlayer;

bool waitForIdle(uint32_t timeoutMs) {
    const uint32_t start = millis();
    while (M5.Speaker.isPlaying(kChannel) && millis() - start < timeoutMs) delay(1);
    return M5.Speaker.isPlaying(kChannel) == 0;
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

void onBufferRelease(void*, const void* data, uint8_t channel) {
    if (channel != kChannel || !gPlayer.pcm) return;
    if (data == gPlayer.pcm)
        gPlayer.bufferFree[0].store(true, std::memory_order_release);
    else if (data == gPlayer.pcm + kPcmSamples)
        gPlayer.bufferFree[1].store(true, std::memory_order_release);
}

bool ensurePcm() {
    if (gPlayer.pcm) return true;
    gPlayer.pcm = static_cast<int16_t*>(malloc(2 * kPcmSamples * sizeof(int16_t)));
    if (!gPlayer.pcm) return false;
    gPlayer.bufferFree[0].store(true, std::memory_order_relaxed);
    gPlayer.bufferFree[1].store(true, std::memory_order_relaxed);
    return true;
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
        std::memmove(gPlayer.fileBytes, gPlayer.fileBytes + gPlayer.pos, left);
        gPlayer.len = left;
        gPlayer.pos = 0;
    }
    while (gPlayer.len < kMinFrameWindow && gPlayer.len < sizeof(gPlayer.fileBytes)) {
        const int got =
            gPlayer.file.read(gPlayer.fileBytes + gPlayer.len, sizeof(gPlayer.fileBytes) - gPlayer.len);
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
        // Register before the first play*() call. M5Unified asks for this to
        // happen before the speaker task exists; setup() runs before any clip.
        M5.Speaker.setBufferReleaseCallback(nullptr, onBufferRelease);
        gPlayer.registered = true;
    }
}

bool audioPlayerPlay(uint32_t accountKey, const Subject& subject, uint8_t voice) {
    if (!gPlayer.sd || !audioSubjectPlayable(subject.type)) return false;

    // Stop our stream and every tone channel first. A queued request discarded
    // by stop() does not call the release callback, so wait for the channel to
    // drain and give the playback task a moment to deliver the last callback
    // before reusing the two buffers.
    audioPlayerStop();
    M5.Speaker.stop();
    if (!waitForIdle(80)) {
        Serial.println("[audio] speaker busy; not starting");
        return false;
    }
    delay(2);
    gPlayer.bufferFree[0].store(true, std::memory_order_relaxed);
    gPlayer.bufferFree[1].store(true, std::memory_order_relaxed);

    if (!ensurePcm()) {
        Serial.println("[audio] no PCM buffers");
        return false;
    }
    if (!audioCacheOpen(*gPlayer.sd, accountKey)) {
        Serial.println("[audio] cache unavailable");
        return false;
    }
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

    gPlayer.file = file;
    gPlayer.subjectId = subject.id;
    gPlayer.voice = plan.entry.voice;
    gPlayer.badSubjectId = 0;
    gPlayer.badVoice = 0;
    gPlayer.pos = 0;
    gPlayer.len = 0;
    gPlayer.eof = false;
    gPlayer.playing = true;
    audio::mp3Reset();
    Serial.printf("[audio] play %u voice %u -> %s (%u bytes)\n",
                  static_cast<unsigned>(subject.id), static_cast<unsigned>(plan.entry.voice), path,
                  static_cast<unsigned>(file.size()));

    // Decode the first frame now: a corrupt or non-MP3 file must leave the
    // player stopped and the hint at "no audio" instead of pretending to play.
    audioPlayerTick();
    if (!gPlayer.playing) {
        markBad(subject.id, voice);
        return false;
    }
    return true;
}

void audioPlayerStop() {
    gPlayer.playing = false;
    if (M5.Speaker.isPlaying(kChannel)) M5.Speaker.stop(kChannel);
    closeFile();
}

void audioPlayerTick() {
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
        audio::Mp3FrameInfo info;
        int16_t* pcm = gPlayer.pcm + index * kPcmSamples;
        const int samples = audio::mp3DecodeFrame(gPlayer.fileBytes + gPlayer.pos,
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

AudioHint audioPlayerHint(uint32_t accountKey, const Subject& subject, uint8_t voice) {
    if (!audioSubjectPlayable(subject.type)) return AudioHint::None;
    if (!gPlayer.sd) return AudioHint::Missing;
    if (!audioCacheOpen(*gPlayer.sd, accountKey)) return AudioHint::Missing;
    if (gPlayer.badSubjectId == subject.id && gPlayer.badVoice == voice)
        return AudioHint::Missing;
    const AudioPlan plan = audioCachePlan(subject.id, subject.type, voice);
    if (!plan.playable) return AudioHint::Missing;
    char path[80];
    if (!audioCachePath(plan.entry, path, sizeof path)) return AudioHint::Missing;
    if (!gPlayer.sd->exists(path)) return AudioHint::Missing;
    return AudioHint::Available;
}

}  // namespace kk

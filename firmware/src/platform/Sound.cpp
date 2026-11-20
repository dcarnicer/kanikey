// REM-03 cue playback. The cue's steps are started one at a time: soundCue()
// starts the first step and soundTick() starts the next when the current step's
// duration has passed. Nothing waits, so no key is delayed by a tone.
#include "platform/Sound.h"

#include <Arduino.h>

#include "board/Board.h"
#include "platform/DeviceSettings.h"

namespace kk::sound {

namespace {

SoundSequence gSeq{nullptr, 0};
size_t gStep = 0;
uint32_t gStepEndsAt = 0;
uint8_t gLevel = 128;
bool gPlaying = false;

// Starts gSeq.steps[gStep]. False when the speaker refused the tone.
bool startStep(uint32_t now) {
    const SoundStep& step = gSeq.steps[gStep];
    gStepEndsAt = now + step.durationMs;
    if (step.frequency == 0) return true;  // a rest
    return tgc::board::playTone(step.frequency, step.durationMs, gLevel);
}

}  // namespace

void soundStop() {
    tgc::board::speakerStop();
    gPlaying = false;
    gSeq = SoundSequence{nullptr, 0};
    gStep = 0;
}

bool soundCue(SoundCue cue, uint32_t reviewsCompleted, bool syncing, bool portal) {
    int soundsOn = 0;
    int volume = 1;  // Medium
    kk::settingsGetInt("rm.sound", &soundsOn);
    kk::settingsGetInt("rm.volume", &volume);
    if (!kk::soundAllowed(soundsOn == 1, syncing, portal)) return false;
    const SoundSequence seq = kk::soundSequence(cue, reviewsCompleted);
    if (!seq.count) return false;
    soundStop();  // a cue still running gives way to this one
    gSeq = seq;
    gStep = 0;
    gLevel = kk::soundLevelForVolume(volume);
    if (!startStep(millis())) {
        soundStop();
        return false;
    }
    gPlaying = true;
    return true;
}

void soundAnswer(AnswerStatus status, bool syncing, bool portal) {
    SoundCue cue = SoundCue::AnswerWrong;
    if (kk::soundCueForAnswer(status, &cue)) soundCue(cue, 0, syncing, portal);
}

void soundTick(uint32_t now) {
    if (!gPlaying) return;
    if (static_cast<int32_t>(now - gStepEndsAt) < 0) return;  // still sounding
    if (++gStep >= gSeq.count) {
        soundStop();
        return;
    }
    if (!startStep(now)) soundStop();
}

#ifdef KANIKEY_DEV
namespace {

int gDevVolume = -1;  // `cue vol`: -1 follows M5 Volume

int devVolumeChoice() {
    if (gDevVolume >= 0) return gDevVolume;
    int volume = 1;  // Medium
    kk::settingsGetInt("rm.volume", &volume);
    return volume;
}

}  // namespace

bool soundDevCue(SoundCue cue, uint32_t reviewsCompleted) {
    const SoundSequence seq = kk::soundSequence(cue, reviewsCompleted);
    if (!seq.count) return false;
    soundStop();
    gSeq = seq;
    gStep = 0;
    gLevel = kk::soundLevelForVolume(devVolumeChoice());
    if (!startStep(millis())) {
        soundStop();
        return false;
    }
    gPlaying = true;
    return true;
}

void soundDevSetVolume(int volumeChoice) { gDevVolume = volumeChoice; }

uint8_t soundDevLevel() { return kk::soundLevelForVolume(devVolumeChoice()); }
#endif

}  // namespace kk::sound

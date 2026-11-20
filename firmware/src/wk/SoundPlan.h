#pragma once
// REM-03 sounds: which tones each cue plays, at which level, and when a cue may
// play at all. Pure C++ (no Arduino); platform/Sound plays the table through
// the board's speaker and owns the timing.
#include <cstddef>
#include <cstdint>

#include "wk/AnswerChecker.h"

namespace kk {

enum class SoundCue : uint8_t {
    ReminderChime,  // REM-02 banner on a timer wake
    AnswerCorrect,  // review or lesson quiz: a correct answer
    AnswerWrong,    // review or lesson quiz: a wrong answer
    SessionEnd,     // review summary, only after at least one review
};

// One step of a cue: a tone at `frequency` Hz for `durationMs`, or a rest
// (silence) when frequency is 0. Every step is at most kSoundStepMaxMs long.
struct SoundStep {
    uint16_t frequency;
    uint16_t durationMs;
};

constexpr uint16_t kSoundStepMaxMs = 120;

struct SoundSequence {
    const SoundStep* steps;
    size_t count;
};

// The steps of `cue`. SessionEnd with no completed review is empty (count 0):
// a session that answered nothing ends silently.
SoundSequence soundSequence(SoundCue cue, uint32_t reviewsCompleted);

// The cue for a checked answer: Correct and Incorrect have one; a Warning (a
// typo to retype the same part) has none and returns false.
bool soundCueForAnswer(AnswerStatus status, SoundCue* cue);

// M5 Volume (choice values 0 Low, 1 Medium, 2 High) to the speaker's master
// level, 0..255. An unknown choice is Medium.
uint8_t soundLevelForVolume(int volumeChoice);

// True when a cue may play: M4 Sounds is On, no sync is running and the setup
// portal is not open. Off (the default) plays nothing anywhere.
bool soundAllowed(bool soundsOn, bool syncing, bool portal);

}  // namespace kk

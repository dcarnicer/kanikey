#pragma once
// REM-03 cue playback on the board's speaker. Non-blocking: soundCue() starts
// the cue's first step, and soundTick(), called once per loop pass, starts each
// later step when the previous one has run its duration. Cues never overlap.
#include <cstdint>

#include "wk/AnswerChecker.h"
#include "wk/SoundPlan.h"

namespace kk::sound {

// Plays `cue` when M4 Sounds is On, no sync is running and the setup portal is
// closed; the level comes from M5 Volume. Stops a cue still running. False when
// nothing started: sounds off or blocked, no speaker in this build (PaperDIY),
// or no steps (SessionEnd with no completed review).
bool soundCue(SoundCue cue, uint32_t reviewsCompleted, bool syncing, bool portal);

// The tone for a checked answer (review or lesson quiz); a Warning is silent.
void soundAnswer(AnswerStatus status, bool syncing, bool portal);

// Advances the running cue. `now` is millis(). Call once per loop pass before
// any early return, so a cue finishes even while the panel is blank.
void soundTick(uint32_t now);

// Stops the running cue and the speaker now. AUD-03 playback calls this before
// it starts a clip (not wired here: that code is on another branch).
void soundStop();

}  // namespace kk::sound

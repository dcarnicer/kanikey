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
// it starts a clip (platform/AudioPlayer.cpp audioPlayerPlay()).
void soundStop();

// True while a cue still has steps to sound (until its last step's duration has
// passed). M6 autoplay waits for this to clear before it starts a clip.
bool soundBusy();

#ifdef KANIKEY_DEV
// REM-03 dev `cue`: plays `cue` whatever M4 says, and without the sync or portal
// check. The level is M5's, or the `cue vol` override. False when no step played
// (no speaker in this build, or a SessionEnd with no completed review).
bool soundDevCue(SoundCue cue, uint32_t reviewsCompleted);

// Dev `cue vol`: the level for later dev cues, as a volume choice (0 Low,
// 1 Medium, 2 High). RAM only; -1 follows M5 again.
void soundDevSetVolume(int volumeChoice);

// The speaker level the next dev cue plays at, 0..255.
uint8_t soundDevLevel();
#endif

}  // namespace kk::sound

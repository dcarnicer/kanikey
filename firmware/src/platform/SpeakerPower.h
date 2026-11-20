#pragma once
// MEM-15: the speaker's I2S port and playback task (about 6.4 KB of heap, E) are
// released after kIdleMs with no clip or cue playing, and begun again by the next
// sound. Every start calls wake() first, so the `[audio] speaker on` log line
// comes before the sound; tick() runs once per loop pass.
#include <cstdint>

namespace kk::speaker {

// The one idle delay: the speaker stays up this long after the last sound.
inline constexpr uint32_t kIdleMs = 3000;

// Before a clip or cue starts: begins the speaker if it was ended, and marks it
// busy. Safe to call while the speaker is already up.
void wake();

// Once per loop pass. `soundPending` is true while a clip or cue is playing or
// still has steps to sound. Ends the speaker after kIdleMs with no sound and
// logs `[audio] speaker off`.
void tick(uint32_t now, bool soundPending);

}  // namespace kk::speaker

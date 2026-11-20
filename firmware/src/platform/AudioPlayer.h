#pragma once
// AUD-03: on-demand vocabulary audio. Opens one cached MP3, decodes one frame
// per tick into one of two small PCM buffers and hands it to M5.Speaker. No
// whole-file buffer, no network and no autoplay. The decoder is MP3 only; an
// OGG entry or a missing/corrupt file reports Missing through the hint path.
#include <FS.h>

#include <cstdint>

#include "wk/AudioPlan.h"
#include "wk/Types.h"

namespace kk {

// Called once after the SD card is mounted. Registers the M5Unified buffer
// release callback before the first playback.
void audioPlayerBegin(fs::FS& sd);

// Starts (or restarts) the subject's preferred-voice MP3, falling back to the
// other voice. Stops any current clip/tone first. False when there is no
// playable clip, the card/file is gone, the decoder finds no MP3 frame or the
// PCM buffers cannot be allocated. Never retries and never touches the network.
bool audioPlayerPlay(uint32_t accountKey, const Subject& subject, uint8_t voice);

// Stops playback and closes the file. Does not wait for the speaker task; a
// following play() waits for the channel to become idle before reusing buffers.
void audioPlayerStop();

// Decodes/flushes at most one MP3 frame's worth of work. Non-blocking apart
// from a small SD read; call from the app loop.
void audioPlayerTick();

// True while our channel still has queued or audible PCM.
bool audioPlayerIsPlaying();

// The hint state for this subject, including the file-existence check and a
// remembered decode failure. Does not start playback.
AudioHint audioPlayerHint(uint32_t accountKey, const Subject& subject, uint8_t voice);

}  // namespace kk

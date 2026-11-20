#pragma once
// AUD-03: on-demand vocabulary audio. Heap pieces (decoder+scratch, a 4 KB file
// window, two PCM buffers) are allocated on R under an admission rule and freed
// when the clip ends, stops or fails. One clip plays at a time through
// M5.Speaker.playRaw(). No whole-file buffer, no network, no autoplay. Only
// audio/mpeg (MP3) clips play; OGG, missing or corrupt clips report Missing.
#include <FS.h>

#include <cstdint>

#include "wk/AudioPlan.h"
#include "wk/Types.h"

namespace kk {

// Called once after the SD card is mounted. Registers the M5Unified buffer
// release callback before the first playback.
void audioPlayerBegin(fs::FS& sd);

// Opens the account's audio index (may create it or clear the audio directory
// on an account change, so it is SD I/O and must not run from a draw). Called
// on entry to the review and lesson card screens. False when there is no SD
// card or no account; the hint then reads `no audio`.
bool audioPlayerPrepare(uint32_t accountKey);

// True after a successful audioPlayerPrepare() and until the next prepare.
bool audioPlayerReady();

// Starts the subject's preferred-voice MP3, falling back to the other voice.
// Requires audioPlayerReady(). Stops any current clip/tone first. False when
// there is no playable clip, the file is gone or corrupt, the decoder finds
// no MP3 frame, or the heap block cannot be allocated (`no audio` then holds
// for this subject until another subject plays). Never retries.
bool audioPlayerPlay(const Subject& subject, uint8_t voice);

// Stops playback, closes the file and frees the decode block once the speaker
// has released its buffers. Bounded wait (about 40 ms) for the channel to go
// idle. A following play() does not reuse memory the speaker still holds.
void audioPlayerStop();

// Frees the block after a natural end once the speaker has released both
// buffers, and decodes/queues at most two MP3 frames. Non-blocking apart from
// a small SD read; call from the app loop.
void audioPlayerTick();

// True while our channel still has queued or audible PCM, or a clip is open.
bool audioPlayerIsPlaying();

// Heap lending (2026-11-20). In a review the heap can be too small for the
// ~31.6 KB of decode pieces (36 KB free, 18 KB largest measured) while the
// display canvas holds a 32.4 KB block and the feedback frame is already on
// the panel. When admission fails, `borrow` may free such a block (true when
// it did); `giveBack` runs once the pieces are freed again (end, stop or a
// failed start). Both run on the app loop.
using AudioHeapBorrow = bool (*)();
using AudioHeapGiveBack = void (*)();
void audioPlayerSetHeapLender(AudioHeapBorrow borrow, AudioHeapGiveBack giveBack);

// The hint state for this subject from in-memory state only: no SD access, no
// NVS access. Does not start playback.
AudioHint audioPlayerHint(const Subject& subject, uint8_t voice);

}  // namespace kk

#pragma once
// KaniKey's narrow interface to the vendored minimp3 decoder.
//
// The implementation is Minimp3.cpp: minimp3 (CC0, https://github.com/lieff/
// minimp3) with MINIMP3_ONLY_MP3. The decoder state and its decode scratch are
// two caller-owned allocations (AudioPlayer heap pieces): neither is static RAM
// and neither is a stack local. The decoder is non-reentrant; one clip is
// decoded at a time from platform/AudioPlayer. Native builds never compile it.
#include <cstddef>
#include <cstdint>

namespace kk::audio {

// The largest MPEG-1/2 Layer III frame the decoder searches for: 320 kbit/s at
// 32 kHz is 1,440 B (MEM-16; the decoder used to search 2,304 B for free-format
// frames). AudioPlayer sizes its file window from it, so a whole frame fits.
inline constexpr size_t kMp3MaxFrameBytes = 1440;

// MPEG frame metadata the player needs for M5.Speaker.playRaw(). `frameBytes`
// counts the leading ID3/garbage bytes the decoder skipped plus the frame, so
// the caller advances its file window by that amount.
struct Mp3FrameInfo {
    int frameBytes = 0;
    int channels = 0;
    int hz = 0;
};

// Bytes of the two allocations one decoder needs: the state (~6.7 KB) and the
// scratch (~16.2 KB). Both are constant for the build and need 4-byte alignment
// (heap allocations are 8-byte aligned).
size_t mp3StateBytes();
size_t mp3ScratchBytes();

// Zero the state before a new file. Required; a state from a previous clip must
// not be reused without it.
void mp3Reset(void* state);

// Decode one MPEG-1/2/2.5 Layer III frame from `data` into interleaved signed
// 16-bit PCM. `pcm` must hold 1152 * maxChannels samples. A frame with more
// channels than maxChannels is not decoded: info->channels reports it and 0 is
// returned. Returns the samples per channel (0 on EOF, corrupt input or a
// non-MPEG frame); `info` is always filled.
int mp3DecodeFrame(void* state, void* scratch, const uint8_t* data, int bytes, int16_t* pcm,
                   int maxChannels, Mp3FrameInfo* info);

}  // namespace kk::audio

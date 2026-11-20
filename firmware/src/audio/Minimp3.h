#pragma once
// KaniKey's narrow interface to the vendored minimp3 decoder.
//
// The implementation is Minimp3.cpp: minimp3 (CC0, https://github.com/lieff/
// minimp3) with MINIMP3_ONLY_MP3. The decoder state and its 16 KB decode
// scratch are not static and not on the stack: the caller allocates
// mp3WorkBytes() bytes (AudioPlayer does it on R, in one heap block with the
// file window and PCM buffers) and passes that block in. The decoder is
// non-reentrant; one clip is decoded at a time from platform/AudioPlayer.
// Native builds never compile it.
#include <cstddef>
#include <cstdint>

namespace kk::audio {

// MPEG frame metadata the player needs for M5.Speaker.playRaw(). `frameBytes`
// counts the leading ID3/garbage bytes the decoder skipped plus the frame, so
// the caller advances its file window by that amount.
struct Mp3FrameInfo {
    int frameBytes = 0;
    int channels = 0;
    int hz = 0;
};

// Bytes of the work block one decoder needs (state + scratch). Constant for
// the build. The block must be at least 4-byte aligned (heap blocks are).
size_t mp3WorkBytes();

// Zero the work block before a new file. Required; a block from a previous
// clip must not be reused without it.
void mp3Reset(void* work);

// Decode one MPEG-1/2/2.5 Layer III frame from `data` into interleaved signed
// 16-bit PCM. `pcm` must hold 1152 * 2 samples (a stereo frame). Returns the
// number of samples per channel (0 on EOF, corrupt input or a non-MPEG frame);
// `info` is always filled.
int mp3DecodeFrame(void* work, const uint8_t* data, int bytes, int16_t* pcm, Mp3FrameInfo* info);

}  // namespace kk::audio

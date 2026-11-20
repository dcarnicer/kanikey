#pragma once
// KaniKey's narrow interface to the vendored minimp3 decoder.
//
// The implementation is Minimp3.cpp: minimp3 (CC0, https://github.com/lieff/
// minimp3) with MINIMP3_ONLY_MP3 and the decode scratch made static. The
// decoder is deliberately non-reentrant and one clip is decoded at a time from
// platform/AudioPlayer. Native builds never compile it.
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

// Reset the one decoder instance before starting a new file.
void mp3Reset();

// Decode one MPEG-1/2/2.5 Layer III frame from `data` into interleaved signed
// 16-bit PCM. `pcm` holds up to 1152 * 2 samples and may be null to inspect
// only the header. Returns the number of samples per channel (0 on EOF,
// corrupt input or a non-MPEG frame); `info` is always filled.
int mp3DecodeFrame(const uint8_t* data, int bytes, int16_t* pcm, Mp3FrameInfo* info);

}  // namespace kk::audio

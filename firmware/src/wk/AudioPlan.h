#pragma once
// AUD-03: pure audio playback policy. Decides which cached MP3 a subject can
// play, applies the voice fallback, and maps the result to the hint state the
// screens draw. No Arduino headers and no decoder, so the native tests cover
// selection, OGG-only/missing clips and the kanji/radical rule.
#include <cstdint>
#include <vector>

#include "wk/AudioIndex.h"
#include "wk/Types.h"

namespace kk {

enum class AudioHint : uint8_t {
    None,       // the subject type has no audio (kanji, radical)
    Available,  // R plays an MP3 for this subject
    Missing,    // the subject could have audio, but no playable clip is cached
};

struct AudioPlan {
    bool applicable = false;  // the subject type offers pronunciation audio
    bool playable = false;    // a cached MP3 was found
    AudioEntry entry;
};

// Vocabulary and kana vocabulary are the only subject types with pronunciation
// audio. Kanji and radicals never offer R.
bool audioSubjectPlayable(SubjectType type);

// Preferred voice first, then the other voice, but MP3 entries only. An OGG
// entry is cached yet treated as missing (AUD-03 is MP3 only).
AudioPlan planAudio(const std::vector<AudioEntry>& entries, SubjectType type,
                    uint32_t subjectId, uint8_t preferredVoice);

AudioHint audioHintForPlan(const AudioPlan& plan);

// "R: audio" when playable, "no audio" when missing, "" when the subject type
// has no audio at all. The screens append/drop this to keep the hint bar short.
const char* audioHintText(AudioHint hint);

// Why a clip could not start or kept failing. Only a clip that is missing,
// corrupt or not MP3 is remembered as `no audio` for its subject and voice. A
// heap refusal is transient: the clip exists, so the next R tries again.
enum class AudioFailure : uint8_t { Missing, Corrupt, NoHeap };
inline bool audioFailureMarksBad(AudioFailure failure) {
    return failure != AudioFailure::NoHeap;
}

// Heap admission for the playback pieces (AUD-03). The request is admitted only
// when the free total leaves kAudioAdmitFreeMargin beside the pieces and the
// largest free block holds the biggest piece plus kAudioAdmitLargestMargin.
// 16 KB: during a review the heap measured 50,516 B free / 27,636 B largest
// (2026-11-20) and the old 24 KB margin refused every R there. Playback does
// no TLS, and the pieces are freed about a second later.
inline constexpr size_t kAudioAdmitFreeMargin = 16 * 1024;
inline constexpr size_t kAudioAdmitLargestMargin = 4 * 1024;
bool audioAdmitsPieces(size_t freeTotal, size_t largest, const size_t* pieces, size_t count);

// M6 Autoplay audio: may a clip start on its own at this point? True only when
// M6 is On, the point is a reading page (the reading part's feedback that was
// checked Correct or Incorrect, or a lesson card with a reading line), the
// subject type has audio, a cached MP3 exists, and no sync or setup portal is
// running. The caller starts the clip once the answer cue has finished.
bool audioAutoplayWanted(bool settingOn, bool onReadingPage, SubjectType type, bool hasClip,
                         bool syncing, bool portal);

// Channels (1 or 2) of the first MPEG-1/2/2.5 Layer III frame header in
// p[0..n), after an optional leading ID3v2 tag. 0 when no such header is in
// the window. The checks are the ones that matter here: sync, a non-reserved
// version, Layer III, a valid bitrate and sample-rate index.
int mp3FirstFrameChannels(const uint8_t* p, size_t n);

// AUD-02 download choice: the preferred voice's MP3 clip first, then the other
// voice's MP3 clip. Only an explicit audio/mpeg (or mp3) content type counts;
// OGG and unknown types are never downloaded. False when there is no MP3 clip.
bool audioPickDownload(const std::vector<AudioClip>& clips, uint8_t preferredVoice,
                       size_t* index);

}  // namespace kk

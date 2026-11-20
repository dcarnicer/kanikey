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

}  // namespace kk

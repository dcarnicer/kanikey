#include "wk/AudioPlan.h"

#include <initializer_list>
#include <string>

namespace kk {

bool audioSubjectPlayable(SubjectType type) {
    return type == SubjectType::Vocabulary || type == SubjectType::KanaVocabulary;
}

AudioPlan planAudio(const std::vector<AudioEntry>& entries, SubjectType type,
                    uint32_t subjectId, uint8_t preferredVoice) {
    AudioPlan plan;
    if (!audioSubjectPlayable(type)) return plan;
    plan.applicable = true;
    plan.playable = audioChoosePlayableEntry(entries, subjectId, preferredVoice, &plan.entry);
    return plan;
}

AudioHint audioHintForPlan(const AudioPlan& plan) {
    if (!plan.applicable) return AudioHint::None;
    return plan.playable ? AudioHint::Available : AudioHint::Missing;
}

const char* audioHintText(AudioHint hint) {
    switch (hint) {
        case AudioHint::Available: return "R: audio";
        case AudioHint::Missing: return "no audio";
        case AudioHint::None: break;
    }
    return "";
}

bool audioAdmitsPieces(size_t freeTotal, size_t largest, const size_t* pieces, size_t count) {
    if (!pieces || count == 0) return false;
    size_t sum = 0;
    size_t biggest = 0;
    for (size_t i = 0; i < count; ++i) {
        sum += pieces[i];
        if (pieces[i] > biggest) biggest = pieces[i];
    }
    return freeTotal >= sum + kAudioAdmitFreeMargin &&
           largest >= biggest + kAudioAdmitLargestMargin;
}

bool audioAutoplayWanted(bool settingOn, bool onReadingPage, SubjectType type, bool hasClip,
                         bool syncing, bool portal) {
    return settingOn && onReadingPage && audioSubjectPlayable(type) && hasClip && !syncing &&
           !portal;
}

int mp3FirstFrameChannels(const uint8_t* p, size_t n) {
    if (!p) return 0;
    size_t start = 0;
    if (n >= 10 && p[0] == 'I' && p[1] == 'D' && p[2] == '3') {
        // ID3v2 size is a 28-bit syncsafe integer after the 10-byte header.
        const size_t size = (static_cast<size_t>(p[6] & 0x7F) << 21) |
                            (static_cast<size_t>(p[7] & 0x7F) << 14) |
                            (static_cast<size_t>(p[8] & 0x7F) << 7) |
                            static_cast<size_t>(p[9] & 0x7F);
        start = 10 + size + ((p[5] & 0x10) ? 10 : 0);  // footer flag
        if (start >= n) return 0;
    }
    for (size_t i = start; i + 4 <= n; ++i) {
        const uint8_t* h = p + i;
        if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) continue;
        const unsigned version = (h[1] >> 3) & 3;  // 1 is reserved
        const unsigned layer = (h[1] >> 1) & 3;    // 1 is Layer III
        if (version == 1 || layer != 1) continue;
        const unsigned bitrate = h[2] >> 4;       // 0 free, 15 bad
        const unsigned sampleRate = (h[2] >> 2) & 3;  // 3 reserved
        if (bitrate == 0 || bitrate == 15 || sampleRate == 3) continue;
        return (h[3] >> 6) == 3 ? 1 : 2;  // channel mode 3 is mono
    }
    return 0;
}

bool audioPickDownload(const std::vector<AudioClip>& clips, uint8_t preferredVoice,
                       size_t* index) {
    if (!index) return false;
    auto isMp3 = [](const AudioClip& clip) {
        if (clip.url.empty()) return false;
        const std::string& type = clip.contentType;
        return type.find("mpeg") != std::string::npos || type.find("mp3") != std::string::npos;
    };
    for (uint8_t voice : {preferredVoice, static_cast<uint8_t>(preferredVoice ^ 1)}) {
        for (size_t i = 0; i < clips.size(); ++i) {
            if (clips[i].voice != voice || !isMp3(clips[i])) continue;
            *index = i;
            return true;
        }
    }
    return false;
}

}  // namespace kk

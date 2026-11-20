#include "wk/AudioPlan.h"

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

}  // namespace kk

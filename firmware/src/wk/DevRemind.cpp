#include "wk/DevRemind.h"

namespace kk {

namespace {

constexpr uint32_t kMaxRemindSeconds = 86400;

}  // namespace

bool parseRemindSeconds(std::string_view text, uint32_t* seconds) {
    // Six digits cannot overflow uint32_t, and 86400 has five.
    if (text.empty() || text.size() > 6) return false;
    uint32_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<uint32_t>(c - '0');
    }
    if (value < 1 || value > kMaxRemindSeconds) return false;
    *seconds = value;
    return true;
}

bool parseCueName(std::string_view name, SoundCue* cue) {
    if (name == "chime") {
        *cue = SoundCue::ReminderChime;
    } else if (name == "correct") {
        *cue = SoundCue::AnswerCorrect;
    } else if (name == "wrong") {
        *cue = SoundCue::AnswerWrong;
    } else if (name == "end") {
        *cue = SoundCue::SessionEnd;
    } else {
        return false;
    }
    return true;
}

bool parseCueVolume(std::string_view name, int* volumeChoice) {
    if (name == "low") {
        *volumeChoice = 0;
    } else if (name == "med" || name == "medium") {
        *volumeChoice = 1;
    } else if (name == "high") {
        *volumeChoice = 2;
    } else {
        return false;
    }
    return true;
}

int64_t remindDriftSeconds(uint32_t rebuiltUtc, uint32_t expectedUtc) {
    return static_cast<int64_t>(rebuiltUtc) - static_cast<int64_t>(expectedUtc);
}

uint32_t devRemindMinDue(uint32_t minDue) { return minDue == 0 ? 1u : minDue; }

}  // namespace kk

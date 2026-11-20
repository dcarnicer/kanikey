// REM-03 sound table. Frequencies are equal-tempered Hz (A4 = 440). Rests keep
// two tones of one cue apart so a repeated pitch still reads as two sounds.
#include "wk/SoundPlan.h"

namespace kk {

namespace {

// Reminder chime: G6 twice, a bell-like "ding-ding".
constexpr SoundStep kChime[] = {
    {1568, 110},
    {0, 40},
    {1568, 110},
};

// Correct: two short tones rising a fifth (A5 to E6).
constexpr SoundStep kCorrect[] = {
    {880, 70},
    {0, 30},
    {1320, 90},
};

// Wrong: one low tone (A3).
constexpr SoundStep kWrong[] = {
    {220, 120},
};

// Session end: a three-note figure (E5, G5, B5).
constexpr SoundStep kSessionEnd[] = {
    {659, 100},
    {0, 30},
    {784, 100},
    {0, 30},
    {988, 120},
};

}  // namespace

SoundSequence soundSequence(SoundCue cue, uint32_t reviewsCompleted) {
    switch (cue) {
        case SoundCue::ReminderChime:
            return {kChime, sizeof kChime / sizeof kChime[0]};
        case SoundCue::AnswerCorrect:
            return {kCorrect, sizeof kCorrect / sizeof kCorrect[0]};
        case SoundCue::AnswerWrong:
            return {kWrong, sizeof kWrong / sizeof kWrong[0]};
        case SoundCue::SessionEnd:
            if (reviewsCompleted == 0) return {nullptr, 0};
            return {kSessionEnd, sizeof kSessionEnd / sizeof kSessionEnd[0]};
    }
    return {nullptr, 0};
}

bool soundCueForAnswer(AnswerStatus status, SoundCue* cue) {
    switch (status) {
        case AnswerStatus::Correct:
            *cue = SoundCue::AnswerCorrect;
            return true;
        case AnswerStatus::Incorrect:
            *cue = SoundCue::AnswerWrong;
            return true;
        case AnswerStatus::Warning:
            return false;
    }
    return false;
}

uint8_t soundLevelForVolume(int volumeChoice) {
    switch (volumeChoice) {
        case 0:  // Low
            return 64;
        case 2:  // High: full scale
            return 255;
        default:  // Medium, and any unknown value
            return 128;
    }
}

bool soundAllowed(bool soundsOn, bool syncing, bool portal) {
    return soundsOn && !syncing && !portal;
}

}  // namespace kk

#pragma once
// CFG-04: the key chords listed by the help overlay, one static table per
// screen or input mode. Pure data (no Arduino) so the native tests can check
// every table; the app selects one with helpScreenForState() and draws it
// through drawHelpScreen().
#include <cstddef>
#include <cstdint>

namespace kk {

struct HelpEntry {
    const char* keys;
    const char* action;
};

// Reviews and lessons have two tables: the question/card and the feedback or
// quiz frame do not offer the same keys. Both share the screen title.
enum class HelpScreen : uint8_t {
    None,
    Home,
    Settings,
    ReviewsQuestion,
    ReviewsFeedback,
    LessonCard,
    LessonQuiz,
    Outbox,
    Firmware,
};

// The overlay shows at most seven rows above the "Esc: close" line. Each row
// fits the 224 px text width in Font0 with both columns.
constexpr size_t kHelpMaxRows = 7;

inline constexpr HelpEntry kHomeHelp[] = {
    {"Enter", "Reviews"},
    {"L", "Lessons"},
    {"O", "Outbox"},
    {"S", "Sync now"},
    {"T", "Stats"},
    {"P", "Practice"},
    {"Esc", "Settings menu"},
};

inline constexpr HelpEntry kSettingsHelp[] = {
    {"Up / Down", "Move"},
    {"Enter", "Open / next value"},
    {"Left / Right", "Previous / next value"},
    {"Esc", "Back / close"},
};

inline constexpr HelpEntry kReviewQuestionHelp[] = {
    {"Enter", "Check the answer"},
    {"Backspace", "Delete a character"},
    {"Esc", "Pause"},
};

inline constexpr HelpEntry kReviewFeedbackHelp[] = {
    {"Enter", "Next item"},
    {"Tab", "Item details"},
    {"Esc", "Pause"},
};

inline constexpr HelpEntry kLessonCardHelp[] = {
    {"Right / Space", "Next page"},
    {"Left / Backspace", "Previous page"},
    {"Enter", "Next page / card"},
    {"Q", "Quiz this item"},
    {"Esc", "Back to lessons"},
};

inline constexpr HelpEntry kLessonQuizHelp[] = {
    {"Enter", "Check / next"},
    {"Backspace", "Delete a character"},
    {"Esc", "Back to the card"},
};

inline constexpr HelpEntry kOutboxHelp[] = {
    {"Up / Down", "Move"},
    {"Enter / D", "Discard selected"},
    {"Esc", "Back to Home"},
};

inline constexpr HelpEntry kFirmwareHelp[] = {
    {"Up / Down", "Move"},
    {"Enter", "Select / install"},
    {"Esc", "Back / cancel"},
};

// Every screen the overlay covers, in enum order after None.
inline constexpr HelpScreen kHelpScreens[] = {
    HelpScreen::Home,            HelpScreen::Settings,
    HelpScreen::ReviewsQuestion, HelpScreen::ReviewsFeedback,
    HelpScreen::LessonCard,      HelpScreen::LessonQuiz,
    HelpScreen::Outbox,          HelpScreen::Firmware,
};

struct HelpTable {
    const HelpEntry* rows = nullptr;
    size_t count = 0;
};

constexpr HelpTable helpFor(HelpScreen screen) {
    switch (screen) {
        case HelpScreen::Home:
            return {kHomeHelp, sizeof kHomeHelp / sizeof kHomeHelp[0]};
        case HelpScreen::Settings:
            return {kSettingsHelp, sizeof kSettingsHelp / sizeof kSettingsHelp[0]};
        case HelpScreen::ReviewsQuestion:
            return {kReviewQuestionHelp,
                    sizeof kReviewQuestionHelp / sizeof kReviewQuestionHelp[0]};
        case HelpScreen::ReviewsFeedback:
            return {kReviewFeedbackHelp,
                    sizeof kReviewFeedbackHelp / sizeof kReviewFeedbackHelp[0]};
        case HelpScreen::LessonCard:
            return {kLessonCardHelp, sizeof kLessonCardHelp / sizeof kLessonCardHelp[0]};
        case HelpScreen::LessonQuiz:
            return {kLessonQuizHelp, sizeof kLessonQuizHelp / sizeof kLessonQuizHelp[0]};
        case HelpScreen::Outbox:
            return {kOutboxHelp, sizeof kOutboxHelp / sizeof kOutboxHelp[0]};
        case HelpScreen::Firmware:
            return {kFirmwareHelp, sizeof kFirmwareHelp / sizeof kFirmwareHelp[0]};
        case HelpScreen::None:
            break;
    }
    return {};
}

constexpr const char* helpScreenTitle(HelpScreen screen) {
    switch (screen) {
        case HelpScreen::Home: return "home";
        case HelpScreen::Settings: return "settings";
        case HelpScreen::ReviewsQuestion:
        case HelpScreen::ReviewsFeedback: return "reviews";
        case HelpScreen::LessonCard:
        case HelpScreen::LessonQuiz: return "lessons";
        case HelpScreen::Outbox: return "outbox";
        case HelpScreen::Firmware: return "firmware";
        case HelpScreen::None: break;
    }
    return "";
}

}  // namespace kk

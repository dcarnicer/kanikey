// The settings registry of record: every row the Settings menu renders, in
// section and menu order. Keys, defaults and choice values are stored in NVS,
// so they must stay stable across releases. CFG-02 owns these tables; other
// tasks add behaviour by reading the rows through wk/SettingsModel.
#include "wk/SettingsModel.h"

namespace kk {

namespace {

// Section names in menu order. The rows below use the same literals.
constexpr const char* kReviews = "Reviews";
constexpr const char* kLessons = "Lessons";
constexpr const char* kStudy = "Study";
constexpr const char* kSync = "Sync & data";
constexpr const char* kDisplay = "Display & power";
constexpr const char* kReminders = "Reminders & sound";
constexpr const char* kAccount = "Account";
constexpr const char* kSystem = "System";

constexpr const char* kSections[] = {
    kReviews, kLessons, kStudy, kSync, kDisplay, kReminders, kAccount, kSystem,
};
constexpr size_t kSectionCount = sizeof kSections / sizeof kSections[0];

// Reviews
constexpr SettingChoice kReviewBatchChoices[] = {
    {"10", 10},
    {"20", 20},
    {"30", 30},
    {"50", 50},
};

constexpr SettingChoice kReviewOrderChoices[] = {
    {"Account preference", 0},
    {"Random", 1},
    {"Lowest level first", 2},
    {"Current level first", 3},
    {"SRS ascending", 4},
    {"SRS descending", 5},
    {"Oldest available first", 6},
};

// Font0 is ASCII-only, so the UI spellings use ">" where the spec's tables use
// an arrow.
constexpr SettingChoice kTypeOrderChoices[] = {
    {"Off", 0},
    {"Radical>Kanji>Vocab", 1},
};

// R4 migrated from the old `order` key: 0 Reading first, 1 Meaning first.
// 2 is the new Random choice; `settingFirstQuestionValue()` maps old values.
constexpr SettingChoice kFirstQuestionChoices[] = {
    {"Meaning", 1},
    {"Reading", 0},
    {"Random", 2},
};

constexpr SettingChoice kOnOffChoices[] = {
    {"On", 1},
    {"Off", 0},
};

constexpr SettingChoice kBackToBackChoices[] = {
    {"On", 1},
    {"Off", 0},
};

constexpr SettingChoice kTypoChoices[] = {
    {"WaniKani", 0},
    {"Strict", 1},
};

constexpr SettingChoice kOffOnChoices[] = {
    {"Off", 0},
    {"On", 1},
};

constexpr SettingChoice kAutoAdvanceChoices[] = {
    {"Off", 0},
    {"0.5 s", 500},
    {"1 s", 1000},
    {"2 s", 2000},
};

constexpr SettingChoice kHeaderChoices[] = {
    {"SRS+accuracy", 0},
    {"Minimal", 1},
};

// 0 = at session end, N = every N items (1 is immediate).
constexpr SettingChoice kSubmitChoices[] = {
    {"At session end", 0},
    {"Every 5", 5},
    {"Immediately", 1},
};

// Lessons
constexpr SettingChoice kLessonBatchChoices[] = {
    {"3", 3},
    {"5", 5},
    {"10", 10},
};

constexpr SettingChoice kLessonOrderChoices[] = {
    {"WaniKani", 0},
    {"Radicals>Kanji>Vocab", 1},
    {"Interleave types", 2},
};

constexpr SettingChoice kQuizChoices[] = {
    {"Required", 1},
    {"Skip", 0},
};

constexpr SettingChoice kApprenticeLimitChoices[] = {
    {"Off", 0},
    {"100", 100},
    {"150", 150},
    {"200", 200},
};

constexpr SettingChoice kDueWarningChoices[] = {
    {"Off", 0},
    {"50", 50},
    {"100", 100},
    {"200", 200},
};

constexpr SettingChoice kDailyLimitChoices[] = {
    {"Off", 0},
    {"5", 5},
    {"10", 10},
    {"15", 15},
    {"20", 20},
};

// Study
constexpr SettingChoice kCriticalPercentChoices[] = {
    {"60%", 60},
    {"70%", 70},
    {"75%", 75},
    {"80%", 80},
    {"90%", 90},
};

constexpr SettingChoice kCriticalMinChoices[] = {
    {"3", 3},
    {"5", 5},
    {"10", 10},
};

constexpr SettingChoice kRecentLessonsChoices[] = {
    {"Apprentice", 0},
    {"24 h", 1},
    {"3 d", 2},
    {"7 d", 3},
};

constexpr SettingChoice kRecentMistakesChoices[] = {
    {"24 h", 86400},
    {"3 d", 259200},
    {"7 d", 604800},
};

constexpr SettingChoice kPracticeSizeChoices[] = {
    {"10", 10},
    {"20", 20},
};

// Sync & data
constexpr SettingChoice kAutoSyncChoices[] = {
    {"Quick", 1},
    {"Full", 2},
    {"Off", 0},
};

constexpr SettingChoice kFullSyncChoices[] = {
    {"1 h", 3600},
    {"6 h", 21600},
    {"24 h", 86400},
};

constexpr SettingChoice kPrefetchChoices[] = {
    {"On", 1},
    {"Off", 0},
};

// Display & power
constexpr SettingChoice kBrightnessChoices[] = {
    {"64", 64},
    {"128", 128},
    {"192", 192},
    {"255", 255},
};

constexpr SettingChoice kDimChoices[] = {
    {"off", 0},
    {"15s", 15},
    {"30s", 30},
    {"60s", 60},
};

constexpr SettingChoice kBlankChoices[] = {
    {"off", 0},
    {"30s", 30},
    {"60s", 60},
    {"2m", 120},
};

constexpr SettingChoice kSleepChoices[] = {
    {"off", 0},
    {"2m", 120},
    {"5m", 300},
    {"10m", 600},
};

constexpr SettingChoice kLightSleepChoices[] = {
    {"On", 1},
    {"Off", 0},
};

constexpr SettingChoice kReviewSleepChoices[] = {
    {"Never", 0},
    {"After 10 min", 600},
};

// Reminders & sound
constexpr SettingChoice kReminderChoices[] = {
    {"Off", 0},
    {"1+ due", 1},
    {"10+ due", 10},
    {"25+ due", 25},
    {"50+ due", 50},
};

constexpr SettingChoice kQuietHoursChoices[] = {
    {"Off", 0},
    {"22-08", 1},
    {"23-07", 2},
};

// Minutes since local midnight; 0 is off.
constexpr SettingChoice kDailyReminderChoices[] = {
    {"Off", 0},
    {"08:00", 480},
    {"12:00", 720},
    {"18:00", 1080},
    {"20:00", 1200},
};

constexpr SettingChoice kSoundsChoices[] = {
    {"Off", 0},
    {"On", 1},
};

constexpr SettingChoice kVolumeChoices[] = {
    {"Low", 0},
    {"Medium", 1},
    {"High", 2},
};

constexpr SettingChoice kAutoplayChoices[] = {
    {"Off", 0},
    {"On", 1},
};

constexpr SettingChoice kVoiceChoices[] = {
    {"Female", 0},
    {"Male", 1},
};

// The NVS namespace is "kanikey"; see platform/DeviceSettings.cpp.
constexpr SettingDef kSettings[] = {
    // Reviews
    {"R1", kReviews, "Batch size", "Reviews per session", SettingKind::Choice, "revsize",
     kReviewBatchChoices, 4, 20, false, nullptr},
    {"R2", kReviews, "Order", "How due reviews are ordered", SettingKind::Choice, "rv.order",
     kReviewOrderChoices, 7, 0, false, nullptr},
    {"R3", kReviews, "Type order", "Group items by subject type", SettingKind::Choice, "rv.types",
     kTypeOrderChoices, 2, 0, false, nullptr},
    {"R4", kReviews, "First question", "Meaning, reading or random first", SettingKind::Choice,
     "rv.first", kFirstQuestionChoices, 3, 1, false, nullptr},
    {"R5", kReviews, "Back-to-back", "Ask both parts back to back", SettingKind::Toggle, "rv.b2b",
     kBackToBackChoices, 2, 1, false, nullptr},
    {"R6", kReviews, "Typos", "Accept close answers or retry", SettingKind::Choice, "rv.typos",
     kTypoChoices, 2, 0, false, nullptr},
    {"R7", kReviews, "Wrong answer actions", "Allow override on a wrong answer", SettingKind::Toggle,
     "rv.wrong", kOffOnChoices, 2, 0, false, nullptr},
    {"R8", kReviews, "Auto-advance on correct", "Advance after a correct answer",
     SettingKind::Choice, "rv.advance", kAutoAdvanceChoices, 4, 0, false, nullptr},
    {"R9", kReviews, "Details on wrong", "Show details on a wrong answer", SettingKind::Toggle,
     "rv.details", kOffOnChoices, 2, 0, false, nullptr},
    {"R10", kReviews, "Header info", "SRS and accuracy in the header", SettingKind::Choice,
     "rv.header", kHeaderChoices, 2, 0, true, nullptr},
    {"R11", kReviews, "Prioritize critical", "Put critical items first", SettingKind::Toggle,
     "rv.critical", kOffOnChoices, 2, 0, false, nullptr},
    {"R12", kReviews, "Submit reviews", "When completed reviews are sent", SettingKind::Choice,
     "rv.submit", kSubmitChoices, 3, 0, false, nullptr},

    // Lessons
    {"L1", kLessons, "Batch size", "Lessons per batch", SettingKind::Choice, "lessonsz",
     kLessonBatchChoices, 3, 5, false, nullptr},
    {"L2", kLessons, "Order", "How the lesson batch is ordered", SettingKind::Choice, "ls.order",
     kLessonOrderChoices, 3, 0, false, nullptr},
    {"L3", kLessons, "Lesson quiz", "Quiz before starting lessons", SettingKind::Choice, "ls.quiz",
     kQuizChoices, 2, 1, false, nullptr},
    {"L4", kLessons, "Apprentice limit warning", "Warn above this Apprentice count",
     SettingKind::Choice, "ls.appr", kApprenticeLimitChoices, 4, 0, true, nullptr},
    {"L5", kLessons, "Due reviews warning", "Warn above this due review count",
     SettingKind::Choice, "ls.due", kDueWarningChoices, 4, 0, true, nullptr},
    {"L6", kLessons, "Daily lesson limit", "Max lessons started per day", SettingKind::Choice,
     "ls.daily", kDailyLimitChoices, 5, 0, true, nullptr},

    // Study
    {"S1", kStudy, "Critical below", "Accuracy below this is critical", SettingKind::Choice,
     "st.critpct", kCriticalPercentChoices, 5, 75, true, nullptr},
    {"S2", kStudy, "Critical minimum reviews", "Answers before critical counts",
     SettingKind::Choice, "st.critmin", kCriticalMinChoices, 3, 5, true, nullptr},
    {"S3", kStudy, "Recent lessons", "Window for recent lessons", SettingKind::Choice, "st.recent",
     kRecentLessonsChoices, 4, 0, true, nullptr},
    {"S4", kStudy, "Recent mistakes", "Window for recent mistakes", SettingKind::Choice,
     "st.mistakes", kRecentMistakesChoices, 3, 86400, true, nullptr},
    {"S5", kStudy, "Practice size", "Items per practice session", SettingKind::Choice,
     "st.practice", kPracticeSizeChoices, 2, 10, true, nullptr},

    // Sync & data
    {"Y1", kSync, "Auto sync on wake", "Sync when the device wakes", SettingKind::Choice,
     "sy.wake", kAutoSyncChoices, 3, 1, true, nullptr},
    {"Y2", kSync, "Full sync every", "Interval for a full sync", SettingKind::Choice, "sy.full",
     kFullSyncChoices, 3, 21600, true, nullptr},
    {"Y3", kSync, "Prefetch reviews", "Fetch the next batch early", SettingKind::Toggle,
     "sy.prefetch", kPrefetchChoices, 2, 1, true, nullptr},
    {"sync.now", kSync, "Sync now", "Fetch progress and reviews now", SettingKind::Action, nullptr,
     nullptr, 0, 0, false, "run"},
    {"sync.outbox", kSync, "Outbox", "Unsent reviews and their state", SettingKind::Action,
     nullptr, nullptr, 0, 0, false, "open"},
    {"sync.clearCache", kSync, "Clear cache", "Delete cached snapshot and subjects",
     SettingKind::Action, nullptr, nullptr, 0, 0, false, "clear"},

    // Display & power
    {"P1", kDisplay, "Brightness", "Screen brightness", SettingKind::Choice, "bright",
     kBrightnessChoices, 4, 128, true, nullptr},
    {"P2", kDisplay, "Dim after", "Dim the screen when idle", SettingKind::Choice, "dim",
     kDimChoices, 4, 30, true, nullptr},
    {"P3", kDisplay, "Blank after", "Turn the display off when idle", SettingKind::Choice, "blank",
     kBlankChoices, 4, 60, true, nullptr},
    {"P4", kDisplay, "Sleep after", "Sleep after inactivity", SettingKind::Choice, "sleep",
     kSleepChoices, 4, 120, true, nullptr},
    {"P5", kDisplay, "Light sleep", "Use light sleep when idle", SettingKind::Toggle,
     "dp.lightsleep", kLightSleepChoices, 2, 1, true, nullptr},
    {"P6", kDisplay, "Sleep during review", "Sleep during a review session", SettingKind::Choice,
     "dp.revsleep", kReviewSleepChoices, 2, 0, true, nullptr},

    // Reminders & sound
    {"M1", kReminders, "Reminder", "Remind when this many are due", SettingKind::Choice,
     "rm.remind", kReminderChoices, 5, 0, true, nullptr},
    {"M2", kReminders, "Quiet hours", "No reminders during quiet hours", SettingKind::Choice,
     "rm.quiet", kQuietHoursChoices, 3, 1, true, nullptr},
    {"M3", kReminders, "Daily reminder", "Daily reminder time", SettingKind::Choice, "rm.time",
     kDailyReminderChoices, 5, 0, true, nullptr},
    {"M4", kReminders, "Sounds", "Answer and reminder sounds", SettingKind::Toggle, "rm.sound",
     kSoundsChoices, 2, 0, true, nullptr},
    {"M5", kReminders, "Volume", "Sound volume", SettingKind::Choice, "rm.volume",
     kVolumeChoices, 3, 1, true, nullptr},
    {"M6", kReminders, "Autoplay audio", "Play audio in reviews", SettingKind::Toggle,
     "rm.autoplay", kAutoplayChoices, 2, 0, true, nullptr},
    {"M7", kReminders, "Voice", "Audio voice preference", SettingKind::Choice, "rm.voice",
     kVoiceChoices, 2, 0, true, nullptr},

    // Account
    {"account.wifi", kAccount, "Wi-Fi", "Scan and join a 2.4 GHz network", SettingKind::Action,
     nullptr, nullptr, 0, 0, false, "scan"},
    {"setup.portal", kAccount, "Setup portal", "Wi-Fi, token and time zone", SettingKind::Action,
     nullptr, nullptr, 0, 0, false, "open"},
    {"account.signOut", kAccount, "Sign out", "Clear the token and local data",
     SettingKind::Action, nullptr, nullptr, 0, 0, false, "out"},

    // System
    {"system.firmware", kSystem, "Firmware", "Update and version", SettingKind::Submenu, nullptr,
     nullptr, 0, 0, false, "open"},
    {"system.about", kSystem, "About", "Version, storage and status", SettingKind::Info, nullptr,
     nullptr, 0, 0, false, "info"},
    {"system.reset", kSystem, "Reset settings", "Restore every setting default",
     SettingKind::Action, nullptr, nullptr, 0, 0, false, "reset"},
};

constexpr size_t kSettingsCount = sizeof kSettings / sizeof kSettings[0];

}  // namespace

const SettingDef* settingsAll(size_t* count) {
    if (count) *count = kSettingsCount;
    return kSettings;
}

size_t settingsSectionCount() { return kSectionCount; }

const char* settingsSectionName(size_t index) {
    return index < kSectionCount ? kSections[index] : "";
}

}  // namespace kk

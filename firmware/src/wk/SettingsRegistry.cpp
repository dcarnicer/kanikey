// The settings registry of record for the menu: today's thirteen rows, in the
// same order the Settings screen shows them. CFG-02 extends this table; keep
// keys, defaults and choice values stable because they are stored in NVS.
#include "wk/SettingsModel.h"

namespace kk {

namespace {

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

constexpr SettingChoice kReviewSizeChoices[] = {
    {"10", 10},
    {"20", 20},
    {"30", 30},
};

constexpr SettingChoice kFirstQuestionChoices[] = {
    {"Reading first", 0},
    {"Meaning first", 1},
};

constexpr SettingChoice kLessonSizeChoices[] = {
    {"3", 3},
    {"5", 5},
    {"10", 10},
};

// The NVS namespace is "kanikey"; see platform/DeviceSettings.cpp.
constexpr SettingDef kSettings[] = {
    {"P1", "Display & power", "Brightness", "Screen brightness", SettingKind::Choice,
     "bright", kBrightnessChoices, 4, 128, true},
    {"P2", "Display & power", "Dim after", "Dim the screen when idle", SettingKind::Choice,
     "dim", kDimChoices, 4, 30, true},
    {"P3", "Display & power", "Blank after", "Turn the display off when idle",
     SettingKind::Choice, "blank", kBlankChoices, 4, 60, true},
    {"P4", "Display & power", "Sleep after", "Sleep after inactivity", SettingKind::Choice,
     "sleep", kSleepChoices, 4, 120, true},
    {"R1", "Reviews", "Batch size", "Reviews per session", SettingKind::Choice, "revsize",
     kReviewSizeChoices, 3, 20, false},
    {"R4", "Reviews", "First question", "Ask meaning or reading first", SettingKind::Toggle,
     "order", kFirstQuestionChoices, 2, 1, false},
    {"L1", "Lessons", "Batch size", "Lessons per batch", SettingKind::Choice, "lessonsz",
     kLessonSizeChoices, 3, 5, false},
    {"setup.portal", "Account", "Setup portal", "Wi-Fi, token and time zone", SettingKind::Action,
     nullptr, nullptr, 0, 0, false},
    {"sync.now", "Sync & data", "Sync now", "Fetch progress and reviews now",
     SettingKind::Action, nullptr, nullptr, 0, 0, false},
    {"system.firmware", "System", "Firmware", "Update and version", SettingKind::Submenu,
     nullptr, nullptr, 0, 0, false},
    {"system.about", "System", "About", "Version, storage and status", SettingKind::Info,
     nullptr, nullptr, 0, 0, false},
    {"sync.clearCache", "Sync & data", "Clear cache", "Delete cached snapshot and subjects",
     SettingKind::Action, nullptr, nullptr, 0, 0, false},
    {"account.signOut", "Account", "Sign out", "Clear the token and local data",
     SettingKind::Action, nullptr, nullptr, 0, 0, false},
};

constexpr size_t kSettingsCount = sizeof kSettings / sizeof kSettings[0];

}  // namespace

const SettingDef* settingsAll(size_t* count) {
    if (count) *count = kSettingsCount;
    return kSettings;
}

}  // namespace kk

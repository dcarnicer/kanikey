#pragma once
// Settings, pause, low-battery prompt, About and the firmware update UI.
#include <cstdint>

#include "app/AppState.h"

namespace kk::app {

bool lowBatteryNow();
void requestLowBatteryConfirm(int action);
void openSettings(SettingsOrigin origin);
void closeSettings();
// Selects one registry entry by id (section + row), for the Firmware screen's
// Esc return and the serial back paths.
void settingsSelectEntry(const char* id);
void openPause();
void settingsKey(const tgc::KeyEvent& e);
void pauseKey(const tgc::KeyEvent& e);
void drawPause();
void drawSettings();
void drawLowBattery();
void drawSignOutConfirm();
void drawAbout();
void firmwareNote(const char* text, uint16_t color);
void openFirmware();
void drawFirmware();
void firmwareKey(const tgc::KeyEvent& e);
void firmwareBoot();
void firmwareTick(uint32_t now);
void firmwareBeforeSleep();
void firmwareCheck(bool manual);
void firmwareAfterSync();
void startFirmwareInstall();

}  // namespace kk::app

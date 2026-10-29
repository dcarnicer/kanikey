#pragma once
// Modal screens: Pause, low battery and sign-out confirmation, drawn from
// explicit view data. The app adapter fills the views from its state and keeps
// startWrite()/present().
#include <cstdint>

#include "board/Board.h"
#include "ui/Theme.h"

namespace kk::app {

struct PauseView {
    int selectedIndex = 0;
    unsigned completed = 0;
    unsigned total = 0;
};

struct LowBatteryView {
    int batteryPercent = 0;
    bool lessonAction = false;
};

// Draws the Pause frame into d. The caller owns startWrite()/endWrite() and
// present(); this function only draws.
void drawPauseScreen(lgfx::LovyanGFX& d, const PauseView& view);

// Draws the low battery prompt into d.
void drawLowBatteryScreen(lgfx::LovyanGFX& d, const LowBatteryView& view);

// Draws the sign-out confirmation into d.
void drawSignOutScreen(lgfx::LovyanGFX& d);

}  // namespace kk::app

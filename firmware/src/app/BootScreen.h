#pragma once
// Boot screen: the entire Boot frame, drawn from explicit view data. The app
// adapter picks the BootState text and keeps startWrite()/present().
#include <cstdint>

#include "board/Board.h"
#include "ui/Theme.h"

namespace kk::app {

struct BootView {
    int batteryPercent = -1;  // -1 draws USB instead of a percentage
    const char* title = "";
    const char* line1 = "";
    const char* line2 = "";
    const char* action = "";
    bool warn = false;
};

// Draws the Boot frame into d. The caller owns startWrite()/endWrite() and
// present(); this function only draws.
void drawBootScreen(lgfx::LovyanGFX& d, const BootView& view);

}  // namespace kk::app

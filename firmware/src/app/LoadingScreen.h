#pragma once
// Loading screen: the branded wait frame, drawn from explicit view data. The
// app adapter supplies the title/subtitle and keeps startWrite()/present().
#include "board/Board.h"
#include "ui/Theme.h"

namespace kk::app {

struct LoadingView {
    const char* title = "";
    const char* subtitle = "";
};

// Draws the Loading frame into d. The caller owns startWrite()/endWrite() and
// present(); this function only draws.
void drawLoadingScreen(lgfx::LovyanGFX& d, const LoadingView& view);

}  // namespace kk::app

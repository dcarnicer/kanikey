#pragma once
// About screen: version and diagnostic lines, drawn from explicit view data.
// The app adapter builds the lines and keeps startWrite()/present().
#include <cstdint>
#include <string>
#include <vector>

#include "board/Board.h"
#include "ui/Theme.h"

namespace kk::app {

struct AboutLine {
    std::string text;
    uint16_t color = ui::kDim;
    int y = 0;
};

struct AboutView {
    const char* version = "";
    std::vector<AboutLine> lines;
};

// Draws the About frame into d. The caller owns startWrite()/endWrite() and
// present(); this function only draws.
void drawAboutScreen(lgfx::LovyanGFX& d, const AboutView& view);

}  // namespace kk::app

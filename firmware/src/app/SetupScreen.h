#pragma once
// Setup portal screen: the entire setup frame, drawn from explicit view data.
// The app adapter fills SetupView from its state and keeps
// startWrite()/present().
#include <string>

#include "board/Board.h"
#include "ui/Theme.h"

namespace kk::app {

struct SetupView {
    int batteryPercent = -1;  // -1 draws USB instead of a percentage
    bool apMode = true;
    std::string ssid;
    std::string apPassword;
    std::string url;
    bool tokenValidated = false;
    std::string tokenUser;
    int tokenLevel = 0;
    bool tokenConfigured = false;
    std::string message;
    std::string packStatus;
};

// Draws the setup portal frame into d. The caller owns startWrite()/endWrite()
// and present(); this function only draws.
void drawSetupScreen(lgfx::LovyanGFX& d, const SetupView& view);

}  // namespace kk::app

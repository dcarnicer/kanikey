#pragma once
// On-device Wi-Fi setup screen (SETUP-09): the entire frame drawn from
// explicit view data. The app adapter fills WifiSetupView from its state and
// keeps startWrite()/present().
#include <cstdint>
#include <string>
#include <vector>

#include "board/Board.h"
#include "ui/Theme.h"

namespace kk::app {

enum class WifiSetupPanel : uint8_t { List, ManualSsid, Password };

struct WifiListRowView {
    std::string ssid;
    std::string signal;
    std::string security;
    bool manual = false;
};

struct WifiSetupView {
    WifiSetupPanel panel = WifiSetupPanel::List;
    std::vector<WifiListRowView> rows;
    int selectedIndex = 0;
    std::string ssid;   // password panel: the chosen network
    std::string text;   // manual SSID, or the already-masked password
    bool showPassword = false;
    std::string note;
    uint16_t noteColor = ui::kFaint;
    std::string help;
};

// Draws the Wi-Fi setup frame into d. The caller owns startWrite()/endWrite()
// and present(); this function only draws.
void drawWifiSetupScreen(lgfx::LovyanGFX& d, const WifiSetupView& view);

}  // namespace kk::app

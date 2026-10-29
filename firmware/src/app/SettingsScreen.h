#pragma once
// Settings screen: the entire Settings frame, drawn from explicit view data.
// The app adapter fills SettingsView from its state and keeps
// startWrite()/present().
#include <cstdint>
#include <string>
#include <vector>

#include "board/Board.h"
#include "ui/Theme.h"

namespace kk::app {

struct SettingsRowView {
    std::string label;
    std::string value;
};

struct SettingsView {
    std::vector<SettingsRowView> rows;
    int selectedIndex = 0;
    std::string note;
    uint16_t noteColor = ui::kFaint;
};

// Draws the Settings frame into d. The caller owns startWrite()/endWrite() and
// present(); this function only draws.
void drawSettingsScreen(lgfx::LovyanGFX& d, const SettingsView& view);

}  // namespace kk::app

#pragma once
// Firmware update screen: running version and slot, the last check/install
// status, the actions (Install / Check now) and the download progress. Pure
// view code; the app adapter owns the network, Update and the transfer window.
#include <cstdint>
#include <string>
#include <vector>

#include "board/Board.h"
#include "ui/Theme.h"

namespace kk::app {

struct FirmwareRowView {
    std::string label;
    std::string value;
};

struct FirmwareView {
    std::string version;
    std::string env;
    std::string slot;
    std::string status;
    uint16_t statusColor = ui::kDim;
    std::vector<FirmwareRowView> rows;
    int selectedIndex = 0;
    std::string note;
    uint16_t noteColor = ui::kFaint;
    int progress = -1;  // -1 hides the bar, 0..100 shows it
    bool busy = false;
};

void drawFirmwareScreen(lgfx::LovyanGFX& d, const FirmwareView& view);

}  // namespace kk::app

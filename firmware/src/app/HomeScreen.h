#pragma once
// Home screen: the entire Home frame, drawn from explicit view data. The app
// adapter fills HomeView from its state and keeps startWrite()/present().
#include <cstdint>

#include "board/Board.h"
#include "ui/Theme.h"
#include "wk/SrsCounts.h"

namespace kk::app {

struct HomeView {
    int batteryPercent = -1;  // -1 draws USB instead of a percentage
    bool signedIn = false;
    int level = 0;
    const char* username = "";  // ignored when signedIn is false
    uint32_t dueReviews = 0;
    uint32_t lessons = 0;
    const char* nextReview = "--:--";
    const char* syncClock = "--:--";
    bool offline = false;
    kk::SrsCounts srs;
    // Status line at y=117. Empty when the app has no status to show; the
    // level progress line then takes its place.
    char statusText[40] = "";
    uint16_t statusColor = ui::kFaint;
    char levelLine[40] = "";
    bool levelUp = false;  // statusText is the level-up notice
    uint32_t outboxAttention = 0;
    uint32_t outboxPending = 0;
};

// Draws the Home frame into d. The caller owns startWrite()/endWrite() and
// present(); this function only draws.
void drawHomeScreen(lgfx::LovyanGFX& d, const HomeView& view);

}  // namespace kk::app

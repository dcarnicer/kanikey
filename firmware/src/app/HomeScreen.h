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
    // REM-02: a reminder wake shows the banner in the next-review row and the
    // hint "any key: continue" until the 30 s window ends.
    bool reminderActive = false;
    char reminder[24] = "";
    const char* syncClock = "--:--";
    // Non-zero while an automatic sync runs: the header shows `sync n/m`
    // instead of the last successful clock (SPEED-03).
    uint8_t syncStep = 0;
    uint8_t syncTotal = 0;
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
    bool outboxNearlyFull = false;  // 96 of 128 slots used
    // PD-03: a two-button board draws the focus strip (Lessons, Stats, Sync,
    // Settings) in place of the status line and names buttons in the hint. The
    // status text then moves to the header's right side.
    bool buttonMenu = false;
    uint8_t focus = 0;
    // PD-04: the sleeping frame drawn before a deep sleep. On a two-button board
    // the hint then names the buttons that wake the device.
    bool asleep = false;
};

// Draws the Home frame into d. The caller owns startWrite()/endWrite() and
// present(); this function only draws.
void drawHomeScreen(lgfx::LovyanGFX& d, const HomeView& view);

}  // namespace kk::app

#pragma once
// PD-02: e-paper present and refresh policy for the PaperDIY 4x path.
//
// The app marks the canvas dirty whenever a screen draws, and asks tick() once
// per loop pass whether to push a frame and in which EPD mode. Pushing a 4x
// frame is a full-screen update on a 1-4 s panel, so presents are coalesced:
// a frame goes out once the screen has been still for `settleMs`, at most once
// per `floorMs`, and never while the panel is busy. A steady stream of changes
// still shows up after `maxDeferMs`. Every `fullEvery` partial refreshes the
// next frame is a full (quality) refresh to clear ghosting. A clock or battery
// tick that changes nothing never marks the canvas dirty, so it never refreshes.
//
// Pure C++: no Arduino, no LovyanGFX, no allocation. Native-tested in
// test/test_wk/test_epd.cpp. The timing numbers are proposals from
// docs/spec/10-bluetooth-paperdiy.md §2.3 and are not hardware measurements.
#include <cstdint>

namespace kk::epd {

// The two EPD modes the policy picks between. The faster epd_fast/epd_fastest
// modes are not chosen until a hardware run shows their ghosting is acceptable.
enum class Mode : uint8_t { Quality, Text };

struct Config {
    uint32_t settleMs = 150;     // push after this long without a new change
    uint32_t floorMs = 500;      // at most one push per interval
    uint32_t maxDeferMs = 1000;  // a steady stream of changes still presents
    uint32_t fullEvery = 10;     // a full refresh after this many partials
};

struct Presenter {
    bool pending = false;       // the canvas changed since the last push
    bool presentedOnce = false; // the first frame is always pushed at once
    uint32_t pendingSinceMs = 0;
    uint32_t lastDirtyMs = 0;
    uint32_t lastPresentMs = 0;
    uint32_t partials = 0;      // partial refreshes since the last full one
};

enum class Action : uint8_t { Wait, Present };

struct Decision {
    Action action = Action::Wait;
    Mode mode = Mode::Text;
    bool full = false;          // a full refresh (quality) rather than partial
};

// A screen changed the canvas at `now` (millis()).
void markDirty(Presenter* p, uint32_t now);

// Whether to push now. `busy` is displayBusy(): nothing is pushed while the
// panel is still refreshing, whatever the timers say. `force` pushes even with
// nothing pending and skips the settle/floor/defer timers; the caller waits for
// the panel first, so pass busy=false then.
Decision decide(const Presenter& p, uint32_t now, bool busy, const Config& cfg,
                bool force = false);

// Record that `d` was pushed at `now`.
void recordPresent(Presenter* p, const Decision& d, uint32_t now);

}  // namespace kk::epd

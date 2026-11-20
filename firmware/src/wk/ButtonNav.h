// From Tamagotcho (MIT, Copyright (c) 2026 Tamagotcho contributors).
#pragma once
// PD-03: the two-button mapping of a board whose only input is A and B
// (PaperDIY). Pure C++, no Arduino (native-tested in test_wk); the board reads
// the pins and feeds the level of each button into ButtonNav::update() once per
// loop, and the key it returns goes into the same queue as a keyboard key.
//
//   A tap  -> Down   (Next: the next row, option or page)
//   A hold -> Enter  (Confirm: opens, starts or chooses; sent once, at kHoldMs)
//   B tap  -> Esc    (Back: up one level, closes, cancels)
//   B hold -> Up     (Prev: the previous row or option)
//
// Why this way: the frequent step is a tap of A (the same finger for lists),
// Confirm needs the deliberate hold so scrolling does not open items, and Back
// is the quick tap of B, the common exit. Holds send one key and never repeat,
// so a long press cannot run a list away; a press of both buttons together
// sends nothing, so an accidental squeeze is harmless.
#include <cstdint>

#include "ui/KeyInput.h"

namespace tgc {

// Hold threshold for the long action, in ms. The M5 button flags use the same
// value (board/Board.h kHoldMs).
constexpr uint32_t kButtonHoldMs = 600;

enum class NavButton : uint8_t { A, B };

constexpr Key navShortKey(NavButton b) { return b == NavButton::A ? Key::Down : Key::Esc; }
constexpr Key navLongKey(NavButton b) { return b == NavButton::A ? Key::Enter : Key::Up; }

class ButtonNav {
public:
    // Feeds the level of `b` (true = down) at `now` (ms). Returns the key to
    // send this call, or Key::None. A tap is sent on release, a hold once when
    // kButtonHoldMs has passed while the button is still down.
    Key update(NavButton b, bool down, uint32_t now);

    // True while a chord (both buttons down together) suppresses the keys.
    bool chord() const { return chord_; }

private:
    struct Side {
        bool down = false;
        bool longSent = false;
        uint32_t since = 0;
    };
    static int index(NavButton b) { return b == NavButton::A ? 0 : 1; }

    Side side_[2];
    bool chord_ = false;
};

}  // namespace tgc

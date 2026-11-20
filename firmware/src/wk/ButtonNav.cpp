// From Tamagotcho (MIT, Copyright (c) 2026 Tamagotcho contributors).
#include "wk/ButtonNav.h"

namespace tgc {

Key ButtonNav::update(NavButton b, bool down, uint32_t now) {
    Side& s = side_[index(b)];
    const Side& other = side_[index(b == NavButton::A ? NavButton::B : NavButton::A)];

    if (down && !s.down) {  // press edge
        s.down = true;
        s.longSent = false;
        s.since = now;
        if (other.down) chord_ = true;
        return Key::None;
    }

    if (!down && s.down) {  // release edge
        s.down = false;
        const bool tap = !s.longSent;
        s.longSent = false;
        const bool suppressed = chord_;
        if (!side_[0].down && !side_[1].down) chord_ = false;
        if (tap && !suppressed) return navShortKey(b);
        return Key::None;
    }

    if (down && s.down && !s.longSent && !chord_ && now - s.since >= kButtonHoldMs) {
        s.longSent = true;
        return navLongKey(b);
    }
    return Key::None;
}

}  // namespace tgc

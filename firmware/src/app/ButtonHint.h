// From Tamagotcho (MIT, Copyright (c) 2026 Tamagotcho contributors).
#pragma once
// PD-03: the hint for the current board. A keyboard board names keys ("Enter:
// sync  Esc: home"); a two-button board names the buttons ("hold A: sync  B:
// home"). The caller passes both strings; the board decides which is shown.
#include "board/Board.h"

namespace kk::app {

inline const char* pickHint(const char* keys, const char* buttons) {
    return tgc::board::buttonsOnly() ? buttons : keys;
}

}  // namespace kk::app

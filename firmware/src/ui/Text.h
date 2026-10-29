#pragma once
// Shared text drawing: word-wrapped lines in the UI font (8 px line pitch).
#include <cstdint>
#include <string>

#include "board/Board.h"

namespace kk::ui {

void drawWrapped(lgfx::LovyanGFX& d, const std::string& text, int x, int y, int w, int maxLines,
                 uint16_t fg, uint16_t bg);

}  // namespace kk::ui

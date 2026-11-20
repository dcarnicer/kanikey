#pragma once
// Shared text drawing: word-wrapped lines in the UI font (8 px line pitch).
#include <cstdint>
#include <string>
#include <vector>

#include "board/Board.h"

namespace kk::ui {

// Word-wrapped Font0 text at 8 px line pitch. When `ellipsis` is set and the
// text still overflows maxLines, the last line is shortened and ends with
// "...". The feedback body (ui::drawFeedbackBody) does not use this: it never
// cuts a line; this serves the screens with a fixed line budget.
void drawWrapped(lgfx::LovyanGFX& d, const std::string& text, int x, int y, int w, int maxLines,
                 uint16_t fg, uint16_t bg, bool ellipsis = false);

}  // namespace kk::ui

#pragma once
// Shared text drawing: word-wrapped lines in the UI font (8 px line pitch).
#include <cstdint>
#include <string>
#include <vector>

#include "board/Board.h"

namespace kk::ui {

// Word-wrapped Font0 text at 8 px line pitch. When `ellipsis` is set and the
// text still overflows maxLines, the last line is shortened and ends with
// "...".
void drawWrapped(lgfx::LovyanGFX& d, const std::string& text, int x, int y, int w, int maxLines,
                 uint16_t fg, uint16_t bg, bool ellipsis = false);

// Accepted answers joined with " / ", followed by "  + <synonyms>" in the
// synonym colour. Font0, wrapped to maxLines lines at 8 px pitch; the last line
// ends with "..." when tokens are dropped.
void drawAnswerTokens(lgfx::LovyanGFX& d, const std::vector<std::string>& answers,
                      const std::vector<std::string>& synonyms, int x, int y, int w,
                      int maxLines, uint16_t fg, uint16_t synonymFg, uint16_t bg);

}  // namespace kk::ui

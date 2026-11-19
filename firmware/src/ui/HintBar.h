#pragma once
// Shared hint bar at y=125 and a preferred-first width fallback. Font0, the
// same left/right alignment and the kFaint color the hand-drawn screens use.
#include <initializer_list>

#include "ui/Theme.h"

namespace lgfx {
inline namespace v1 {
class LovyanGFX;
}
}

namespace kk::ui {

constexpr int kHintY = 125;
constexpr int kHintLeftX = 8;
constexpr int kHintRightX = 232;
constexpr int kHintWidth = kHintRightX - kHintLeftX;  // 224 px

// The first width that fits maxWidth, else the last one (the shortest fallback
// when the list is ordered preferred-first). -1 for an empty list.
inline int fitHintIndex(const int* widths, int count, int maxWidth) {
    if (!widths || count <= 0) return -1;
    for (int i = 0; i < count; ++i)
        if (widths[i] <= maxWidth) return i;
    return count - 1;
}

// Picks the first hint that fits maxWidth, or the last hint when none fits.
// The returned pointer points into `hints`; the list is ordered longest first.
const char* fitHint(lgfx::LovyanGFX& d, std::initializer_list<const char*> hints,
                    int maxWidth = kHintWidth);

// One hint. Left aligned at (x, y), right aligned at (rightX, y). Empty/null
// text is skipped. The datum is left at top-left.
void drawHintLeft(lgfx::LovyanGFX& d, const char* text, uint16_t color = kFaint,
                  int x = kHintLeftX, int y = kHintY);
void drawHintRight(lgfx::LovyanGFX& d, const char* text, uint16_t color = kFaint,
                   int rightX = kHintRightX, int y = kHintY);

// Both sides on the same line. Use fitHint() first for the fallback variants.
void drawHints(lgfx::LovyanGFX& d, const char* left, const char* right,
               uint16_t color = kFaint, int y = kHintY);

}  // namespace kk::ui

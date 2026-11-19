#pragma once
// Reusable list window math and a seven-row drawing frame. The math is pure
// (native-tested); the draw helper only draws the focus bar, the row callback
// and the scroll thumb, so a screen keeps its own fonts and row content.
#include <algorithm>
#include <cstdint>

#include "ui/Theme.h"

namespace lgfx {
inline namespace v1 {
class LovyanGFX;
}
}

namespace kk::ui {

// One screenful of a list. `visible` is how many rows fit, `first` is the top
// row to draw and `rows` is how many rows are actually drawn (less than
// `visible` only when the whole list is shorter). `selected` is always inside
// the list for a non-empty count.
struct ListWindow {
    int count = 0;
    int visible = 0;
    int selected = 0;
    int first = 0;
    int rows = 0;
};

// Clamp or wrap `selected`, centre the window on it and clamp `first` to the
// last full page. `wrap` only changes how an out-of-range selection is
// normalised; the window itself still stops at the first and last row.
inline ListWindow listWindow(int count, int selected, int visible, bool wrap = false) {
    ListWindow window;
    window.count = count > 0 ? count : 0;
    window.visible = visible > 0 ? visible : 0;
    if (window.count == 0) return window;

    if (wrap) {
        window.selected = selected % window.count;
        if (window.selected < 0) window.selected += window.count;
    } else {
        window.selected =
            selected < 0 ? 0 : selected >= window.count ? window.count - 1 : selected;
    }
    if (window.visible == 0) return window;

    window.first = window.selected - window.visible / 2;
    const int maxFirst = std::max(0, window.count - window.visible);
    if (window.first < 0) window.first = 0;
    if (window.first > maxFirst) window.first = maxFirst;
    window.rows = std::min(window.visible, window.count - window.first);
    return window;
}

// Move the selection by `delta` rows. Without wrap the selection stops at the
// first and last row; with wrap it cycles. An empty list keeps the index 0 and
// a stale index is normalised first.
inline int listMoveSelection(int selected, int count, int delta, bool wrap = false) {
    if (count <= 0) return 0;
    int current = selected < 0 ? 0 : selected >= count ? count - 1 : selected;
    if (!wrap) {
        const int next = current + delta;
        return next < 0 ? 0 : next >= count ? count - 1 : next;
    }
    int next = (current + delta) % count;
    if (next < 0) next += count;
    return next;
}

// Default geometry: the seven-row Settings/Wi-Fi frame (240x135, Font0).
struct ListStyle {
    int x = 4;
    int y = 42;
    int width = 228;
    int rowHeight = 11;
    int trackX = 235;
    int trackHeight = 66;
    uint16_t focusBg = kSurfaceHi;
    uint16_t focusBar = kAccent;
    uint16_t track = kLine;
    uint16_t thumb = kAccentHi;
};

// Draws one row's content at (x, y); `focus` is true for the selected row.
using ListRowFn = void (*)(lgfx::LovyanGFX& d, int index, int x, int y, int width,
                           bool focus, const void* ctx);

// Draws the visible rows of `window`: the focus background/bar, then `row` for
// each row, then the scrollbar when the list is longer than one page. The
// caller chooses the font/colors inside the callback; this helper only uses
// the palette in ListStyle and leaves the text datum at top-left.
void drawList(lgfx::LovyanGFX& d, const ListWindow& window, ListRowFn row,
              const void* ctx, const ListStyle& style = ListStyle());

}  // namespace kk::ui

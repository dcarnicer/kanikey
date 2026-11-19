// Reusable list frame. The caller owns startWrite()/present() and draws the
// row text in the callback.
#include "ui/ListView.h"

#include <algorithm>

#include "board/Board.h"

namespace kk::ui {

void drawList(lgfx::LovyanGFX& d, const ListWindow& window, ListRowFn row,
              const void* ctx, const ListStyle& style) {
    for (int r = 0; r < window.rows; ++r) {
        const int index = window.first + r;
        const int y = style.y + r * style.rowHeight;
        const bool focus = index == window.selected;
        if (focus) {
            d.fillRect(style.x, y - 2, style.width, style.rowHeight, style.focusBg);
            d.fillRect(style.x, y - 2, 2, style.rowHeight, style.focusBar);
        }
        d.setTextDatum(lgfx::top_left);
        if (row) {
            const int textX = style.x + style.textInset;
            row(d, index, textX, y, style.width - style.textInset, focus, ctx);
        }
    }
    d.setTextDatum(lgfx::top_left);

    if (window.count <= window.visible || window.visible <= 0) return;
    const int trackY = style.y;
    d.fillRect(style.trackX, trackY, 2, style.trackHeight, style.track);
    const int thumbH =
        std::min(style.trackHeight,
                 std::max(10, style.trackHeight * window.visible / window.count));
    const int maxFirst = window.count - window.visible;
    const int thumbY = maxFirst > 0
                           ? (style.trackHeight - thumbH) * window.first / maxFirst
                           : 0;
    d.fillRect(style.trackX, trackY + thumbY, 2, thumbH, style.thumb);
}

}  // namespace kk::ui

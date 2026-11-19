// 1bpp icon drawing. The tables are header-only so the native tests can read
// them; only this file needs the display API.
#include "ui/Icons.h"

#include "board/Board.h"

namespace kk::ui {

void drawIcon(lgfx::LovyanGFX& d, const Icon& icon, int x, int y, uint16_t fg,
              uint8_t scale) {
    if (!icon.bits || icon.width == 0 || icon.height == 0 || scale == 0) return;
    for (int row = 0; row < icon.height; ++row) {
        int run = -1;
        for (int col = 0; col <= icon.width; ++col) {
            const bool on = col < icon.width && iconPixel(icon, col, row);
            if (on) {
                if (run < 0) run = col;
            } else if (run >= 0) {
                d.fillRect(x + run * scale, y + row * scale, (col - run) * scale, scale,
                           fg);
                run = -1;
            }
        }
    }
}

void drawIconBox(lgfx::LovyanGFX& d, const Icon& icon, int x, int y, uint16_t fg,
                 uint16_t bg, uint8_t scale) {
    if (!icon.bits || icon.width == 0 || icon.height == 0 || scale == 0) return;
    d.fillRect(x, y, icon.width * scale, icon.height * scale, bg);
    drawIcon(d, icon, x, y, fg, scale);
}

}  // namespace kk::ui

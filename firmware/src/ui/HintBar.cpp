// Shared hint bar drawing. The caller owns startWrite()/present().
#include "ui/HintBar.h"

#include "board/Board.h"

namespace kk::ui {

const char* fitHint(lgfx::LovyanGFX& d, std::initializer_list<const char*> hints,
                    int maxWidth) {
    d.setTextFont(0);
    d.setTextSize(1);
    for (const char* hint : hints) {
        if (!hint || !*hint) continue;
        if (d.textWidth(hint) <= maxWidth) return hint;
    }
    const char* last = nullptr;
    for (const char* hint : hints)
        if (hint && *hint) last = hint;
    return last;
}

void drawHintLeft(lgfx::LovyanGFX& d, const char* text, uint16_t color, int x, int y) {
    if (!text || !*text) return;
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextDatum(lgfx::top_left);
    d.setTextColor(color, kBg);
    d.drawString(text, x, y);
}

void drawHintRight(lgfx::LovyanGFX& d, const char* text, uint16_t color, int rightX, int y) {
    if (!text || !*text) return;
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(color, kBg);
    d.drawString(text, rightX, y);
    d.setTextDatum(lgfx::top_left);
}

void drawHints(lgfx::LovyanGFX& d, const char* left, const char* right, uint16_t color,
               int y) {
    drawHintLeft(d, left, color, kHintLeftX, y);
    drawHintRight(d, right, color, kHintRightX, y);
}

}  // namespace kk::ui

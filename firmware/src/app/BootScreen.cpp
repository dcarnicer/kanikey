// Boot screen drawing. Pure view code: no app globals, no present(), no
// startWrite()/endWrite() (the caller owns the transfer window).
#include "app/BootScreen.h"

#include <cstdio>
#include <string>

namespace kk::app {
namespace {

void drawBootLine(lgfx::LovyanGFX& d, const char* text, int x, int y, uint16_t fg) {
    if (!text || !text[0]) return;
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(fg, ui::kBg);
    std::string s = text;
    while (s.size() > 1 && d.textWidth(s.c_str()) > 224) s.pop_back();
    d.drawString(s.c_str(), x, y);
}

}  // namespace

void drawBootScreen(lgfx::LovyanGFX& d, const BootView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);

    char right[24];
    if (view.batteryPercent >= 0)
        snprintf(right, sizeof right, "%d%%", view.batteryPercent);
    else
        snprintf(right, sizeof right, "USB");
    d.setTextFont(1);
    d.setTextColor(view.batteryPercent >= 0 && view.batteryPercent < 20 ? ui::kWarn : ui::kDim,
                   ui::kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString(right, 233, 5);
    d.setTextDatum(lgfx::top_left);

    d.setTextFont(4);
    d.setTextSize(1);
    d.setTextColor(view.warn ? ui::kWarn : ui::kText, ui::kBg);
    d.drawString(view.title, 8, 38);
    drawBootLine(d, view.line1, 8, 82, ui::kDim);
    drawBootLine(d, view.line2, 8, 96, ui::kDim);
    if (view.action[0]) {
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(ui::kFaint, ui::kBg);
        d.drawString(view.action, 8, 120);
    }
}

}  // namespace kk::app

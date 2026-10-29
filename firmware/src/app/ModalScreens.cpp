// Modal screen drawing. Pure view code: no app globals, no present(), no
// startWrite()/endWrite() (the caller owns the transfer window).
#include "app/ModalScreens.h"

#include <cstdio>

namespace kk::app {

void drawPauseScreen(lgfx::LovyanGFX& d, const PauseView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("paused", 7, 22);

    char progress[24];
    snprintf(progress, sizeof progress, "%u/%u", view.completed, view.total);
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString(progress, 232, 5);
    d.setTextDatum(lgfx::top_left);

    static const char* kRows[] = {"Continue", "End session", "Settings"};
    for (int i = 0; i < 3; ++i) {
        const int y = 40 + i * 20;
        if (i == view.selectedIndex) {
            d.fillRect(4, y - 2, 232, 16, ui::kSurfaceHi);
            d.fillRect(4, y - 2, 2, 16, ui::kAccent);
        }
        d.setTextFont(1);
        d.setTextSize(1);
        d.setTextColor(i == view.selectedIndex ? ui::kText : ui::kDim,
                       i == view.selectedIndex ? ui::kSurfaceHi : ui::kBg);
        d.drawString(kRows[i], 12, y);
    }
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("Enter: select  Esc: continue", 8, 125);
}

void drawLowBatteryScreen(lgfx::LovyanGFX& d, const LowBatteryView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextColor(ui::kWarn, ui::kBg);
    d.drawString("battery low", 7, 22);

    char pct[16];
    snprintf(pct, sizeof pct, "%d%%", view.batteryPercent >= 0 ? view.batteryPercent : 0);
    d.setTextFont(4);
    d.setTextSize(1);
    d.setTextColor(ui::kWarn, ui::kBg);
    d.setTextDatum(lgfx::middle_center);
    d.drawString(pct, 120, 62);
    d.setTextDatum(lgfx::top_left);

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ui::kText, ui::kBg);
    d.drawString(view.lessonAction ? "A lesson batch may not finish."
                                   : "A review session may not finish.",
                 8, 86);
    d.setTextFont(0);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("Connect power or continue anyway.", 8, 102);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("Enter: continue  Esc: cancel", 8, 125);
}

void drawSignOutScreen(lgfx::LovyanGFX& d) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextColor(ui::kErr, ui::kBg);
    d.drawString("sign out", 7, 22);
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ui::kText, ui::kBg);
    d.drawString("Token, cache and", 8, 46);
    d.drawString("pending reviews will", 8, 60);
    d.drawString("be removed.", 8, 74);
    d.setTextFont(0);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("Wi-Fi stays saved.", 8, 92);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("Enter: sign out", 8, 112);
    d.drawString("Esc: cancel", 8, 125);
}

}  // namespace kk::app

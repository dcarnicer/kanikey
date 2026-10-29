// Setup portal screen drawing. Pure view code: no app globals, no present(),
// no startWrite()/endWrite() (the caller owns the transfer window).
#include "app/SetupScreen.h"

#include <cstdio>

namespace kk::app {
namespace {

void drawLine(lgfx::LovyanGFX& d, std::string text, int y, uint16_t fg,
              uint8_t font = 1) {
    d.setTextFont(font);
    d.setTextSize(1);
    d.setTextColor(fg, ui::kBg);
    while (text.size() > 1 && d.textWidth(text.c_str()) > 228) text.pop_back();
    d.drawString(text.c_str(), 8, y);
}

}  // namespace

void drawSetupScreen(lgfx::LovyanGFX& d, const SetupView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString(view.apMode ? "setup" : "setup · Wi-Fi", 7, 22);

    char right[16];
    if (view.batteryPercent >= 0)
        snprintf(right, sizeof right, "%d%%", view.batteryPercent);
    else
        snprintf(right, sizeof right, "USB");
    d.setTextFont(1);
    d.setTextColor(view.batteryPercent >= 0 && view.batteryPercent < 20 ? ui::kWarn
                                                                       : ui::kDim,
                   ui::kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString(right, 233, 5);
    d.setTextDatum(lgfx::top_left);

    if (view.apMode) {
        drawLine(d, "Join this Wi-Fi", 38, ui::kDim);
        drawLine(d, view.ssid, 52, ui::kText, 2);
        drawLine(d, "Password", 74, ui::kDim);
        drawLine(d, view.apPassword, 86, ui::kText);
        drawLine(d, "Open", 100, ui::kDim);
        drawLine(d, view.url, 110, ui::kAccentHi);
        drawLine(d, "Esc: cancel", 125, ui::kFaint, 0);
    } else {
        drawLine(d, "Connected", 38, ui::kOk);
        drawLine(d, view.ssid, 52, ui::kText, 2);
        drawLine(d, "Open on this network", 74, ui::kDim);
        drawLine(d, view.url, 84, ui::kAccentHi);
        if (view.tokenValidated)
            drawLine(d,
                     "Token OK: " + view.tokenUser + " L" + std::to_string(view.tokenLevel),
                     100, ui::kOk);
        else if (view.tokenConfigured)
            drawLine(d, "Token stored: validate on the page", 100, ui::kDim);
        else
            drawLine(d, "Token needed: enter it on the page", 100, ui::kWarn);
        drawLine(d, view.message.size() ? view.message
                                        : "Pack: " + view.packStatus,
                 114, ui::kFaint, 0);
        drawLine(d, "Finish on the web page  Esc: cancel", 125, ui::kFaint, 0);
    }
}

}  // namespace kk::app

// Loading screen drawing. Pure view code: no app globals, no present(), no
// startWrite()/endWrite() (the caller owns the transfer window).
#include "app/LoadingScreen.h"

namespace kk::app {

void drawLoadingScreen(lgfx::LovyanGFX& d, const LoadingView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ui::kText, ui::kBg);
    d.drawString(view.title, 8, 56);
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString(view.subtitle, 8, 76);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("The first request can take a few seconds", 8, 98);
}

}  // namespace kk::app

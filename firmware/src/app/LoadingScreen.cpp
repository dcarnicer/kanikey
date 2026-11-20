// Loading screen drawing. Pure view code: no app globals, no present(), no
// startWrite()/endWrite() (the caller owns the transfer window).
#include "app/LoadingScreen.h"

#include "ui/UiFont.h"

namespace kk::app {

void drawLoadingScreen(lgfx::LovyanGFX& d, const LoadingView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    ui::applyFont(d, ui::UiFont::Title);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);
    ui::applyFont(d, ui::UiFont::Base);
    d.setTextColor(ui::kText, ui::kBg);
    d.drawString(view.title, 8, 56);
    ui::applyFont(d, ui::UiFont::Small);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString(view.subtitle, 8, 76);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("The first request can take a few seconds", 8, 98);
}

}  // namespace kk::app

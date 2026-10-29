// About screen drawing. Pure view code: no app globals, no present(), no
// startWrite()/endWrite() (the caller owns the transfer window).
#include "app/AboutScreen.h"

namespace kk::app {

void drawAboutScreen(lgfx::LovyanGFX& d, const AboutView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("about · unofficial", 7, 22);
    d.setTextFont(0);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString("Esc: back", 232, 5);
    d.setTextDatum(lgfx::top_left);

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ui::kText, ui::kBg);
    std::string version = "KaniKey ";
    version += view.version;
    d.drawString(version.c_str(), 7, 34);

    for (const AboutLine& line : view.lines) {
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(line.color, ui::kBg);
        std::string s = line.text;
        while (s.size() > 1 && d.textWidth(s.c_str()) > 226) s.pop_back();
        d.drawString(s.c_str(), 7, line.y);
    }
}

}  // namespace kk::app

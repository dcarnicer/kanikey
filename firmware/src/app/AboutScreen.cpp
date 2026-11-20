// About screen drawing. Pure view code: no app globals, no present(), no
// startWrite()/endWrite() (the caller owns the transfer window).
#include "app/AboutScreen.h"

#include "app/ButtonHint.h"
#include "ui/Icons.h"
#include "ui/UiFont.h"

namespace kk::app {
namespace {

// The About lines are built as text elsewhere; the icon follows the label and
// is only drawn when the value leaves room for it.
const ui::Icon* statusIconFor(const std::string& text) {
    if (text.rfind("Font: ", 0) == 0 || text.rfind("Rad: ", 0) == 0) return &ui::kIconSd;
    if (text.rfind("Token: ", 0) == 0) return &ui::kIconKey;
    return nullptr;
}

}  // namespace

void drawAboutScreen(lgfx::LovyanGFX& d, const AboutView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    ui::applyFont(d, ui::UiFont::Title);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("about · unofficial", 7, 22);
    d.setTextFont(0);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString(pickHint("Esc: back", "B: back"), 232, 5);
    d.setTextDatum(lgfx::top_left);

    ui::applyFont(d, ui::UiFont::Base);
    d.setTextColor(ui::kText, ui::kBg);
    std::string version = "KaniKey ";
    version += view.version;
    d.drawString(version.c_str(), 7, 34);

    for (const AboutLine& line : view.lines) {
        ui::applyFont(d, ui::UiFont::Small);
        d.setTextColor(line.color, ui::kBg);
        std::string s = line.text;
        while (s.size() > 1 && d.textWidth(s.c_str()) > 226) s.pop_back();
        const int valueWidth = d.textWidth(s.c_str());
        d.drawString(s.c_str(), 7, line.y);
        const ui::Icon* icon = statusIconFor(line.text);
        if (icon && 7 + valueWidth + 4 + ui::kIconWidth <= 232)
            ui::drawIcon(d, *icon, 7 + valueWidth + 4, line.y - 2, line.color);
    }
}

}  // namespace kk::app

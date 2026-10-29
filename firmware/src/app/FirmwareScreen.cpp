// Firmware update screen drawing. Pure view code: no app globals, no
// present(), no startWrite()/endWrite() (the caller owns the window).
#include "app/FirmwareScreen.h"

#include <algorithm>
#include <cstdio>

#include "ui/Text.h"

namespace kk::app {

void drawFirmwareScreen(lgfx::LovyanGFX& d, const FirmwareView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("firmware", 7, 22);
    d.setTextFont(0);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString("Esc: back", 232, 5);
    d.setTextDatum(lgfx::top_left);
    d.drawFastHLine(4, 36, 232, ui::kLine);

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ui::kText, ui::kBg);
    std::string version = view.version;
    if (!view.slot.empty()) version += "  " + view.slot;
    d.drawString(version.c_str(), 7, 41);
    if (!view.env.empty()) {
        d.setTextFont(0);
        d.setTextColor(ui::kFaint, ui::kBg);
        d.setTextDatum(lgfx::top_right);
        d.drawString(view.env.c_str(), 232, 43);
        d.setTextDatum(lgfx::top_left);
    }

    ui::drawWrapped(d, view.status, 7, 56, 226, 2, view.statusColor, ui::kBg);

    if (view.progress >= 0) {
        const int y = 76;
        d.drawRect(7, y, 226, 8, ui::kLine);
        const int fill = std::max(0, std::min(100, view.progress));
        d.fillRect(9, y + 2, 222 * fill / 100, 4, ui::kAccentHi);
        char pct[8];
        snprintf(pct, sizeof pct, "%d%%", fill);
        d.setTextFont(0);
        d.setTextColor(ui::kDim, ui::kBg);
        d.drawString(pct, 7, y + 12);
    }

    const int rowTop = view.progress >= 0 ? 94 : 80;
    for (size_t i = 0; i < view.rows.size() && i < 2; ++i) {
        const bool focus = static_cast<int>(i) == view.selectedIndex;
        const int y = rowTop + static_cast<int>(i) * 12;
        if (focus) {
            d.fillRect(4, y - 2, 228, 12, ui::kSurfaceHi);
            d.fillRect(4, y - 2, 2, 12, ui::kAccent);
        }
        d.setTextFont(0);
        d.setTextColor(focus ? ui::kText : ui::kDim, focus ? ui::kSurfaceHi : ui::kBg);
        d.drawString(view.rows[i].label.c_str(), 10, y);
        d.setTextDatum(lgfx::top_right);
        d.setTextColor(focus ? ui::kAccentHi : ui::kFaint,
                       focus ? ui::kSurfaceHi : ui::kBg);
        d.drawString(view.rows[i].value.c_str(), 228, y);
        d.setTextDatum(lgfx::top_left);
    }

    if (view.note.empty()) {
        d.setTextFont(0);
        d.setTextColor(ui::kFaint, ui::kBg);
        d.drawString("Enter: select  Esc: back", 8, 123);
    } else {
        ui::drawWrapped(d, view.note, 8, 123, 224, 1, view.noteColor, ui::kBg);
    }
}

}  // namespace kk::app

// Settings screen drawing. Pure view code: no app globals, no present(), no
// startWrite()/endWrite() (the caller owns the transfer window).
#include "app/SettingsScreen.h"

#include <algorithm>

namespace kk::app {

void drawSettingsScreen(lgfx::LovyanGFX& d, const SettingsView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("settings", 7, 22);
    d.setTextFont(0);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString("Esc: back", 232, 5);
    d.setTextDatum(lgfx::top_left);
    d.drawFastHLine(4, 36, 232, ui::kLine);

    const int rowCount = static_cast<int>(view.rows.size());
    constexpr int kVisibleRows = 7;
    int top = view.selectedIndex - kVisibleRows / 2;
    if (top < 0) top = 0;
    const int maxTop = std::max(0, rowCount - kVisibleRows);
    if (top > maxTop) top = maxTop;

    for (int row = 0; row < kVisibleRows; ++row) {
        const int index = top + row;
        if (index >= rowCount) break;
        const SettingsRowView& item = view.rows[static_cast<size_t>(index)];
        const bool focus = index == view.selectedIndex;
        const int y = 42 + row * 11;
        if (focus) {
            d.fillRect(4, y - 2, 228, 11, ui::kSurfaceHi);
            d.fillRect(4, y - 2, 2, 11, ui::kAccent);
        }
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(focus ? ui::kText : ui::kDim, focus ? ui::kSurfaceHi : ui::kBg);
        d.drawString(item.label.c_str(), 10, y);
        d.setTextDatum(lgfx::top_right);
        d.setTextColor(focus ? ui::kAccentHi : ui::kFaint,
                       focus ? ui::kSurfaceHi : ui::kBg);
        d.drawString(item.value.c_str(), 228, y);
        d.setTextDatum(lgfx::top_left);
    }

    // Scroll thumb when there are more rows than fit.
    if (rowCount > kVisibleRows) {
        constexpr int kTrackY = 42;
        constexpr int kTrackH = 66;
        d.fillRect(235, kTrackY, 2, kTrackH, ui::kLine);
        const int thumbH = std::max(10, kTrackH * kVisibleRows / rowCount);
        const int thumbY =
            kTrackY + (kTrackH - thumbH) * top / (rowCount - kVisibleRows);
        d.fillRect(235, thumbY, 2, thumbH, ui::kAccentHi);
    }

    d.setTextFont(0);
    d.setTextColor(view.note.empty() ? ui::kFaint : view.noteColor, ui::kBg);
    d.drawString(view.note.empty() ? "Enter: change  Esc: back" : view.note.c_str(),
                 8, 123);
}

}  // namespace kk::app

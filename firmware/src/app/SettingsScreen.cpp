// Settings screen drawing. Pure view code: no app globals, no present(), no
// startWrite()/endWrite() (the caller owns the transfer window).
#include "app/SettingsScreen.h"

#include "app/ButtonHint.h"
#include "ui/HintBar.h"
#include "ui/ListView.h"

namespace kk::app {
namespace {

void drawSettingsRow(lgfx::LovyanGFX& d, int index, int x, int y, int width,
                     bool focus, const void* ctx) {
    (void)width;
    const SettingsView& view = *static_cast<const SettingsView*>(ctx);
    const SettingsRowView& item = view.rows[static_cast<size_t>(index)];
    d.setTextFont(0);
    d.setTextSize(1);
    if (item.disabled) {
        // Focusable but inert: keep the bar for the cursor, fade the text.
        d.setTextColor(ui::kFaint, focus ? ui::kSurfaceHi : ui::kBg);
        d.drawString(item.label.c_str(), x, y);
        d.setTextDatum(lgfx::top_right);
        d.setTextColor(ui::kFaint, focus ? ui::kSurfaceHi : ui::kBg);
    } else {
        d.setTextColor(focus ? ui::kText : ui::kDim, focus ? ui::kSurfaceHi : ui::kBg);
        d.drawString(item.label.c_str(), x, y);
        d.setTextDatum(lgfx::top_right);
        d.setTextColor(focus ? ui::kAccentHi : ui::kFaint,
                       focus ? ui::kSurfaceHi : ui::kBg);
    }
    d.drawString(item.value.c_str(), 228, y);
    d.setTextDatum(lgfx::top_left);
}

}  // namespace

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
    d.drawString(view.title.empty() ? "settings" : view.title.c_str(), 7, 22);
    ui::drawHintRight(d, pickHint("Esc: back", "B: back"), ui::kFaint, 232, 5);
    d.drawFastHLine(4, 36, 232, ui::kLine);

    constexpr int kVisibleRows = 7;
    const ui::ListWindow window =
        ui::listWindow(static_cast<int>(view.rows.size()), view.selectedIndex, kVisibleRows);
    ui::drawList(d, window, drawSettingsRow, &view);

    d.setTextFont(0);
    if (!view.note.empty()) {
        d.setTextColor(view.noteColor, ui::kBg);
        d.drawString(view.note.c_str(), 8, 123);
    } else {
        // The focused row's registry help; a note always wins until the
        // selection moves.
        ui::drawHintLeft(d,
                         view.help.empty() ? pickHint("Enter: change  Esc: back", "hold A: change  B: back") : view.help.c_str(),
                         ui::kFaint, 8, 123);
    }
}

}  // namespace kk::app

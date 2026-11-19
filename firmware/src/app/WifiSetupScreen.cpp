// On-device Wi-Fi setup drawing. Pure view code: no app globals, no present(),
// no startWrite()/endWrite() (the caller owns the transfer window).
#include "app/WifiSetupScreen.h"

#include <algorithm>

namespace kk::app {
namespace {

constexpr int kVisibleRows = 6;

std::string trimToWidth(lgfx::LovyanGFX& d, std::string text, int maxWidth) {
    while (text.size() > 1 && d.textWidth(text.c_str()) > maxWidth) text.pop_back();
    return text;
}

std::string tailToWidth(lgfx::LovyanGFX& d, std::string text, int maxWidth) {
    while (text.size() > 1 && d.textWidth(text.c_str()) > maxWidth) text.erase(0, 1);
    return text;
}

void drawHeader(lgfx::LovyanGFX& d, const char* subtitle) {
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString(subtitle, 7, 22);
    d.setTextFont(0);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString("Esc: back", 232, 5);
    d.setTextDatum(lgfx::top_left);
    d.drawFastHLine(4, 36, 232, ui::kLine);
}

void drawNote(lgfx::LovyanGFX& d, const WifiSetupView& view, int y) {
    if (view.note.empty()) return;
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(view.noteColor, ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.drawString(trimToWidth(d, view.note, 224).c_str(), 8, y);
}

void drawHelp(lgfx::LovyanGFX& d, const WifiSetupView& view) {
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.setTextDatum(lgfx::top_left);
    const char* help = view.help.empty() ? "Esc: back" : view.help.c_str();
    d.drawString(trimToWidth(d, help, 224).c_str(), 8, 124);
}

// A one-line input box with the tail visible (so long passwords scroll) and a
// small caret after the text. `view.text` is already masked when needed.
void drawTextField(lgfx::LovyanGFX& d, const std::string& text, int y) {
    d.fillRect(6, y, 228, 18, ui::kSurface);
    d.drawRect(6, y, 228, 18, ui::kLine);
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ui::kText, ui::kSurface);
    d.setTextDatum(lgfx::top_left);
    const std::string shown = tailToWidth(d, text, 208);
    d.drawString(shown.c_str(), 10, y + 5);
    const int caretX = 10 + d.textWidth(shown.c_str()) + 1;
    if (caretX < 230) d.fillRect(caretX, y + 5, 2, 10, ui::kAccentHi);
}

void drawList(lgfx::LovyanGFX& d, const WifiSetupView& view) {
    const int rowCount = static_cast<int>(view.rows.size());
    int top = view.selectedIndex - kVisibleRows / 2;
    if (top < 0) top = 0;
    const int maxTop = std::max(0, rowCount - kVisibleRows);
    if (top > maxTop) top = maxTop;

    for (int row = 0; row < kVisibleRows; ++row) {
        const int index = top + row;
        if (index >= rowCount) break;
        const WifiListRowView& item = view.rows[static_cast<size_t>(index)];
        const bool focus = index == view.selectedIndex;
        const int y = 42 + row * 11;
        if (focus) {
            d.fillRect(4, y - 2, 228, 11, ui::kSurfaceHi);
            d.fillRect(4, y - 2, 2, 11, ui::kAccent);
        }
        d.setTextFont(0);
        d.setTextSize(1);
        const uint16_t labelColor =
            item.manual ? (focus ? ui::kAccentHi : ui::kDim)
                        : (focus ? ui::kText : ui::kDim);
        d.setTextColor(labelColor, focus ? ui::kSurfaceHi : ui::kBg);
        d.setTextDatum(lgfx::top_left);
        d.drawString(trimToWidth(d, item.ssid, 142).c_str(), 9, y);

        d.setTextDatum(lgfx::top_right);
        if (item.manual) {
            d.setTextColor(ui::kFaint, focus ? ui::kSurfaceHi : ui::kBg);
            d.drawString("hidden", 228, y);
        } else {
            d.setTextColor(focus ? ui::kAccentHi : ui::kFaint,
                           focus ? ui::kSurfaceHi : ui::kBg);
            const int securityWidth = d.textWidth(item.security.c_str());
            d.drawString(item.security.c_str(), 228, y);
            if (!item.signal.empty()) {
                d.setTextColor(focus ? ui::kText : ui::kDim,
                               focus ? ui::kSurfaceHi : ui::kBg);
                d.drawString(item.signal.c_str(), 228 - securityWidth - 5, y);
            }
        }
        d.setTextDatum(lgfx::top_left);
    }

    if (rowCount > kVisibleRows) {
        constexpr int kTrackY = 42;
        constexpr int kTrackH = 66;
        d.fillRect(235, kTrackY, 2, kTrackH, ui::kLine);
        const int thumbH = std::max(10, kTrackH * kVisibleRows / rowCount);
        const int thumbY = kTrackY + (kTrackH - thumbH) * top / (rowCount - kVisibleRows);
        d.fillRect(235, thumbY, 2, thumbH, ui::kAccentHi);
    }

    drawNote(d, view, 110);
    drawHelp(d, view);
}

void drawManual(lgfx::LovyanGFX& d, const WifiSetupView& view) {
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.drawString("Network name (hidden)", 8, 48);
    drawTextField(d, view.text, 64);
    d.setTextFont(0);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("1-32 characters, 2.4 GHz only", 8, 88);
    drawNote(d, view, 108);
    drawHelp(d, view);
}

void drawPassword(lgfx::LovyanGFX& d, const WifiSetupView& view) {
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.drawString("Network", 8, 44);
    d.setTextFont(1);
    d.setTextColor(ui::kText, ui::kBg);
    d.drawString(trimToWidth(d, view.ssid, 220).c_str(), 8, 55);

    d.setTextFont(0);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("Password", 8, 78);
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(ui::kAccentHi, ui::kBg);
    d.drawString(view.showPassword ? "Tab: hide" : "Tab: show", 232, 78);
    d.setTextDatum(lgfx::top_left);
    drawTextField(d, view.text, 88);
    drawNote(d, view, 112);
    drawHelp(d, view);
}

}  // namespace

void drawWifiSetupScreen(lgfx::LovyanGFX& d, const WifiSetupView& view) {
    d.fillScreen(ui::kBg);
    switch (view.panel) {
        case WifiSetupPanel::List:
            drawHeader(d, "Wi-Fi · 2.4 GHz networks");
            drawList(d, view);
            break;
        case WifiSetupPanel::ManualSsid:
            drawHeader(d, "Wi-Fi · hidden network");
            drawManual(d, view);
            break;
        case WifiSetupPanel::Password:
            drawHeader(d, "Wi-Fi · password");
            drawPassword(d, view);
            break;
    }
}

}  // namespace kk::app

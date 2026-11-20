// On-device Wi-Fi setup drawing. Pure view code: no app globals, no present(),
// no startWrite()/endWrite() (the caller owns the transfer window).
#include "app/WifiSetupScreen.h"

#include "ui/HintBar.h"
#include "ui/Icons.h"
#include "ui/ListView.h"
#include "ui/UiFont.h"

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
    ui::applyFont(d, ui::UiFont::Title);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString(subtitle, 7, 22);
    ui::drawIcon(d, ui::kIconWifi, 7 + d.textWidth(subtitle) + 4, 20, ui::kDim);
    ui::drawHintRight(d, "Esc: back", ui::kFaint, 232, 5);
    d.drawFastHLine(4, 36, 232, ui::kLine);
}

void drawNote(lgfx::LovyanGFX& d, const WifiSetupView& view, int y) {
    if (view.note.empty()) return;
    ui::applyFont(d, ui::UiFont::Small);
    ui::drawHintLeft(d, trimToWidth(d, view.note, 224).c_str(), view.noteColor, 8, y);
}

void drawHelp(lgfx::LovyanGFX& d, const WifiSetupView& view) {
    ui::applyFont(d, ui::UiFont::Small);
    const char* help = view.help.empty() ? "Esc: back" : view.help.c_str();
    ui::drawHintLeft(d, trimToWidth(d, help, 224).c_str(), ui::kFaint, 8, 124);
}

// A one-line input box with the tail visible (so long passwords scroll) and a
// small caret after the text. `view.text` is already masked when needed.
void drawTextField(lgfx::LovyanGFX& d, const std::string& text, int y) {
    d.fillRect(6, y, 228, 18, ui::kSurface);
    d.drawRect(6, y, 228, 18, ui::kLine);
    ui::applyFont(d, ui::UiFont::Base);
    d.setTextColor(ui::kText, ui::kSurface);
    d.setTextDatum(lgfx::top_left);
    const std::string shown = tailToWidth(d, text, 208);
    d.drawString(shown.c_str(), 10, y + 5);
    const int caretX = 10 + d.textWidth(shown.c_str()) + 1;
    if (caretX < 230) d.fillRect(caretX, y + 5, 2, 10, ui::kAccentHi);
}

void drawScanRow(lgfx::LovyanGFX& d, int index, int x, int y, int width, bool focus,
                 const void* ctx) {
    (void)width;
    const WifiSetupView& view = *static_cast<const WifiSetupView*>(ctx);
    const WifiListRowView& item = view.rows[static_cast<size_t>(index)];
    ui::applyFont(d, ui::UiFont::Small);
    const uint16_t labelColor =
        item.manual ? (focus ? ui::kAccentHi : ui::kDim)
                    : (focus ? ui::kText : ui::kDim);
    d.setTextColor(labelColor, focus ? ui::kSurfaceHi : ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.drawString(trimToWidth(d, item.ssid, 142).c_str(), x, y);

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

void drawNetworkList(lgfx::LovyanGFX& d, const WifiSetupView& view) {
    const int rowCount = static_cast<int>(view.rows.size());
    const ui::ListWindow window = ui::listWindow(rowCount, view.selectedIndex, kVisibleRows);
    ui::ListStyle style;
    style.textInset = 5;  // scan labels start at x=9; other lists use x=10
    ui::drawList(d, window, drawScanRow, &view, style);

    drawNote(d, view, 110);
    drawHelp(d, view);
}

void drawManual(lgfx::LovyanGFX& d, const WifiSetupView& view) {
    ui::applyFont(d, ui::UiFont::Small);
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
    ui::applyFont(d, ui::UiFont::Small);
    d.setTextColor(ui::kDim, ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.drawString("Network", 8, 44);
    d.setTextFont(1);
    d.setTextColor(ui::kText, ui::kBg);
    d.drawString(trimToWidth(d, view.ssid, 220).c_str(), 8, 55);

    d.setTextFont(0);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("Password", 8, 78);
    ui::drawHintRight(d, view.showPassword ? "Tab: hide" : "Tab: show", ui::kAccentHi, 232, 78);
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
            drawNetworkList(d, view);
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

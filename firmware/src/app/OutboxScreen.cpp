// Outbox screen drawing. Pure view code: no app globals, no present(), no
// startWrite()/endWrite() (the caller owns the transfer window).
#include "app/OutboxScreen.h"

#include <cstdio>

#include "ui/HintBar.h"
#include "ui/ListView.h"
#include "ui/Text.h"
#include "ui/UiFont.h"
#include "wk/Utf8.h"

namespace kk::app {
namespace {

void fitBack(lgfx::LovyanGFX& d, std::string& text, int maxWidth) {
    while (text.size() > 1 && d.textWidth(text.c_str()) > maxWidth) {
        const char* begin = text.c_str();
        const char* last = begin;
        const char* p = begin;
        while (*p) {
            last = p;
            kk::utf8Next(&p);
        }
        if (last == begin) break;
        text.erase(static_cast<size_t>(last - begin));
    }
}

uint16_t reasonColor(OutboxReasonKind kind) {
    switch (kind) {
        case OutboxReasonKind::Warn: return ui::kWarn;
        case OutboxReasonKind::Err: return ui::kErr;
        case OutboxReasonKind::Faint: break;
    }
    return ui::kFaint;
}

void drawOutboxRow(lgfx::LovyanGFX& d, int index, int x, int y, int width, bool focus,
                   const void* ctx) {
    (void)width;
    const OutboxView& view = *static_cast<const OutboxView*>(ctx);
    const OutboxRowView& item = view.rows[static_cast<size_t>(index)];

    ui::applyFont(d, ui::UiFont::Small);
    const int reasonWidth = d.textWidth(item.reason.c_str());
    std::string label = item.label;
    fitBack(d, label, 212 - reasonWidth);

    d.setTextColor(focus ? ui::kText : ui::kDim, focus ? ui::kSurfaceHi : ui::kBg);
    d.drawString(label.c_str(), x, y);
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(reasonColor(item.reasonKind), focus ? ui::kSurfaceHi : ui::kBg);
    d.drawString(item.reason.c_str(), 228, y);
    d.setTextDatum(lgfx::top_left);
}

}  // namespace

void drawOutboxScreen(lgfx::LovyanGFX& d, const OutboxView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    ui::applyFont(d, ui::UiFont::Title);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("OUTBOX", 6, 2);
    char count[32];
    snprintf(count, sizeof count, "%u need attention", view.total);
    ui::drawHintRight(d, count, ui::kErr, 232, 5);

    if (view.rows.empty()) {
        d.drawFastHLine(4, 22, 232, ui::kLine);
        d.setTextFont(1);
        d.setTextColor(ui::kDim, ui::kBg);
        d.drawString("No reviews need attention", 8, 52);
        ui::drawHintLeft(d, "Esc: back", ui::kFaint, 8, 124);
        return;
    }

    if (view.confirm) {
        d.setTextFont(1);
        d.setTextColor(ui::kErr, ui::kBg);
        d.drawString("discard review", 7, 22);
        d.drawFastHLine(4, 36, 232, ui::kLine);

        std::string label = view.confirmLabel;
        fitBack(d, label, 224);
        d.setTextFont(1);
        d.setTextColor(ui::kText, ui::kBg);
        d.drawString(label.c_str(), 8, 42);

        ui::drawWrapped(d, view.confirmReason, 8, 60, 224, 3, ui::kDim, ui::kBg);
        ui::drawWrapped(d,
                        "Dismissing only removes the local unsent review and does not change "
                        "WaniKani.",
                        8, 88, 224, 3, ui::kFaint, ui::kBg);
        if (!view.note.empty()) {
            ui::applyFont(d, ui::UiFont::Small);
            d.setTextColor(ui::kErr, ui::kBg);
            d.drawString(view.note.c_str(), 8, 112);
        }

        ui::drawHintLeft(d, "Enter: discard  Esc: cancel", ui::kFaint, 8, 124);
        return;
    }

    d.drawFastHLine(4, 22, 232, ui::kLine);

    int selectedIndex = view.selectedIndex;
    if (selectedIndex < 0) selectedIndex = 0;

    constexpr int kVisibleRows = 7;
    const ui::ListWindow window =
        ui::listWindow(static_cast<int>(view.rows.size()), selectedIndex, kVisibleRows);
    ui::ListStyle style;
    style.y = 28;
    style.rowHeight = 12;
    style.trackHeight = kVisibleRows * style.rowHeight - 4;
    ui::drawList(d, window, drawOutboxRow, &view, style);

    if (!view.note.empty()) {
        ui::applyFont(d, ui::UiFont::Small);
        d.setTextColor(ui::kErr, ui::kBg);
        d.drawString(view.note.c_str(), 8, 112);
    }
    ui::drawHintLeft(d, "Enter: review  D: discard  Esc: back", ui::kFaint, 8, 124);
}

}  // namespace kk::app

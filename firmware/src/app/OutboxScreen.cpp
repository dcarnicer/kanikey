// Outbox screen drawing. Pure view code: no app globals, no present(), no
// startWrite()/endWrite() (the caller owns the transfer window).
#include "app/OutboxScreen.h"

#include <algorithm>
#include <cstdio>

#include "ui/Text.h"
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

}  // namespace

void drawOutboxScreen(lgfx::LovyanGFX& d, const OutboxView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("OUTBOX", 6, 2);
    char count[32];
    snprintf(count, sizeof count, "%u need attention", view.total);
    d.setTextFont(0);
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(ui::kErr, ui::kBg);
    d.drawString(count, 232, 5);
    d.setTextDatum(lgfx::top_left);

    if (view.rows.empty()) {
        d.drawFastHLine(4, 22, 232, ui::kLine);
        d.setTextFont(1);
        d.setTextColor(ui::kDim, ui::kBg);
        d.drawString("No reviews need attention", 8, 52);
        d.setTextFont(0);
        d.setTextColor(ui::kFaint, ui::kBg);
        d.drawString("Esc: back", 8, 124);
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
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(ui::kErr, ui::kBg);
            d.drawString(view.note.c_str(), 8, 112);
        }

        d.setTextFont(0);
        d.setTextColor(ui::kFaint, ui::kBg);
        d.drawString("Enter: discard  Esc: cancel", 8, 124);
        return;
    }

    d.drawFastHLine(4, 22, 232, ui::kLine);

    int selectedIndex = view.selectedIndex;
    if (selectedIndex < 0) selectedIndex = 0;

    constexpr int kVisibleRows = 7;
    constexpr int kRowY = 28;
    constexpr int kRowH = 12;
    int top = selectedIndex - kVisibleRows / 2;
    if (top < 0) top = 0;
    const int maxTop = std::max(0, static_cast<int>(view.rows.size()) - kVisibleRows);
    if (top > maxTop) top = maxTop;

    for (int row = 0; row < kVisibleRows; ++row) {
        const int index = top + row;
        if (index >= static_cast<int>(view.rows.size())) break;
        const OutboxRowView& item = view.rows[static_cast<size_t>(index)];
        const bool focus = index == selectedIndex;
        const int y = kRowY + row * kRowH;
        if (focus) {
            d.fillRect(4, y - 2, 228, kRowH, ui::kSurfaceHi);
            d.fillRect(4, y - 2, 2, kRowH, ui::kAccent);
        }

        const int reasonWidth = d.textWidth(item.reason.c_str());
        std::string label = item.label;
        fitBack(d, label, 212 - reasonWidth);

        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(focus ? ui::kText : ui::kDim, focus ? ui::kSurfaceHi : ui::kBg);
        d.drawString(label.c_str(), 10, y);
        d.setTextDatum(lgfx::top_right);
        d.setTextColor(reasonColor(item.reasonKind), focus ? ui::kSurfaceHi : ui::kBg);
        d.drawString(item.reason.c_str(), 228, y);
        d.setTextDatum(lgfx::top_left);
    }

    if (view.rows.size() > kVisibleRows) {
        constexpr int kTrackY = kRowY;
        constexpr int kTrackH = kVisibleRows * kRowH - 4;
        d.fillRect(235, kTrackY, 2, kTrackH, ui::kLine);
        const int thumbH =
            std::max(10, kTrackH * kVisibleRows / static_cast<int>(view.rows.size()));
        const int thumbY = kTrackY + (kTrackH - thumbH) * top /
                                         (static_cast<int>(view.rows.size()) - kVisibleRows);
        d.fillRect(235, thumbY, 2, thumbH, ui::kAccentHi);
    }

    if (!view.note.empty()) {
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(ui::kErr, ui::kBg);
        d.drawString(view.note.c_str(), 8, 112);
    }
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("Enter: review  D: discard  Esc: back", 8, 124);
}

}  // namespace kk::app

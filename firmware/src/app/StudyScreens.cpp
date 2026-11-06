#include "app/StudyScreens.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "ui/Widgets.h"
#include "wk/Utf8.h"

namespace kk::app {
namespace {
const char* kStudyLabels[kStudyRows] = {"Recent mistakes", "Recent lessons", "Critical items",
                                        "Current level"};

void drawPrompt(lgfx::LovyanGFX& d, kk::GlyphPack& pack, const kk::Subject* s, int cx, int cy) {
    if (!s) {
        ui::drawMissingGlyph(d, pack, cx, cy);
        return;
    }
    if (!ui::drawSubjectPrompt(d, pack, *s, cx, cy, 20, 18, 18, ui::kText, ui::kSurface,
                               kk::ClipRect{cx - 10, cy - 9, cx + 10, cy + 9}))
        ui::drawMissingGlyph(d, pack, cx, cy);
}
}  // namespace

void drawStudyMenuScreen(lgfx::LovyanGFX& d, const StudyMenuView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("EXTRA STUDY", 6, 2);
    d.drawFastHLine(4, 22, 232, ui::kLine);

    for (int i = 0; i < kStudyRows; ++i) {
        const int y = 30 + i * 20;
        const bool focus = i == view.selectedIndex;
        if (focus) {
            d.fillRect(4, y - 1, 228, 19, ui::kSurfaceHi);
            d.fillRect(4, y - 1, 2, 19, ui::kAccent);
        }
        d.setTextFont(1);
        d.setTextSize(1);
        d.setTextColor(focus ? ui::kText : ui::kDim, focus ? ui::kSurfaceHi : ui::kBg);
        d.drawString(kStudyLabels[i], 12, y + 2);
        if (view.counts[i][0]) {
            d.setTextFont(0);
            d.setTextDatum(lgfx::top_right);
            d.setTextColor(focus ? ui::kAccentHi : ui::kFaint,
                           focus ? ui::kSurfaceHi : ui::kBg);
            d.drawString(view.counts[i], 228, y + 4);
            d.setTextDatum(lgfx::top_left);
        }
    }

    if (view.note[0]) {
        d.setTextFont(0);
        d.setTextColor(ui::kWarn, ui::kBg);
        d.drawString(view.note, 8, 112);
    }
    d.setTextFont(0);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("Enter: open  Esc: home", 8, 125);
}

void drawCriticalListScreen(lgfx::LovyanGFX& d, kk::GlyphPack& pack,
                            const CriticalListView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("CRITICAL ITEMS", 6, 3);

    const int total = static_cast<int>(view.rows.size());
    if (total == 0) {
        d.setTextFont(1);
        d.setTextColor(ui::kFaint, ui::kBg);
        d.drawString("No critical items", 8, 56);
        d.setTextFont(0);
        d.setTextColor(ui::kFaint, ui::kBg);
        d.drawString("Esc: back", 8, 125);
        return;
    }
    char count[16];
    snprintf(count, sizeof count, "%d", total);
    d.setTextFont(0);
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString(count, 232, 5);
    d.setTextDatum(lgfx::top_left);
    d.fillRect(0, 14, 240, 2, ui::kSurfaceHi);

    constexpr int kVisible = 5;
    int top = std::max(0, view.selectedIndex - kVisible / 2);
    if (top + kVisible > total) top = std::max(0, total - kVisible);
    for (int row = 0; row < kVisible && top + row < total; ++row) {
        const int i = top + row;
        const CriticalRowView& item = view.rows[static_cast<size_t>(i)];
        const int y = 18 + row * 20;
        if (i == view.selectedIndex) {
            d.fillRect(4, y - 1, 228, 19, ui::kSurfaceHi);
            d.fillRect(4, y - 1, 2, 19, ui::kAccent);
        }
        d.fillRect(9, y, 22, 20, ui::kSurface);
        d.drawRect(9, y, 22, 20, ui::kLine);
        drawPrompt(d, pack, item.subject, 20, y + 10);

        char label[72];
        snprintf(label, sizeof label, "%s", item.meaning.c_str());
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(i == view.selectedIndex ? ui::kText : ui::kDim,
                       i == view.selectedIndex ? ui::kSurfaceHi : ui::kBg);
        char* end = label + strlen(label);
        while (end > label + 1 && d.textWidth(label) > 178) *--end = '\0';
        d.drawString(label, 34, y + 1);

        char percent[12];
        snprintf(percent, sizeof percent, "%u%%", static_cast<unsigned>(item.percentage));
        d.setTextDatum(lgfx::top_right);
        d.setTextColor(ui::kErr, i == view.selectedIndex ? ui::kSurfaceHi : ui::kBg);
        d.drawString(percent, 230, y + 1);
        d.setTextDatum(lgfx::top_left);
    }
    if (total > kVisible) {
        constexpr int kTrackY = 18;
        constexpr int kTrackH = 100;
        d.fillRect(236, kTrackY, 2, kTrackH, ui::kLine);
        const int thumbH = std::max(10, kTrackH * kVisible / total);
        const int thumbY = kTrackY + (kTrackH - thumbH) * top / std::max(1, total - kVisible);
        d.fillRect(236, thumbY, 2, thumbH, ui::kAccentHi);
    }
    d.setTextFont(0);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("Enter: detail  P: practice  Esc: back", 8, 125);
}

}  // namespace kk::app

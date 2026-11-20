#include "app/StudyScreens.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "ui/HintBar.h"
#include "ui/ListView.h"
#include "ui/UiFont.h"
#include "ui/Widgets.h"
#include "wk/Utf8.h"

namespace kk::app {
namespace {
const char* kStudyLabels[kStudyRows] = {"Recent mistakes", "Recent lessons", "Critical items",
                                        "Current level"};

void drawPrompt(lgfx::LovyanGFX& d, kk::GlyphPack& fontPack, kk::GlyphPack& radicalPack,
                const kk::Subject* s, int cx, int cy) {
    if (!s) {
        ui::drawMissingGlyph(d, fontPack, radicalPack, cx, cy);
        return;
    }
    if (!ui::drawSubjectPrompt(d, fontPack, radicalPack, *s, cx, cy, 20, 18, 18, ui::kText,
                               ui::kSurface,
                               kk::ClipRect{cx - 10, cy - 9, cx + 10, cy + 9}))
        ui::drawMissingGlyph(d, fontPack, radicalPack, cx, cy);
}
}  // namespace

void drawStudyMenuScreen(lgfx::LovyanGFX& d, const StudyMenuView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    ui::applyFont(d, ui::UiFont::Title);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("EXTRA STUDY", 6, 2);
    d.drawFastHLine(4, 22, 232, ui::kLine);

    const ui::ListWindow window = ui::listWindow(kStudyRows, view.selectedIndex, kStudyRows);
    for (int row = 0; row < window.rows; ++row) {
        const int i = window.first + row;
        const int y = 30 + row * 20;
        const bool focus = i == view.selectedIndex;
        if (focus) {
            d.fillRect(4, y - 1, 228, 19, ui::kSurfaceHi);
            d.fillRect(4, y - 1, 2, 19, ui::kAccent);
        }
        ui::applyFont(d, ui::UiFont::Base);
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
    ui::drawHintLeft(d, "Enter: open  Esc: home");
}

void drawCriticalListScreen(lgfx::LovyanGFX& d, kk::GlyphPack& fontPack,
                            kk::GlyphPack& radicalPack, const CriticalListView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    ui::applyFont(d, ui::UiFont::Base);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("CRITICAL ITEMS", 6, 3);

    const int total = static_cast<int>(view.rows.size());
    if (total == 0) {
        d.setTextFont(1);
        d.setTextColor(ui::kFaint, ui::kBg);
        d.drawString("No critical items", 8, 56);
        ui::drawHintLeft(d, "Esc: back");
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
    const ui::ListWindow window = ui::listWindow(total, view.selectedIndex, kVisible);
    for (int row = 0; row < window.rows; ++row) {
        const int i = window.first + row;
        const CriticalRowView& item = view.rows[static_cast<size_t>(i)];
        const int y = 18 + row * 20;
        if (i == view.selectedIndex) {
            d.fillRect(4, y - 1, 228, 19, ui::kSurfaceHi);
            d.fillRect(4, y - 1, 2, 19, ui::kAccent);
        }
        d.fillRect(9, y, 22, 20, ui::kSurface);
        d.drawRect(9, y, 22, 20, ui::kLine);
        drawPrompt(d, fontPack, radicalPack, item.subject, 20, y + 10);

        char label[72];
        snprintf(label, sizeof label, "%s", item.meaning.c_str());
        ui::applyFont(d, ui::UiFont::Small);
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
        const int thumbY =
            kTrackY + (kTrackH - thumbH) * window.first / std::max(1, total - kVisible);
        d.fillRect(236, thumbY, 2, thumbH, ui::kAccentHi);
    }
    ui::drawHintLeft(d, "Enter: detail  P: practice  Esc: back");
}

}  // namespace kk::app

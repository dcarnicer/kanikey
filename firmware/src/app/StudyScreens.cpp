#include "app/StudyScreens.h"

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

void drawStudyRow(lgfx::LovyanGFX& d, int index, int x, int y, int width, bool focus,
                  const void* ctx) {
    (void)width;
    const StudyMenuView& view = *static_cast<const StudyMenuView*>(ctx);
    ui::applyFont(d, ui::UiFont::Base);
    d.setTextColor(focus ? ui::kText : ui::kDim, focus ? ui::kSurfaceHi : ui::kBg);
    d.drawString(kStudyLabels[index], x, y + 2);
    if (view.counts[index][0]) {
        d.setTextFont(0);
        d.setTextDatum(lgfx::top_right);
        d.setTextColor(focus ? ui::kAccentHi : ui::kFaint,
                       focus ? ui::kSurfaceHi : ui::kBg);
        d.drawString(view.counts[index], 228, y + 4);
        d.setTextDatum(lgfx::top_left);
    }
}

struct CriticalListCtx {
    const CriticalListView* view = nullptr;
    kk::GlyphPack* fontPack = nullptr;
    kk::GlyphPack* radicalPack = nullptr;
};

void drawCriticalRow(lgfx::LovyanGFX& d, int index, int x, int y, int width, bool focus,
                     const void* ctx) {
    (void)width;
    const CriticalListCtx& list = *static_cast<const CriticalListCtx*>(ctx);
    const CriticalRowView& item = list.view->rows[static_cast<size_t>(index)];
    d.fillRect(x, y, 22, 20, ui::kSurface);
    d.drawRect(x, y, 22, 20, ui::kLine);
    drawPrompt(d, *list.fontPack, *list.radicalPack, item.subject, x + 11, y + 10);

    char label[72];
    snprintf(label, sizeof label, "%s", item.meaning.c_str());
    ui::applyFont(d, ui::UiFont::Small);
    d.setTextColor(focus ? ui::kText : ui::kDim, focus ? ui::kSurfaceHi : ui::kBg);
    char* end = label + strlen(label);
    while (end > label + 1 && d.textWidth(label) > 178) *--end = '\0';
    d.drawString(label, x + 25, y + 1);

    char percent[12];
    snprintf(percent, sizeof percent, "%u%%", static_cast<unsigned>(item.percentage));
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(ui::kErr, focus ? ui::kSurfaceHi : ui::kBg);
    d.drawString(percent, 230, y + 1);
    d.setTextDatum(lgfx::top_left);
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
    ui::ListStyle style;
    style.x = 4;
    style.y = 30;
    style.width = 228;
    style.rowHeight = 20;
    style.textInset = 8;  // the labels start at x=12
    style.focusOffsetY = -1;
    style.focusHeight = 19;
    ui::drawList(d, window, drawStudyRow, &view, style);

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
    ui::ListStyle style;
    style.x = 4;
    style.y = 18;
    style.width = 228;
    style.rowHeight = 20;
    style.textInset = 5;  // the prompt box starts at x=9
    style.focusOffsetY = -1;
    style.focusHeight = 19;
    style.trackX = 236;
    style.trackHeight = 100;
    CriticalListCtx ctx{&view, &fontPack, &radicalPack};
    ui::drawList(d, window, drawCriticalRow, &ctx, style);
    ui::drawHintLeft(d, "Enter: detail  P: practice  Esc: back");
}

}  // namespace kk::app

// Development screens drawing. Pure view code: no app globals, no present(),
// no startWrite()/endWrite() (the caller owns the transfer window).
#include "app/DebugScreens.h"

#ifdef KANIKEY_DEV
#include <cstdio>
#include <cstring>

#include "ui/Widgets.h"

namespace kk::app {

using namespace ui;

void drawDebugScreen(lgfx::LovyanGFX& d, const DebugView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(2);
    d.setTextSize(2);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("debug build", 7, 38);

    char right[24];
    if (view.batteryPercent >= 0)
        snprintf(right, sizeof right, "%d%%", view.batteryPercent);
    else
        snprintf(right, sizeof right, "USB");
    d.setTextColor(view.batteryPercent >= 0 && view.batteryPercent < 20 ? ui::kWarn
                                                                       : ui::kDim,
                   ui::kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString(right, 233, 7);
    d.setTextDatum(lgfx::top_left);

    d.setTextColor(ui::kText, ui::kBg);
    d.setTextFont(1);
    d.drawString("M0 hardware check", 7, 54);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString(view.netLine, 7, 66);

    d.fillRect(4, 78, 232, 34, ui::kSurface);
    d.drawRect(4, 78, 232, 34, ui::kLine);
    d.setTextFont(1);
    for (int i = 0; i < 3; ++i) {
        d.setTextColor(i == 2 ? ui::kText : ui::kFaint, ui::kSurface);
        d.drawString(view.log[i], 9, 81 + i * 10);
    }

    d.setTextFont(0);
    d.setTextColor(view.sdOk ? ui::kOk : ui::kWarn, ui::kBg);
    d.drawString(view.sdLine, 5, 117);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString(view.heapLine, 5, 126);

    char mods[24] = "";
    if (view.shift) strncat(mods, "S", sizeof mods - strlen(mods) - 1);
    if (view.fn) strncat(mods, "F", sizeof mods - strlen(mods) - 1);
    if (view.ctrl) strncat(mods, "C", sizeof mods - strlen(mods) - 1);
    if (view.opt) strncat(mods, "O", sizeof mods - strlen(mods) - 1);
    if (view.alt) strncat(mods, "A", sizeof mods - strlen(mods) - 1);
    if (mods[0]) {
        d.setTextColor(ui::kDim, ui::kBg);
        d.setTextDatum(lgfx::top_right);
        d.drawString(mods, 233, 126);
        d.setTextDatum(lgfx::top_left);
    }
}

void drawRadicalTestScreen(lgfx::LovyanGFX& d, kk::GlyphPack& pack,
                           const RadicalTestView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("radical test", 7, 22);

    d.fillRect(4, 38, 232, 66, ui::kSurface);
    d.drawRect(4, 38, 232, 66, ui::kLine);
    const bool ok = pack.drawRadical(d, view.subjectId, 120, 71, 96, 62, ui::kText,
                                     ui::kSurface, kk::ClipRect{6, 40, 234, 102});
    if (!ok) drawMissingGlyph(d, pack, 120, 71);

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ok ? ui::kOk : ui::kWarn, ui::kBg);
    char line[64];
    snprintf(line, sizeof line, "subject %u  %s",
             static_cast<unsigned>(view.subjectId), ok ? "bitmap" : "missing");
    d.drawString(line, 8, 110);
    d.setTextFont(0);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("R <id>: load  Esc: back", 8, 124);
}

}  // namespace kk::app
#endif

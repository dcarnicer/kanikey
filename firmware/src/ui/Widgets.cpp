// Shared drawing widgets used by the Review and Lesson screens. View code:
// the caller owns startWrite()/present() and passes its GlyphPack.
#include "ui/Widgets.h"

#include <algorithm>

#include "wk/Utf8.h"

namespace kk::ui {

void drawPill(lgfx::LovyanGFX& d, int x, int y, const char* text, uint16_t fg, uint16_t bg) {
    d.setTextFont(0);
    d.setTextSize(1);
    const int w = d.textWidth(text) + 8;
    d.fillRect(x, y, w, 13, bg);
    d.setTextColor(fg, bg);
    d.drawString(text, x + 4, y + 2);
}

void trimFrontToWidth(lgfx::LovyanGFX& d, std::string& text, int maxWidth) {
    while (text.size() > 1 && d.textWidth(text.c_str()) > maxWidth) {
        const char* p = text.c_str();
        kk::utf8Next(&p);
        const size_t n = static_cast<size_t>(p - text.c_str());
        if (n == 0 || n >= text.size()) break;
        text.erase(0, n);
    }
}

// Radicals use their own bitmap; everything else uses the character pack. The
// caller draws an explicit missing-glyph state when this returns false.
bool drawSubjectPrompt(lgfx::LovyanGFX& d, kk::GlyphPack& pack, const kk::Subject& s, int cx,
                       int cy, int maxWidth, int maxHeight, int radicalMaxSide, uint16_t fg,
                       uint16_t bg, const kk::ClipRect& clip) {
    if (s.type == kk::SubjectType::Radical) {
        const int side = std::min({maxWidth, maxHeight, radicalMaxSide});
        if (pack.drawRadical(d, s.id, cx, cy, side, side, fg, bg, clip)) return true;
    }
    if (!s.characters.empty() &&
        pack.drawTextCentered(d, s.characters, 64, cx, cy, maxWidth, maxHeight, fg, bg, clip))
        return true;
    return false;
}

void drawMissingGlyph(lgfx::LovyanGFX& d, const kk::GlyphPack& pack, int cx, int cy) {
    d.setTextDatum(lgfx::middle_center);
    d.setTextFont(4);
    d.setTextSize(1);
    d.setTextColor(kWarn, kSurface);
    d.drawString("?", cx, cy - 8);
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(kFaint, kSurface);
    d.drawString(pack.ready() ? "no glyph" : "no pack", cx, cy + 12);
    d.drawString("check SD", cx, cy + 21);
    d.setTextDatum(lgfx::top_left);
}

// 20 px text that fits, or right-aligned when the reading is too wide. A
// missing glyph is different from a long reading; only the former says
// "no glyph".
bool drawJapaneseFit(lgfx::LovyanGFX& d, kk::GlyphPack& pack, const std::string& text, int x,
                     int right, int y, int maxWidth, uint16_t fg, uint16_t bg,
                     const kk::ClipRect& clip) {
    if (pack.drawTextFit(d, text, x, y, 20, maxWidth, fg, bg, clip)) return true;
    if (pack.covers(text, 20)) return pack.drawTextRight(d, text, right, y, 20, fg, bg, clip);
    return false;
}

}  // namespace kk::ui

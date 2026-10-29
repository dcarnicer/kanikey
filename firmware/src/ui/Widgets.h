#pragma once
// Shared drawing widgets for the Review and Lesson screens: pills, input
// trimming and subject/reading glyph drawing. Each caller owns the transfer
// window (startWrite()/present()) and passes its own GlyphPack.
#include <cstdint>
#include <string>

#include "platform/GlyphPack.h"
#include "ui/Theme.h"
#include "wk/Types.h"

namespace kk::ui {

// Small 6 px label with a background block, top-left at (x, y).
void drawPill(lgfx::LovyanGFX& d, int x, int y, const char* text, uint16_t fg, uint16_t bg);

// Drops whole UTF-8 code points from the front until the text fits maxWidth.
void trimFrontToWidth(lgfx::LovyanGFX& d, std::string& text, int maxWidth);

// Draws the subject's radical bitmap or its character centred at (cx, cy).
// Returns false when neither is available; the caller draws the missing state.
bool drawSubjectPrompt(lgfx::LovyanGFX& d, kk::GlyphPack& pack, const kk::Subject& s, int cx,
                       int cy, int maxWidth, int maxHeight, int radicalMaxSide, uint16_t fg,
                       uint16_t bg, const kk::ClipRect& clip);

// "?" plus the pack status, centred at (cx, cy).
void drawMissingGlyph(lgfx::LovyanGFX& d, const kk::GlyphPack& pack, int cx, int cy);

// 20 px reading that fits maxWidth, or right-aligned to `right` when it is too
// wide. Returns false when the pack lacks a glyph; a long reading is not
// "no glyph".
bool drawJapaneseFit(lgfx::LovyanGFX& d, kk::GlyphPack& pack, const std::string& text, int x,
                     int right, int y, int maxWidth, uint16_t fg, uint16_t bg,
                     const kk::ClipRect& clip);

}  // namespace kk::ui

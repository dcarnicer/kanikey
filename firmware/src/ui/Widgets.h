#pragma once
// Shared drawing widgets for the Review and Lesson screens: pills, input
// trimming and subject/reading glyph drawing. Each caller owns the transfer
// window (startWrite()/present()) and passes the font and radical packs.
#include <cstdint>
#include <string>
#include <vector>

#include "platform/GlyphPack.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "wk/FeedbackLayout.h"
#include "wk/Types.h"

namespace kk::ui {

// Small 6 px label with a background block, top-left at (x, y). An optional
// icon sits after the label in the same pill; the label keeps its position and
// only the block grows to the right.
void drawPill(lgfx::LovyanGFX& d, int x, int y, const char* text, uint16_t fg, uint16_t bg,
              const Icon* icon = nullptr);

// Drops whole UTF-8 code points from the front until the text fits maxWidth.
void trimFrontToWidth(lgfx::LovyanGFX& d, std::string& text, int maxWidth);

// Draws the subject's radical bitmap from `radicalPack`, or its character from
// `fontPack`, centred at (cx, cy). Returns false when neither is available;
// the caller draws the missing state.
bool drawSubjectPrompt(lgfx::LovyanGFX& d, kk::GlyphPack& fontPack,
                       kk::GlyphPack& radicalPack, const kk::Subject& s, int cx, int cy,
                       int maxWidth, int maxHeight, int radicalMaxSide, uint16_t fg,
                       uint16_t bg, const kk::ClipRect& clip);

// "?" plus the pack status, centred at (cx, cy). "no pack" means the font pack
// is missing; a radical-only miss says "no glyph".
void drawMissingGlyph(lgfx::LovyanGFX& d, const kk::GlyphPack& fontPack,
                      const kk::GlyphPack& radicalPack, int cx, int cy);

// 20 px reading that fits maxWidth, or right-aligned to `right` when it is too
// wide. Returns false when the pack lacks a glyph; a long reading is not
// "no glyph".
bool drawJapaneseFit(lgfx::LovyanGFX& d, kk::GlyphPack& pack, const std::string& text, int x,
                     int right, int y, int maxWidth, uint16_t fg, uint16_t bg,
                     const kk::ClipRect& clip);

// REV-10 reading line: the kana at 20 px followed by the unconverted romaji
// tail (`pending`, for example the k of `ak`) in the UI font, so the tail is
// never sent to the kana pack. alignRight keeps the end of a long answer in
// view inside the answer box; otherwise the kana stays left when it fits and
// scrolls right-aligned only when it does not (the feedback line). `y` is the
// kana baseline, `latinY` the top of the tail. False when the pack lacks a kana
// glyph; the caller draws the explicit `no glyph` state.
bool drawReadingText(lgfx::LovyanGFX& d, kk::GlyphPack& pack, const std::string& kana,
                     const std::string& pending, int x, int y, int maxWidth, int latinY,
                     bool alignRight, uint16_t fg, uint16_t latinFg, uint16_t bg,
                     const kk::ClipRect& clip);

// The feedback body (REV-01/03) for the review and lesson quiz feedback frames:
// the pieces wrapped to the box (x, y, w, h) and clipped to it. `scroll` is the
// Up/Down offset in px, in and out: it is clamped to the content here, so the
// key handler may add past the end. A content taller than the box draws a 2 px
// scroll track at x + w + 2 (ListView's style). A Japanese piece the pack
// cannot draw shows `no glyph` in the warning tone. Returns true when the body
// scrolls, which is when the hint bar names the scroll keys.
bool drawFeedbackBody(lgfx::LovyanGFX& d, kk::GlyphPack& pack,
                      const std::vector<kk::FeedbackPiece>& pieces, int x, int y, int w, int h,
                      int* scroll, uint16_t bg);

}  // namespace kk::ui

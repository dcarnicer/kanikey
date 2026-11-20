// Shared drawing widgets used by the Review and Lesson screens. View code:
// the caller owns startWrite()/present() and passes the font and radical packs.
#include "ui/Widgets.h"

#include "ui/UiFont.h"

#include <algorithm>
#include <string_view>

#include "wk/Utf8.h"

namespace kk::ui {

void drawPill(lgfx::LovyanGFX& d, int x, int y, const char* text, uint16_t fg, uint16_t bg,
              const Icon* icon) {
    ui::applyFont(d, ui::UiFont::Small);
    const int textWidth = d.textWidth(text);
    const int w = textWidth + 8 + (icon ? kIconWidth + 4 : 0);
    d.fillRect(x, y, w, 13, bg);
    d.setTextColor(fg, bg);
    d.drawString(text, x + 4, y + 2);
    if (icon) drawIcon(d, *icon, x + 4 + textWidth + 4, y + 1, fg);
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

// Radicals use the radical pack's bitmap first; everything else uses the
// font pack. The caller draws an explicit missing-glyph state when this
// returns false.
bool drawSubjectPrompt(lgfx::LovyanGFX& d, kk::GlyphPack& fontPack,
                       kk::GlyphPack& radicalPack, const kk::Subject& s, int cx, int cy,
                       int maxWidth, int maxHeight, int radicalMaxSide, uint16_t fg,
                       uint16_t bg, const kk::ClipRect& clip) {
    if (s.type == kk::SubjectType::Radical) {
        const int side = std::min({maxWidth, maxHeight, radicalMaxSide});
        if (radicalPack.drawRadical(d, s.id, cx, cy, side, side, fg, bg, clip)) return true;
    }
    if (!s.characters.empty() &&
        fontPack.drawTextCentered(d, s.characters, 64, cx, cy, maxWidth, maxHeight, fg, bg,
                                  clip))
        return true;
    return false;
}

void drawSubjectPanel(lgfx::LovyanGFX& d, kk::GlyphPack& fontPack, kk::GlyphPack& radicalPack,
                      const kk::Subject& s, int x, int y, int w, int h, int cx, int cy,
                      int maxWidth, int maxHeight, int radicalMaxSide) {
    const uint16_t panel = subjectTypeColor(s.type);
    const kk::ClipRect clip{x + 2, y + 2, x + w - 2, y + h - 2};
    d.fillRect(x, y, w, h, panel);
    d.drawRect(x, y, w, h, kLine);
    if (drawSubjectPrompt(d, fontPack, radicalPack, s, cx, cy, maxWidth, maxHeight,
                          radicalMaxSide, kText, panel, clip))
        return;
    // The "?" state sits on the dark panel, where its colours were chosen.
    d.fillRect(x, y, w, h, kSurface);
    d.drawRect(x, y, w, h, kLine);
    drawMissingGlyph(d, fontPack, radicalPack, cx, cy);
}

void drawMissingGlyph(lgfx::LovyanGFX& d, const kk::GlyphPack& fontPack,
                      const kk::GlyphPack& radicalPack, int cx, int cy) {
    const bool noFont = !fontPack.ready();
    d.setTextDatum(lgfx::middle_center);
    d.setTextFont(4);
    d.setTextSize(1);
    d.setTextColor(kWarn, kSurface);
    d.drawString("?", cx, cy - 8);
    ui::applyFont(d, ui::UiFont::Small);
    d.setTextColor(kFaint, kSurface);
    d.drawString(noFont ? "no pack" : "no glyph", cx, cy + 12);
    // "check SD" is the boot-level failure; a missing radical bitmap on a
    // working font pack is a different, non-fatal state.
    if (noFont)
        d.drawString("check SD", cx, cy + 21);
    else if (!radicalPack.ready())
        d.drawString("no radicals", cx, cy + 21);
    else
        d.drawString("not in pack", cx, cy + 21);
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

namespace {
constexpr int kTailGap = 2;        // px between the kana and the romaji tail
constexpr int kTailMaxWidth = 40;  // the tail keeps its last letters when longer
}  // namespace

bool drawReadingText(lgfx::LovyanGFX& d, kk::GlyphPack& pack, const std::string& kana,
                     const std::string& pending, int x, int y, int maxWidth, int latinY,
                     bool alignRight, uint16_t fg, uint16_t latinFg, uint16_t bg,
                     const kk::ClipRect& clip) {
    const int right = x + maxWidth;
    std::string tail = pending;
    int tailW = 0;
    if (!tail.empty()) {
        ui::applyFont(d, ui::UiFont::Base);
        trimFrontToWidth(d, tail, kTailMaxWidth);
        tailW = d.textWidth(tail.c_str());
    }
    const int gap = tailW ? kTailGap : 0;
    const int kanaMax = maxWidth - tailW - gap;
    int kanaW = 0;
    if (!kana.empty()) {
        kanaW = pack.textWidth(kana, 20);
        if (kanaW < 0) return false;
    }
    const bool leftFit = !alignRight && kanaW <= kanaMax;
    // Where the kana ends: before the tail when right-aligned, after the kana
    // when it fits on the left, and at the room left of the tail when it scrolls.
    int kanaEnd = right - tailW - gap;
    if (!alignRight && !kana.empty() && leftFit) kanaEnd = x + kanaW;
    if (!alignRight && !kana.empty() && !leftFit) kanaEnd = x + kanaMax;
    if (!kana.empty()) {
        const bool drawn = leftFit ? pack.drawText(d, kana, x, y, 20, fg, bg, clip)
                                   : pack.drawTextRight(d, kana, kanaEnd, y, 20, fg, bg, clip);
        if (!drawn) return false;
    }
    if (tailW) {
        const int tailX = (!alignRight && kana.empty()) ? x : kanaEnd + gap;
        ui::applyFont(d, ui::UiFont::Base);
        d.setTextColor(latinFg, bg);
        d.drawString(tail.c_str(), tailX, latinY);
    }
    return true;
}

namespace {

// Feedback colours: the answer tones follow the pill (ok or err).
uint16_t feedbackColor(kk::FeedbackTone tone) {
    switch (tone) {
        case kk::FeedbackTone::Typed:
            return kDim;
        case kk::FeedbackTone::Right:
            return kOk;
        case kk::FeedbackTone::Wrong:
            return kErr;
        case kk::FeedbackTone::Warn:
            return kWarn;
        case kk::FeedbackTone::Label:
        case kk::FeedbackTone::Note:
        case kk::FeedbackTone::Synonym:
            return kFaint;
    }
    return kFaint;
}

// The layout measures Font0 with the display and Japanese with the pack. The
// pack width is -1 for a missing glyph; the caller has replaced those pieces,
// so 0 here only guards against a race with a pack close.
struct FeedbackMeasureCtx {
    lgfx::LovyanGFX* d;
    kk::GlyphPack* pack;
};

int measureFeedbackSpan(void* ctx, kk::FeedbackFace face, std::string_view text) {
    auto* m = static_cast<FeedbackMeasureCtx*>(ctx);
    if (face == kk::FeedbackFace::Japanese) {
        const int width = m->pack->textWidth(text, kk::kFeedbackJapaneseHeight);
        return width < 0 ? 0 : width;
    }
    return m->d->textWidth(std::string(text).c_str());
}

}  // namespace

bool drawFeedbackBody(lgfx::LovyanGFX& d, kk::GlyphPack& pack,
                      const std::vector<kk::FeedbackPiece>& input, int x, int y, int w, int h,
                      int* scroll, uint16_t bg) {
    ui::applyFont(d, ui::UiFont::Small);
    d.setTextDatum(lgfx::top_left);

    std::vector<kk::FeedbackPiece> pieces = input;
    for (kk::FeedbackPiece& p : pieces) {
        if (p.face == kk::FeedbackFace::Japanese && !p.text.empty() &&
            !pack.covers(p.text, kk::kFeedbackJapaneseHeight)) {
            p.text = "no glyph";
            p.face = kk::FeedbackFace::Latin;
            p.tone = kk::FeedbackTone::Warn;
        }
    }

    FeedbackMeasureCtx ctx{&d, &pack};
    const kk::FeedbackLayout layout =
        kk::layoutFeedback(pieces, w, kk::FeedbackMeasure{&measureFeedbackSpan, &ctx});
    const bool scrolls = kk::feedbackScrollable(layout.height, h);
    const int top = scroll ? kk::clampFeedbackScroll(*scroll, layout.height, h) : 0;
    if (scroll) *scroll = top;

    d.fillRect(x, y, w, h, bg);
    d.setClipRect(x, y, w, h);
    const kk::ClipRect clip{x, y, x + w, y + h};
    for (const kk::FeedbackRow& row : layout.rows) {
        if (!kk::feedbackRowVisible(row, top, h)) continue;
        const int rowY = y + row.top - top;
        int pen = x;
        for (size_t k = 0; k < row.count; ++k) {
            const kk::FeedbackSegment& seg = layout.segments[row.first + k];
            const kk::FeedbackPiece& piece = pieces[seg.piece];
            const uint16_t fg = feedbackColor(piece.tone);
            if (seg.gapWidth > 0) {
                // The gap is Font0 text, centred on a Japanese row.
                ui::applyFont(d, ui::UiFont::Small);
                d.setTextColor(fg, bg);
                d.drawString(piece.gap.c_str(), pen,
                             rowY + (row.height - kk::kFeedbackLatinHeight) / 2);
                pen += seg.gapWidth;
            }
            const std::string text = piece.text.substr(seg.begin, seg.end - seg.begin);
            if (!text.empty()) {
                if (piece.face == kk::FeedbackFace::Japanese) {
                    pack.drawText(d, text, pen, rowY + kk::kFeedbackJapaneseHeight,
                                  kk::kFeedbackJapaneseHeight, fg, bg, clip);
                } else {
                    ui::applyFont(d, ui::UiFont::Small);
                    d.setTextColor(fg, bg);
                    d.drawString(text.c_str(), pen, rowY);
                }
            }
            pen += seg.width;
        }
    }
    d.clearClipRect();

    if (scrolls) {
        // The ListView track (2 px, kLine) with the thumb (kAccentHi).
        const int trackX = x + w + 2;
        d.fillRect(trackX, y, 2, h, kLine);
        const kk::FeedbackThumb thumb = kk::feedbackThumb(layout.height, h, top, h);
        d.fillRect(trackX, y + thumb.y, 2, thumb.height, kAccentHi);
    }
    return scrolls;
}

}  // namespace kk::ui

// Shared drawing widgets used by the Review and Lesson screens. View code:
// the caller owns startWrite()/present() and passes the font and radical packs.
#include "ui/Widgets.h"

#include "ui/UiFont.h"

#include <algorithm>

#include "wk/Utf8.h"

namespace kk::ui {

void drawPill(lgfx::LovyanGFX& d, int x, int y, const char* text, uint16_t fg, uint16_t bg) {
    ui::applyFont(d, ui::UiFont::Small);
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

int drawReadingTokens(lgfx::LovyanGFX& d, kk::GlyphPack& pack,
                      const std::vector<std::string>& tokens, int x, int y, int maxWidth,
                      int maxLines, uint16_t fg, uint16_t bg, const kk::ClipRect& clip) {
    if (tokens.empty() || maxLines < 1) return 0;

    // Font0 separators keep the pack from needing ASCII glyphs. Four spaces
    // separate the on'yomi and kun'yomi groups, more than the " / " gap.
    ui::applyFont(d, ui::UiFont::Small);
    const int spaceW = d.textWidth(" ");
    const int slashW = d.textWidth("/");
    const int slashGap = spaceW + slashW + spaceW;
    const int groupGap = spaceW * 4;

    enum Sep : uint8_t { None, Slash, Group };
    struct Entry {
        std::string text;
        Sep sep = None;
    };
    std::vector<Entry> entries;
    bool first = true;
    bool pendingBreak = false;
    for (const std::string& token : tokens) {
        if (token.empty()) {
            pendingBreak = true;
            continue;
        }
        const int tokenW = pack.textWidth(token, 20);
        if (tokenW < 0) return -1;
        if (tokenW <= maxWidth) {
            entries.push_back({token, first ? None : (pendingBreak ? Group : Slash)});
            first = false;
            pendingBreak = false;
            continue;
        }
        // A reading longer than one line wraps between its kana instead of
        // being reported as a missing glyph.
        const char* p = token.data();
        const char* end = p + token.size();
        bool firstPart = true;
        while (p < end) {
            const char* start = p;
            const uint32_t cp = kk::utf8Next(&p);
            if (cp == 0) break;
            entries.push_back({std::string(start, static_cast<size_t>(p - start)),
                               firstPart && !first ? (pendingBreak ? Group : Slash) : None});
            firstPart = false;
        }
        first = false;
        pendingBreak = false;
    }
    if (entries.empty()) return 0;

    std::vector<std::vector<size_t>> lines(1);
    int width = 0;
    bool truncated = false;
    auto sepWidth = [&](const Entry& entry) {
        return entry.sep == Group ? groupGap : entry.sep == Slash ? slashGap : 0;
    };
    for (size_t i = 0; i < entries.size(); ++i) {
        const int sep = lines.back().empty() ? 0 : sepWidth(entries[i]);
        const int entryW = pack.textWidth(entries[i].text, 20);
        if (!lines.back().empty() && width + sep + entryW > maxWidth) {
            if (static_cast<int>(lines.size()) >= maxLines) {
                truncated = true;
                break;
            }
            lines.emplace_back();
            width = 0;
        }
        width += (lines.back().empty() ? 0 : sepWidth(entries[i])) + entryW;
        lines.back().push_back(i);
    }

    const std::string ellipsis = "...";
    const int dotW = d.textWidth(ellipsis.c_str());
    auto lineWidth = [&](const std::vector<size_t>& line) {
        int used = 0;
        for (size_t k = 0; k < line.size(); ++k)
            used += (k == 0 ? 0 : sepWidth(entries[line[k]])) +
                    pack.textWidth(entries[line[k]].text, 20);
        return used;
    };
    if (truncated) {
        while (!lines.back().empty() && lineWidth(lines.back()) + spaceW + dotW > maxWidth)
            lines.back().pop_back();
    }

    for (size_t line = 0; line < lines.size(); ++line) {
        const int top = y + static_cast<int>(line) * 20;
        int pen = x;
        for (size_t k = 0; k < lines[line].size(); ++k) {
            const Entry& entry = entries[lines[line][k]];
            if (k) {
                if (entry.sep == Group) {
                    pen += groupGap;
                } else if (entry.sep == Slash) {
                    pen += spaceW;
                    d.setTextColor(fg, bg);
                    d.drawString("/", pen, top + 8);
                    pen += slashW + spaceW;
                }
            }
            const int avail = x + maxWidth - pen;
            const uint8_t size =
                pack.drawTextFit(d, entry.text, pen, top + 20, 20, avail, fg, bg, clip);
            if (size == 0) return -1;
            pen += pack.textWidth(entry.text, size);
        }
        if (truncated && line + 1 == lines.size()) {
            ui::applyFont(d, ui::UiFont::Small);
            d.setTextColor(fg, bg);
            d.drawString(ellipsis.c_str(), pen + (lines[line].empty() ? 0 : spaceW), top + 8);
        }
    }
    return static_cast<int>(lines.size());
}

}  // namespace kk::ui

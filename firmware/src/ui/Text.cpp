// Shared wrapped-text drawing. The app and the screens call this with the
// caller-owned transfer window already open.
#include "ui/Text.h"

namespace kk::ui {
namespace {

struct Piece {
    std::string text;
    uint16_t color = 0;
    uint8_t spacesBefore = 0;
    // Separator drawn before this word: none, " / " between answers, or "  + "
    // before the first synonym. A word that starts a line drops its separator
    // so a break never leaves a leading slash or plus.
    enum Sep : uint8_t { None, Slash, Plus } sep = None;
};

// Splits one answer or synonym into words so wrapping never breaks a word.
// Returns the index of the last word added, or npos when the text was empty.
size_t addWords(std::vector<Piece>* pieces, const std::string& text, uint16_t color,
                Piece::Sep sep, uint8_t spacesBefore) {
    size_t last = std::string::npos;
    size_t i = 0;
    bool first = true;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
        size_t j = i;
        while (j < text.size() && text[j] != ' ' && text[j] != '\t') ++j;
        if (j == i) break;
        Piece p;
        p.text = text.substr(i, j - i);
        p.color = color;
        p.spacesBefore = first ? spacesBefore : 1;
        p.sep = first ? sep : Piece::None;
        pieces->push_back(std::move(p));
        last = pieces->size() - 1;
        first = false;
        i = j;
    }
    return last;
}

// Removes whole UTF-8 code points from the end until the text fits maxWidth.
void trimBackToWidth(lgfx::LovyanGFX& d, std::string& text, int maxWidth) {
    if (maxWidth <= 0) {
        text.clear();
        return;
    }
    while (!text.empty() && d.textWidth(text.c_str()) > maxWidth) {
        text.pop_back();
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80)
            text.pop_back();
    }
}

}  // namespace

void drawWrapped(lgfx::LovyanGFX& d, const std::string& text, int x, int y, int w, int maxLines,
                 uint16_t fg, uint16_t bg, bool ellipsis) {
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(fg, bg);
    size_t start = 0;
    size_t lastStart = 0;
    size_t lastEnd = 0;
    int lines = 0;
    for (int line = 0; line < maxLines && start < text.size(); ++line) {
        size_t end = text.size();
        while (end > start && d.textWidth(text.substr(start, end - start).c_str()) > w) --end;
        if (end < text.size()) {
            const size_t sp = text.rfind(' ', end);
            if (sp != std::string::npos && sp > start) end = sp;
        }
        d.drawString(text.substr(start, end - start).c_str(), x, y + line * 8);
        lastStart = start;
        lastEnd = end;
        ++lines;
        start = end;
        while (start < text.size() && text[start] == ' ') ++start;
    }
    if (ellipsis && lines > 0 && start < text.size()) {
        // Replace the last drawn line with a shortened one ending in "...".
        const int lastY = y + (lines - 1) * 8;
        d.fillRect(x, lastY, w, 8, bg);
        std::string shown = text.substr(lastStart, lastEnd - lastStart);
        const std::string dots = "...";
        const int reserve = d.textWidth(dots.c_str()) + d.textWidth(" ");
        while (!shown.empty() && d.textWidth(shown.c_str()) > w - reserve) {
            shown.pop_back();
            while (!shown.empty() && (static_cast<unsigned char>(shown.back()) & 0xC0) == 0x80)
                shown.pop_back();
        }
        while (!shown.empty() && shown.back() == ' ') shown.pop_back();
        if (!shown.empty()) shown += " ";
        shown += dots;
        d.setTextColor(fg, bg);
        d.drawString(shown.c_str(), x, lastY);
    }
}

void drawAnswerTokens(lgfx::LovyanGFX& d, const std::vector<std::string>& answers,
                      const std::vector<std::string>& synonyms, int x, int y, int w,
                      int maxLines, uint16_t fg, uint16_t synonymFg, uint16_t bg) {
    if (maxLines < 1) return;
    d.setTextFont(0);
    d.setTextSize(1);

    std::vector<Piece> pieces;
    bool anyAnswer = false;
    for (const std::string& answer : answers) {
        if (answer.empty()) continue;
        addWords(&pieces, answer, fg, anyAnswer ? Piece::Slash : Piece::None,
                 anyAnswer ? 1 : 0);
        anyAnswer = true;
    }
    std::vector<size_t> shownSynonym;
    for (size_t i = 0; i < synonyms.size(); ++i)
        if (!synonyms[i].empty()) shownSynonym.push_back(i);
    for (size_t k = 0; k < shownSynonym.size(); ++k) {
        const bool firstSynonym = k == 0;
        const Piece::Sep sep = firstSynonym ? (anyAnswer ? Piece::Plus : Piece::None) : Piece::None;
        const uint8_t spaces = firstSynonym && anyAnswer ? 2 : (anyAnswer || k > 0 ? 1 : 0);
        const size_t last = addWords(&pieces, synonyms[shownSynonym[k]], synonymFg, sep, spaces);
        // Comma after every synonym but the last one.
        if (last != std::string::npos && k + 1 < shownSynonym.size())
            pieces[last].text += ",";
    }
    if (pieces.empty()) return;

    const int spaceW = d.textWidth(" ");
    const int slashW = d.textWidth("/");
    const int plusW = d.textWidth("+");
    auto sepWidth = [&](const Piece& p) {
        return p.sep == Piece::Slash ? slashW + spaceW
             : p.sep == Piece::Plus ? plusW + spaceW
                                    : 0;
    };
    auto pieceWidth = [&](const Piece& p, bool firstInLine) {
        return p.text.size() == 0 ? 0
               : (firstInLine ? 0 : p.spacesBefore * spaceW + sepWidth(p)) +
                     d.textWidth(p.text.c_str());
    };

    std::vector<std::vector<size_t>> lines(1);
    int width = 0;
    bool truncated = false;
    for (size_t i = 0; i < pieces.size(); ++i) {
        const int advance = pieceWidth(pieces[i], lines.back().empty());
        if (!lines.back().empty() && width + advance > w) {
            if (static_cast<int>(lines.size()) >= maxLines) {
                truncated = true;
                break;
            }
            lines.emplace_back();
            width = 0;
        }
        width += pieceWidth(pieces[i], lines.back().empty());
        lines.back().push_back(i);
    }

    const std::string ellipsis = "...";
    const int dotW = d.textWidth(ellipsis.c_str());
    auto lineWidth = [&](const std::vector<size_t>& line) {
        int used = 0;
        for (size_t k = 0; k < line.size(); ++k)
            used += pieceWidth(pieces[line[k]], k == 0);
        return used;
    };
    if (truncated) {
        while (!lines.back().empty() && lineWidth(lines.back()) + spaceW + dotW > w)
            lines.back().pop_back();
        // Do not leave a trailing comma on the last synonym shown.
        if (!lines.back().empty()) {
            Piece& p = pieces[lines.back().back()];
            if (!p.text.empty() && p.text.back() == ',' && p.color == synonymFg)
                p.text.pop_back();
        }
    }

    for (size_t line = 0; line < lines.size(); ++line) {
        int pen = x;
        const int lineY = y + static_cast<int>(line) * 8;
        for (size_t k = 0; k < lines[line].size(); ++k) {
            Piece& p = pieces[lines[line][k]];
            if (k) {
                pen += p.spacesBefore * spaceW;
                if (p.sep == Piece::Slash) {
                    d.setTextColor(fg, bg);
                    d.drawString("/", pen, lineY);
                    pen += slashW + spaceW;
                } else if (p.sep == Piece::Plus) {
                    d.setTextColor(synonymFg, bg);
                    d.drawString("+", pen, lineY);
                    pen += plusW + spaceW;
                }
            }
            std::string text = p.text;
            trimBackToWidth(d, text, x + w - pen);
            d.setTextColor(p.color, bg);
            d.drawString(text.c_str(), pen, lineY);
            pen += d.textWidth(text.c_str());
        }
        if (truncated && line + 1 == lines.size()) {
            const int dotX = pen + (lines[line].empty() ? 0 : spaceW);
            d.setTextColor(lines[line].empty() ? fg : pieces[lines[line].back()].color, bg);
            d.drawString(ellipsis.c_str(), dotX, lineY);
        }
    }
}

}  // namespace kk::ui

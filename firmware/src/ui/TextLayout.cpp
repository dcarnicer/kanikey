// Pure text layout. The width callback is the only font dependency, so the
// 6x8 Font0 path and the scaled glyph path share the same wrap and truncation.
#include "ui/TextLayout.h"

namespace kk::ui {
namespace {

// A negative measured width means "cannot be drawn" (GlyphPack uses -1 for a
// missing glyph), so it never counts as fitting.
bool fits(int width, int maxWidth) { return width >= 0 && width <= maxWidth; }

size_t advance(std::string_view text, size_t i) {
    if (i >= text.size()) return text.size();
    const unsigned char lead = static_cast<unsigned char>(text[i]);
    size_t n = 1;
    if ((lead & 0xE0) == 0xC0) n = 2;
    else if ((lead & 0xF0) == 0xE0) n = 3;
    else if ((lead & 0xF8) == 0xF0) n = 4;
    if (i + n > text.size()) n = 1;  // truncated sequence: step one byte
    return i + n;
}

size_t retreat(std::string_view text, size_t i) {
    if (i == 0) return 0;
    --i;
    while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) --i;
    return i;
}

bool hasText(std::string_view text, size_t from, WrapMode mode) {
    if (mode == WrapMode::Characters) return from < text.size();
    while (from < text.size() && text[from] == ' ') ++from;
    return from < text.size();
}

// One line starting at or after `start`. Words mode drops leading spaces, so
// callers never draw a line that begins with the space consumed by a break.
TextLine chooseLine(std::string_view text, size_t start, int maxWidth,
                    const TextMeasure& measure, WrapMode mode) {
    size_t begin = start;
    if (mode == WrapMode::Words)
        while (begin < text.size() && text[begin] == ' ') ++begin;
    if (begin >= text.size()) return {begin, begin};

    size_t end = fitPrefix(text, begin, text.size(), maxWidth, measure);
    if (end == begin) end = advance(text, begin);

    if (mode == WrapMode::Words && end < text.size()) {
        // Break before the last space in the fitting prefix. With no space
        // (a word wider than the line) keep the code-point split.
        size_t at = end;
        while (at > begin) {
            const size_t prev = retreat(text, at);
            if (text[prev] == ' ') {
                if (prev > begin) end = prev;
                break;
            }
            at = prev;
        }
    }

    if (mode == WrapMode::Words)
        while (end > begin && text[end - 1] == ' ') --end;
    if (end <= begin) end = advance(text, begin);
    return {begin, end};
}

}  // namespace

size_t fitPrefix(std::string_view text, size_t begin, size_t end, int maxWidth,
                 const TextMeasure& measure) {
    if (end > text.size()) end = text.size();
    if (begin > end) begin = end;
    if (begin >= end || maxWidth < 0) return begin;
    size_t fit = begin;
    while (fit < end) {
        const size_t next = advance(text, fit);
        if (!fits(measure(text.substr(begin, next - begin)), maxWidth)) break;
        fit = next;
    }
    return fit;
}

size_t fitSuffix(std::string_view text, size_t begin, size_t end, int maxWidth,
                 const TextMeasure& measure) {
    if (end > text.size()) end = text.size();
    if (begin > end) begin = end;
    if (begin >= end || maxWidth < 0) return end;
    size_t start = end;
    while (start > begin) {
        const size_t prev = retreat(text, start);
        if (!fits(measure(text.substr(prev, end - prev)), maxWidth)) break;
        start = prev;
    }
    return start;
}

WrapResult wrapLines(std::string_view text, int maxWidth, const TextMeasure& measure,
                     TextLine* out, int maxLines, WrapMode mode) {
    WrapResult result;
    if (maxLines <= 0) {
        result.truncated = hasText(text, 0, mode);
        return result;
    }

    size_t start = 0;
    while (start < text.size() && result.lineCount < maxLines) {
        const TextLine line = chooseLine(text, start, maxWidth, measure, mode);
        if (line.end <= line.begin) break;
        if (out) out[result.lineCount] = line;
        ++result.lineCount;
        start = line.end;
    }
    result.truncated = hasText(text, start, mode);
    return result;
}

int wrapLineCount(std::string_view text, int maxWidth, const TextMeasure& measure,
                  WrapMode mode) {
    int count = 0;
    size_t start = 0;
    while (start < text.size()) {
        const TextLine line = chooseLine(text, start, maxWidth, measure, mode);
        if (line.end <= line.begin) break;
        ++count;
        start = line.end;
    }
    return count;
}

std::string ellipsize(std::string_view text, int maxWidth, const TextMeasure& measure,
                      std::string_view dots, bool force) {
    if (text.empty()) return force ? std::string(dots) : std::string();
    if (!force && fits(measure(text), maxWidth)) return std::string(text);

    const int dotsWidth = measure(dots);
    const int spaceWidth = measure(" ");
    const int reserve = (dotsWidth > 0 ? dotsWidth : 0) + (spaceWidth > 0 ? spaceWidth : 0);
    const int limit = maxWidth - reserve;

    std::string shown(text);
    while (!shown.empty() && !fits(measure(shown), limit))
        shown.resize(retreat(shown, shown.size()));
    while (!shown.empty() && shown.back() == ' ') shown.pop_back();
    if (!shown.empty()) shown += ' ';
    shown.append(dots);
    return shown;
}

}  // namespace kk::ui

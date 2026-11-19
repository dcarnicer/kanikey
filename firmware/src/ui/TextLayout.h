#pragma once
// Pure text layout: wrapping, line counting and width-aware truncation. The
// caller supplies a width callback, so the same code serves the 6x8 Font0 path
// (LovyanGFX textWidth) and the scaled glyph path (GlyphPack::textWidth).
// No display or Arduino headers here: this file is native-tested.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace kk::ui {

// Measures a byte range of UTF-8 text in pixels. `text` may be empty.
using WidthFn = int (*)(void* ctx, std::string_view text);

// A width callback and its context. A null callback measures every span as 0.
struct TextMeasure {
    WidthFn fn = nullptr;
    void* ctx = nullptr;

    int operator()(std::string_view text) const { return fn ? fn(ctx, text) : 0; }
};

enum class WrapMode : uint8_t {
    Words,       // break at spaces; split a word only when it cannot fit
    Characters,  // break between UTF-8 code points
};

// One [begin, end) span of the source string.
struct TextLine {
    size_t begin = 0;
    size_t end = 0;

    size_t length() const { return end > begin ? end - begin : 0; }
};

struct WrapResult {
    int lineCount = 0;
    bool truncated = false;  // text remains after the last line
};

// Wraps `text` into at most `maxLines` lines no wider than `maxWidth` pixels.
// Writes the spans to `out` when it is not null; `out` must have room for
// `maxLines` entries. Words mode drops the spaces at a break and at the start
// of each line; a word wider than the line is split between code points.
// Characters mode keeps every code point, including spaces. A line always
// makes progress, even when a single code point is wider than maxWidth.
WrapResult wrapLines(std::string_view text, int maxWidth, const TextMeasure& measure,
                     TextLine* out, int maxLines,
                     WrapMode mode = WrapMode::Words);

// The number of lines wrapLines() would produce without a line limit. Returns 0
// for empty (or all-space Words-mode) text.
int wrapLineCount(std::string_view text, int maxWidth, const TextMeasure& measure,
                  WrapMode mode = WrapMode::Words);

// Longest prefix of text[begin, end) that fits maxWidth, rounded down to a
// UTF-8 code-point boundary. Equals `begin` when not even one code point fits.
size_t fitPrefix(std::string_view text, size_t begin, size_t end, int maxWidth,
                 const TextMeasure& measure);

// Start offset of the longest suffix of text[begin, end) that fits maxWidth,
// rounded up to a UTF-8 code-point boundary. Equals `end` when nothing fits.
size_t fitSuffix(std::string_view text, size_t begin, size_t end, int maxWidth,
                 const TextMeasure& measure);

// Trims `text` to maxWidth on a code-point boundary and appends `dots`. `force`
// reserves room for the dots even when the text already fits (the caller knows
// more text follows); a non-empty prefix gets one space before the dots. When
// even the reserved dots do not fit, returns just the dots.
std::string ellipsize(std::string_view text, int maxWidth, const TextMeasure& measure,
                      std::string_view dots = "...", bool force = false);

}  // namespace kk::ui

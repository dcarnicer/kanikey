#pragma once
// Feedback body (REV-01/REV-03, review and lesson quiz): the typed text, the
// accepted answers, the synonyms and the note lines after an answer, turned
// into wrapped rows and a scroll window. Pure: no display headers, so it is
// native-tested. Widths come from a callback: the device measures Font0 with
// textWidth() for Latin and the glyph pack at 20 px for Japanese; the tests use
// a fixed width per code point.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "wk/AnswerChecker.h"
#include "wk/Types.h"

namespace kk {

// Latin is Font0 (8 px rows); Japanese is a pack glyph at 20 px. A row never
// mixes the two faces.
enum class FeedbackFace : uint8_t { Latin, Japanese };

// A colour slot. The drawer maps it to the palette; this header has no colours.
enum class FeedbackTone : uint8_t { Label, Typed, Right, Wrong, Note, Synonym, Warn };

constexpr int kFeedbackLatinHeight = 8;
constexpr int kFeedbackJapaneseHeight = 20;
// One Up/Down press scrolls one Font0 line.
constexpr int kFeedbackScrollStep = 8;

struct FeedbackPiece {
    std::string text;
    FeedbackFace face = FeedbackFace::Latin;
    FeedbackTone tone = FeedbackTone::Note;
    // Latin separator drawn before the piece when it does not open a row (for
    // example " / " between answers, "    " between kanji reading groups).
    std::string gap;
    // Always opens a new row (section labels, each note).
    bool newline = false;
};

// Width of `text` in pixels for `face`. Must be >= 0 for every span passed.
using FeedbackWidthFn = int (*)(void* ctx, FeedbackFace face, std::string_view text);

struct FeedbackMeasure {
    FeedbackWidthFn fn = nullptr;
    void* ctx = nullptr;

    int operator()(FeedbackFace face, std::string_view text) const {
        return fn ? fn(ctx, face, text) : 0;
    }
};

// One drawn span of a piece. A piece wider than a row is split over segments.
struct FeedbackSegment {
    size_t piece = 0;  // index into the pieces
    size_t begin = 0;  // byte span of the piece's text drawn here
    size_t end = 0;
    int gapWidth = 0;  // width of the piece's gap drawn before this span (0 = none)
    int width = 0;     // width of the span's text
};

struct FeedbackRow {
    size_t first = 0;  // index of the row's first segment
    size_t count = 0;  // segments in the row
    FeedbackFace face = FeedbackFace::Latin;
    int top = 0;     // y of the row relative to the top of the body
    int height = 0;  // 8 for Latin, 20 for Japanese
};

struct FeedbackLayout {
    std::vector<FeedbackSegment> segments;
    std::vector<FeedbackRow> rows;
    int height = 0;  // total height of all rows
};

// Wraps the pieces into rows no wider than maxWidth. Latin wraps between words:
// a piece that does not fit the rest of a row moves whole to the next row, and
// is split by code point only when it is wider than an empty row. Japanese
// splits by code point. A code point wider than a row still gets its own row,
// so the layout always makes progress.
FeedbackLayout layoutFeedback(const std::vector<FeedbackPiece>& pieces, int maxWidth,
                              const FeedbackMeasure& measure);

// The largest scroll offset that keeps the box full; 0 when the content fits.
int feedbackMaxScroll(int contentHeight, int viewHeight);

// `scroll` clamped to [0, feedbackMaxScroll()].
int clampFeedbackScroll(int scroll, int contentHeight, int viewHeight);

// True when the content is taller than the box: the hint then names the scroll
// keys, and only then.
inline bool feedbackScrollable(int contentHeight, int viewHeight) {
    return contentHeight > viewHeight;
}

// The scroll offset after one Up (down = false) or Down press. Never negative;
// the upper bound is applied by clampFeedbackScroll() when the body is drawn.
inline int feedbackScrollKey(int scroll, bool down) {
    const int next = scroll + (down ? kFeedbackScrollStep : -kFeedbackScrollStep);
    return next < 0 ? 0 : next;
}

// Whether a row overlaps the window [scroll, scroll + viewHeight).
inline bool feedbackRowVisible(const FeedbackRow& row, int scroll, int viewHeight) {
    return row.top + row.height > scroll && row.top < scroll + viewHeight;
}

struct FeedbackThumb {
    int y = 0;
    int height = 0;
};

// The scroll thumb in a track trackHeight px tall, with ListView's rule: the
// thumb is proportional to the visible share, at least 10 px.
FeedbackThumb feedbackThumb(int contentHeight, int viewHeight, int scroll, int trackHeight);

// What the feedback body shows, before wrapping.
struct FeedbackContent {
    bool japanese = false;  // reading part: "Y" / "A" labels and kana tokens
    bool warning = false;   // CHECK: only the notes, never the typed or answer rows
    bool correct = false;   // answer tokens in the Right tone, else Wrong
    std::string typed;
    // Accepted answers in display order. Reading lists may hold an empty token
    // as an on'yomi/kun'yomi group break.
    std::vector<std::string> answers;
    std::vector<std::string> synonyms;  // drawn after the answers, with "+"
    std::vector<std::string> notes;     // one line each, after the answers
};

// The pieces for one feedback frame. A normal frame is "You typed" + typed,
// "Answer" + the accepted list (synonyms after "+"), then the notes. Every
// section starts a new row, so no text is cut to a fixed number of lines.
// Latin text splits into words and Japanese runs into their own pieces.
std::vector<FeedbackPiece> buildFeedbackPieces(const FeedbackContent& content);

// The REV-03 note lines under the answers. A correct answer keeps the checker
// line (for example the close note) and the user's note; a wrong answer shows
// the first sentence of the part's hint, else the user's note; a warning shows
// only the checker message, which never names the answer. Empty without a
// subject, except the warning message.
// `hideKana`: the meaning feedback of an item whose reading is still to be
// asked. WaniKani hints sometimes spell the reading in kana (and kana now draw
// at 20 px), so any line with kana is left out: the hint falls back to the note
// and a note with kana is dropped (owner report, 2026-11-20).
std::vector<std::string> feedbackNoteLines(const Subject* subject, bool meaning,
                                           AnswerStatus status, const std::string& message,
                                           bool hideKana = false);

// True when `text` holds hiragana or katakana.
bool containsKana(std::string_view text);

}  // namespace kk

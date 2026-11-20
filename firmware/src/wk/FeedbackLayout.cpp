// Feedback body layout. Pure: native-tested in test_wk.
#include "wk/FeedbackLayout.h"

#include <algorithm>

#include "wk/Text.h"

namespace kk {
namespace {

// Byte length of the UTF-8 sequence at i. A stray or truncated lead byte counts
// as one byte, so the scan always moves forward.
size_t seqLen(std::string_view text, size_t i) {
    if (i >= text.size()) return 0;
    const unsigned char lead = static_cast<unsigned char>(text[i]);
    size_t n = 1;
    if ((lead & 0xE0) == 0xC0) n = 2;
    else if ((lead & 0xF0) == 0xE0) n = 3;
    else if ((lead & 0xF8) == 0xF0) n = 4;
    if (i + n > text.size()) n = 1;
    return n;
}

uint32_t codePointAt(std::string_view text, size_t i, size_t n) {
    const unsigned char lead = static_cast<unsigned char>(text[i]);
    if (n == 1) return lead;
    uint32_t cp = n == 2 ? (lead & 0x1Fu) : n == 3 ? (lead & 0x0Fu) : (lead & 0x07u);
    for (size_t k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3Fu);
    return cp;
}

// Hiragana, katakana, CJK punctuation and ideographs, full-width forms: the
// characters the glyph pack draws at 20 px.
bool isJapanese(uint32_t cp) {
    return (cp >= 0x3000 && cp <= 0x30FF) || (cp >= 0x31F0 && cp <= 0x31FF) ||
           (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) ||
           (cp >= 0xFF00 && cp <= 0xFFEF);
}

// Hiragana, katakana (and its phonetic extensions and half-width forms): a
// reading. Kanji alone is not one.
bool isKana(uint32_t cp) {
    return (cp >= 0x3041 && cp <= 0x30FF) || (cp >= 0x31F0 && cp <= 0x31FF) ||
           (cp >= 0xFF66 && cp <= 0xFF9D);
}

// End of the longest prefix text[begin, end) that measures <= avail, on a code
// point boundary. Returns begin when not even one code point fits.
size_t fitEnd(std::string_view text, size_t begin, int avail, FeedbackFace face,
              const FeedbackMeasure& measure) {
    size_t best = begin;
    size_t i = begin;
    while (i < text.size()) {
        const size_t next = i + seqLen(text, i);
        if (measure(face, text.substr(begin, next - begin)) > avail) break;
        best = next;
        i = next;
    }
    return best;
}

bool blank(std::string_view text) { return text.find_first_not_of(' ') == std::string_view::npos; }

// Adds the words of `text` as pieces: a Latin word per piece, a Japanese run
// per piece, a single space between words. The first piece takes `firstGap`
// and `newline`; later pieces take the space or no gap.
void appendText(std::vector<FeedbackPiece>& out, std::string_view text, FeedbackTone tone,
                bool newline, std::string_view firstGap) {
    bool open = false;
    bool any = false;
    bool pendingSpace = false;
    FeedbackPiece cur;
    size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (c == ' ' || c == '\t') {
            pendingSpace = true;
            ++i;
            continue;
        }
        const size_t n = seqLen(text, i);
        const FeedbackFace face =
            isJapanese(codePointAt(text, i, n)) ? FeedbackFace::Japanese : FeedbackFace::Latin;
        if (!open || cur.face != face || (face == FeedbackFace::Latin && pendingSpace)) {
            if (open) out.push_back(cur);
            cur = FeedbackPiece{};
            cur.face = face;
            cur.tone = tone;
            if (!any) {
                cur.gap = std::string(firstGap);
                cur.newline = newline;
            } else {
                cur.gap = pendingSpace ? " " : "";
            }
            any = true;
            open = true;
        }
        cur.text.append(text.substr(i, n));
        pendingSpace = false;
        i += n;
    }
    if (open) out.push_back(cur);
}

}  // namespace

FeedbackLayout layoutFeedback(const std::vector<FeedbackPiece>& pieces, int maxWidth,
                              const FeedbackMeasure& measure) {
    FeedbackLayout out;
    FeedbackRow row;
    bool open = false;
    int used = 0;

    auto close = [&]() {
        if (!open) return;
        row.height = row.face == FeedbackFace::Japanese ? kFeedbackJapaneseHeight
                                                        : kFeedbackLatinHeight;
        row.top = out.height;
        out.height += row.height;
        out.rows.push_back(row);
        open = false;
        used = 0;
    };
    auto add = [&](size_t piece, size_t begin, size_t end, int gap, int width,
                   FeedbackFace face) {
        if (!open) {
            row = FeedbackRow{};
            row.first = out.segments.size();
            row.face = face;
            open = true;
        }
        out.segments.push_back(FeedbackSegment{piece, begin, end, gap, width});
        ++row.count;
        used += gap + width;
    };

    for (size_t p = 0; p < pieces.size(); ++p) {
        const FeedbackPiece& piece = pieces[p];
        if (piece.newline) close();
        const std::string_view text = piece.text;
        if (text.empty()) {
            // An empty piece is a zero-width span; it can open a row (a spacer).
            if (open && row.face != piece.face) close();
            add(p, 0, 0, 0, 0, piece.face);
            continue;
        }

        size_t pos = 0;
        bool firstPart = true;
        while (pos < text.size()) {
            if (open && row.face != piece.face) close();
            // The gap is drawn only when the piece continues the current row.
            const int gap = open && firstPart ? measure(FeedbackFace::Latin, piece.gap) : 0;
            const int avail = maxWidth - used - gap;
            const size_t end = fitEnd(text, pos, avail, piece.face, measure);

            if (end == text.size()) {
                add(p, pos, end, gap,
                    measure(piece.face, text.substr(pos, end - pos)), piece.face);
                break;
            }
            // Latin words move whole to the next row; a Japanese run fills the row
            // by code point. Nothing fits here: start a fresh row first.
            if (open && (piece.face == FeedbackFace::Latin || end == pos)) {
                close();
                continue;  // the gap is dropped on the fresh row
            }
            // Fresh row. If nothing fits even here, take one code point so the
            // row always makes progress.
            const size_t cut = end == pos ? pos + seqLen(text, pos) : end;
            add(p, pos, cut, gap, measure(piece.face, text.substr(pos, cut - pos)), piece.face);
            pos = cut;
            firstPart = false;
            close();
        }
    }
    close();
    return out;
}

int feedbackMaxScroll(int contentHeight, int viewHeight) {
    return std::max(0, contentHeight - viewHeight);
}

int clampFeedbackScroll(int scroll, int contentHeight, int viewHeight) {
    return std::min(std::max(scroll, 0), feedbackMaxScroll(contentHeight, viewHeight));
}

FeedbackThumb feedbackThumb(int contentHeight, int viewHeight, int scroll, int trackHeight) {
    FeedbackThumb thumb;
    thumb.height = trackHeight;
    if (!feedbackScrollable(contentHeight, viewHeight) || viewHeight <= 0 || trackHeight <= 0)
        return thumb;
    thumb.height = std::min(trackHeight, std::max(10, trackHeight * viewHeight / contentHeight));
    const int maxScroll = feedbackMaxScroll(contentHeight, viewHeight);
    const int s = std::min(std::max(scroll, 0), maxScroll);
    thumb.y = maxScroll > 0 ? (trackHeight - thumb.height) * s / maxScroll : 0;
    return thumb;
}

std::vector<FeedbackPiece> buildFeedbackPieces(const FeedbackContent& c) {
    std::vector<FeedbackPiece> out;
    if (c.warning) {
        for (const std::string& note : c.notes)
            if (!blank(note)) appendText(out, note, FeedbackTone::Note, true, "");
        return out;
    }

    appendText(out, c.japanese ? "Y" : "You typed", FeedbackTone::Label, true, "");
    appendText(out, blank(c.typed) ? "-" : c.typed, FeedbackTone::Typed, true, "");
    appendText(out, c.japanese ? "A" : "Answer", FeedbackTone::Label, true, "");

    const FeedbackTone answerTone = c.correct ? FeedbackTone::Right : FeedbackTone::Wrong;
    bool first = true;
    bool groupBreak = false;
    for (const std::string& token : c.answers) {
        if (blank(token)) {
            groupBreak = true;  // an empty token starts the next kanji reading group
            continue;
        }
        const std::string_view gap = first ? "" : groupBreak ? "    " : " / ";
        appendText(out, token, answerTone, first, gap);
        first = false;
        groupBreak = false;
    }

    // Synonyms follow the answers after "+"; without answers they open a row.
    const bool anyAnswer = !first;
    std::vector<const std::string*> synonyms;
    for (const std::string& s : c.synonyms)
        if (!blank(s)) synonyms.push_back(&s);
    for (size_t k = 0; k < synonyms.size(); ++k) {
        const bool last = k + 1 == synonyms.size();
        const std::string text = *synonyms[k] + (last ? "" : ",");
        const std::string_view gap = k == 0 ? (anyAnswer ? "  + " : "") : " ";
        appendText(out, text, FeedbackTone::Synonym, k == 0 && !anyAnswer, gap);
    }

    for (const std::string& note : c.notes)
        if (!blank(note)) appendText(out, note, FeedbackTone::Note, true, "");
    return out;
}

bool containsKana(std::string_view text) {
    size_t i = 0;
    while (i < text.size()) {
        const size_t n = seqLen(text, i);
        if (isKana(codePointAt(text, i, n))) return true;
        i += n;
    }
    return false;
}

std::vector<std::string> feedbackNoteLines(const Subject* s, bool meaning, AnswerStatus status,
                                           const std::string& message, bool hideKana) {
    std::vector<std::string> lines;
    if (status == AnswerStatus::Warning) {
        if (!message.empty()) lines.push_back(message);
        return lines;
    }
    if (!s) return lines;
    const std::string& hint = meaning ? s->meaningHint : s->readingHint;
    const std::string& note = meaning ? s->meaningNote : s->readingNote;
    // A line that would show kana while the reading is still to be asked is
    // left out (the detail card still has it).
    const auto allowed = [hideKana](const std::string& text) {
        return !text.empty() && !(hideKana && containsKana(text));
    };
    if (status == AnswerStatus::Incorrect) {
        const std::string sentence = firstSentence(hint);
        if (allowed(sentence))
            lines.push_back(sentence);
        else if (allowed(note))
            lines.push_back(note);
        return lines;
    }
    // Correct: the checker line (REV-02 close note), then the user's note.
    if (!message.empty() && message != "Correct") lines.push_back(message);
    if (allowed(note)) lines.push_back((meaning ? "Note: " : "Reading note: ") + note);
    return lines;
}

}  // namespace kk

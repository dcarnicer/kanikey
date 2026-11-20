// Feedback body (REV-01/REV-03): wrapping of Latin words and Japanese runs, the
// answer and note pieces, the REV-03 note lines and the scroll window. Latin is
// measured at 6 px per code point and Japanese at 20 px, like the device fonts'
// fixed cells, so each expected width here is exact.
#include <unity.h>

#include <string>
#include <string_view>
#include <vector>

#include "wk/FeedbackLayout.h"
#include "wk/Types.h"

using namespace kk;

namespace {

int countCodePoints(std::string_view text) {
    int count = 0;
    for (char c : text)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++count;
    return count;
}

int measureFixed(void*, FeedbackFace face, std::string_view text) {
    const int per = face == FeedbackFace::Japanese ? 20 : 6;
    return countCodePoints(text) * per;
}

FeedbackMeasure fixedMeasure() { return FeedbackMeasure{&measureFixed, nullptr}; }

// Every drawn span in order. Nothing may be dropped or repeated.
std::string shownText(const std::vector<FeedbackPiece>& pieces, const FeedbackLayout& layout) {
    std::string out;
    for (const FeedbackSegment& seg : layout.segments)
        out += pieces[seg.piece].text.substr(seg.begin, seg.end - seg.begin);
    return out;
}

std::string withoutSpaces(std::string s) {
    std::string out;
    for (char c : s)
        if (c != ' ') out += c;
    return out;
}

FeedbackPiece piece(std::string text, FeedbackTone tone, bool newline = false,
                    std::string gap = "", FeedbackFace face = FeedbackFace::Latin) {
    FeedbackPiece p;
    p.text = std::move(text);
    p.tone = tone;
    p.newline = newline;
    p.gap = std::move(gap);
    p.face = face;
    return p;
}

}  // namespace

// ------------------------------------------------------------ layout

static void test_feedback_wraps_latin_by_word() {
    const std::vector<FeedbackPiece> pieces = {
        piece("Composition", FeedbackTone::Right, true),
        piece("Writing", FeedbackTone::Right, false, " "),
        piece("Essay", FeedbackTone::Right, false, " "),
    };
    // 80 px: "Composition" (66) fills row 0 and "Writing" moves whole to row 1,
    // where "Essay" joins it (42 + 6 + 30 = 78).
    const FeedbackLayout layout = layoutFeedback(pieces, 80, fixedMeasure());
    TEST_ASSERT_EQUAL_UINT(2, layout.rows.size());
    TEST_ASSERT_EQUAL_UINT(1, layout.rows[0].count);
    TEST_ASSERT_EQUAL_UINT(2, layout.rows[1].count);
    TEST_ASSERT_EQUAL(0, layout.rows[0].top);
    TEST_ASSERT_EQUAL(8, layout.rows[1].top);
    TEST_ASSERT_EQUAL(16, layout.height);
    // The gap before "Essay" is drawn; the gap before "Writing" was dropped at
    // the row start.
    TEST_ASSERT_EQUAL(0, layout.segments[1].gapWidth);
    TEST_ASSERT_EQUAL(6, layout.segments[2].gapWidth);
    TEST_ASSERT_EQUAL_STRING("CompositionWritingEssay", withoutSpaces(shownText(pieces, layout)).c_str());
}

static void test_feedback_splits_only_a_word_wider_than_a_row() {
    const std::vector<FeedbackPiece> pieces = {
        piece("Supercalifragilisticexpialidocious", FeedbackTone::Note, true),
    };
    const FeedbackLayout layout = layoutFeedback(pieces, 60, fixedMeasure());
    TEST_ASSERT_TRUE(layout.rows.size() >= 2);
    for (const FeedbackRow& row : layout.rows) {
        int width = 0;
        for (size_t k = 0; k < row.count; ++k) width += layout.segments[row.first + k].width;
        TEST_ASSERT_TRUE(width <= 60);
    }
    TEST_ASSERT_EQUAL_STRING("Supercalifragilisticexpialidocious", shownText(pieces, layout).c_str());
}

static void test_feedback_japanese_wraps_by_code_point() {
    // Seven kana at 20 px in a 60 px row: three, three, then one.
    const std::vector<FeedbackPiece> pieces = {
        piece("じょうせいうえ", FeedbackTone::Right, true, "", FeedbackFace::Japanese),
    };
    const FeedbackLayout layout = layoutFeedback(pieces, 60, fixedMeasure());
    TEST_ASSERT_EQUAL_UINT(3, layout.rows.size());
    TEST_ASSERT_TRUE(layout.rows[0].face == FeedbackFace::Japanese);
    TEST_ASSERT_EQUAL(20, layout.rows[0].height);
    TEST_ASSERT_EQUAL(20, layout.rows[1].top);
    TEST_ASSERT_EQUAL(40, layout.rows[2].top);
    TEST_ASSERT_EQUAL(60, layout.height);
    TEST_ASSERT_EQUAL(60, layout.segments[0].width);
    TEST_ASSERT_EQUAL(20, layout.segments[2].width);
    TEST_ASSERT_EQUAL_STRING("じょうせいうえ", shownText(pieces, layout).c_str());
}

static void test_feedback_japanese_and_latin_never_share_a_row() {
    const std::vector<FeedbackPiece> pieces = {
        piece("You typed", FeedbackTone::Label, true),
        piece("日本", FeedbackTone::Typed, true, "", FeedbackFace::Japanese),
        piece("abc", FeedbackTone::Note, true),
    };
    const FeedbackLayout layout = layoutFeedback(pieces, 200, fixedMeasure());
    TEST_ASSERT_EQUAL_UINT(3, layout.rows.size());
    TEST_ASSERT_TRUE(layout.rows[0].face == FeedbackFace::Latin);
    TEST_ASSERT_TRUE(layout.rows[1].face == FeedbackFace::Japanese);
    TEST_ASSERT_TRUE(layout.rows[2].face == FeedbackFace::Latin);
    TEST_ASSERT_EQUAL(8, layout.rows[1].top);
    TEST_ASSERT_EQUAL(28, layout.rows[2].top);
    TEST_ASSERT_EQUAL(36, layout.height);
}

static void test_feedback_gap_drawn_only_inside_a_row() {
    const std::vector<FeedbackPiece> pieces = {
        piece("a", FeedbackTone::Right, true),
        piece("b", FeedbackTone::Right, false, " / "),
    };
    // " / " is 18 px: "a" + gap + "b" = 30 px fits 40 px, not 20 px.
    const FeedbackLayout wide = layoutFeedback(pieces, 40, fixedMeasure());
    TEST_ASSERT_EQUAL_UINT(1, wide.rows.size());
    TEST_ASSERT_EQUAL(18, wide.segments[1].gapWidth);

    const FeedbackLayout narrow = layoutFeedback(pieces, 20, fixedMeasure());
    TEST_ASSERT_EQUAL_UINT(2, narrow.rows.size());
    TEST_ASSERT_EQUAL(0, narrow.segments[1].gapWidth);
}

static void test_feedback_zero_width_still_progresses() {
    const std::vector<FeedbackPiece> pieces = {
        piece("ab", FeedbackTone::Note, true),
        piece("", FeedbackTone::Note, true),
        piece("cd", FeedbackTone::Note, true),
    };
    const FeedbackLayout layout = layoutFeedback(pieces, 0, fixedMeasure());
    TEST_ASSERT_EQUAL_STRING("abcd", withoutSpaces(shownText(pieces, layout)).c_str());
    TEST_ASSERT_TRUE(layout.rows.size() >= 4);
}

// ------------------------------------------------------------ builder

static void test_feedback_meaning_sections_and_synonyms() {
    FeedbackContent c;
    c.correct = true;
    c.typed = "writing";
    c.answers = {"Composition", "Writing"};
    c.synonyms = {"essay", "text"};
    c.notes = {"Note: x"};
    const std::vector<FeedbackPiece> pieces = buildFeedbackPieces(c);
    // Latin text is one piece per word: "You" "typed" "writing" "Answer"
    // "Composition" "Writing" "essay," "text" "Note:" "x".
    TEST_ASSERT_EQUAL_UINT(10, pieces.size());
    TEST_ASSERT_EQUAL_STRING("You", pieces[0].text.c_str());
    TEST_ASSERT_TRUE(pieces[0].tone == FeedbackTone::Label && pieces[0].newline);
    TEST_ASSERT_EQUAL_STRING("typed", pieces[1].text.c_str());
    TEST_ASSERT_EQUAL_STRING(" ", pieces[1].gap.c_str());
    TEST_ASSERT_EQUAL_STRING("writing", pieces[2].text.c_str());
    TEST_ASSERT_TRUE(pieces[2].tone == FeedbackTone::Typed && pieces[2].newline);
    TEST_ASSERT_EQUAL_STRING("Answer", pieces[3].text.c_str());
    TEST_ASSERT_TRUE(pieces[3].newline);
    TEST_ASSERT_EQUAL_STRING("Composition", pieces[4].text.c_str());
    TEST_ASSERT_TRUE(pieces[4].tone == FeedbackTone::Right && pieces[4].newline);
    TEST_ASSERT_EQUAL_STRING("Writing", pieces[5].text.c_str());
    TEST_ASSERT_EQUAL_STRING(" / ", pieces[5].gap.c_str());
    TEST_ASSERT_FALSE(pieces[5].newline);
    // Synonyms follow the answers after "+"; commas join them.
    TEST_ASSERT_EQUAL_STRING("essay,", pieces[6].text.c_str());
    TEST_ASSERT_EQUAL_STRING("  + ", pieces[6].gap.c_str());
    TEST_ASSERT_TRUE(pieces[6].tone == FeedbackTone::Synonym);
    TEST_ASSERT_EQUAL_STRING("text", pieces[7].text.c_str());
    TEST_ASSERT_TRUE(pieces[8].tone == FeedbackTone::Note && pieces[8].newline);
    TEST_ASSERT_EQUAL_STRING("Note:", pieces[8].text.c_str());
    TEST_ASSERT_EQUAL_STRING("x", pieces[9].text.c_str());
}

static void test_feedback_wrong_answers_use_wrong_tone() {
    FeedbackContent c;
    c.correct = false;
    c.answers = {"Composition"};
    const std::vector<FeedbackPiece> pieces = buildFeedbackPieces(c);
    // You, typed, -, Answer, Composition
    TEST_ASSERT_EQUAL_UINT(5, pieces.size());
    TEST_ASSERT_TRUE(pieces[4].tone == FeedbackTone::Wrong);
    TEST_ASSERT_EQUAL_STRING("-", pieces[2].text.c_str());  // empty typed text
}

static void test_feedback_reading_groups_and_labels() {
    FeedbackContent c;
    c.japanese = true;
    c.correct = true;
    c.typed = "じょう";
    c.answers = {"じょう", "せい", "", "うえ"};
    const std::vector<FeedbackPiece> pieces = buildFeedbackPieces(c);
    TEST_ASSERT_EQUAL_STRING("Y", pieces[0].text.c_str());
    TEST_ASSERT_EQUAL_STRING("A", pieces[2].text.c_str());
    TEST_ASSERT_TRUE(pieces[3].face == FeedbackFace::Japanese && pieces[3].newline);
    TEST_ASSERT_EQUAL_STRING(" / ", pieces[4].gap.c_str());
    // An empty token is the on'yomi / kun'yomi break: four spaces, not " / ".
    TEST_ASSERT_EQUAL_STRING("    ", pieces[5].gap.c_str());
    TEST_ASSERT_EQUAL_STRING("うえ", pieces[5].text.c_str());
}

static void test_feedback_builder_splits_japanese_and_latin_words() {
    FeedbackContent c;
    c.typed = "日本 abc";
    const std::vector<FeedbackPiece> pieces = buildFeedbackPieces(c);
    // "You typed", "日本" (Japanese), "abc" (Latin, gap " "), "Answer".
    // You, typed, 日本 (Japanese), abc (Latin, gap " "), Answer
    TEST_ASSERT_EQUAL_UINT(5, pieces.size());
    TEST_ASSERT_EQUAL_STRING("日本", pieces[2].text.c_str());
    TEST_ASSERT_TRUE(pieces[2].face == FeedbackFace::Japanese);
    TEST_ASSERT_EQUAL_STRING("abc", pieces[3].text.c_str());
    TEST_ASSERT_TRUE(pieces[3].face == FeedbackFace::Latin);
    TEST_ASSERT_EQUAL_STRING(" ", pieces[3].gap.c_str());
}

static void test_feedback_warning_shows_only_the_message() {
    FeedbackContent c;
    c.warning = true;
    c.typed = "secret";
    c.answers = {"Composition"};
    c.notes = {"Close. Check the spelling"};
    const std::vector<FeedbackPiece> pieces = buildFeedbackPieces(c);
    // Only the message words: no typed text, no answer, no labels.
    TEST_ASSERT_EQUAL_UINT(4, pieces.size());
    TEST_ASSERT_EQUAL_STRING("Close.", pieces[0].text.c_str());
    TEST_ASSERT_TRUE(pieces[0].newline && pieces[0].tone == FeedbackTone::Note);
    TEST_ASSERT_EQUAL_STRING("spelling", pieces[3].text.c_str());
}

// Every answer and every synonym reaches the pieces: the feedback never cuts a
// list to a fixed number of lines.
static void test_feedback_long_answer_list_is_all_there_and_scrolls() {
    FeedbackContent c;
    c.correct = true;
    c.typed = "meaning";
    for (int i = 0; i < 12; ++i) c.answers.push_back("answer number " + std::to_string(10 + i));
    c.synonyms = {"alpha beta", "gamma delta", "epsilon"};
    const std::vector<FeedbackPiece> pieces = buildFeedbackPieces(c);
    const FeedbackLayout layout = layoutFeedback(pieces, 136, fixedMeasure());

    // Every answer and synonym word is drawn (the synonym commas are kept too).
    const std::string shown = withoutSpaces(shownText(pieces, layout));
    for (const std::string& a : c.answers)
        TEST_ASSERT_TRUE(shown.find(withoutSpaces(a)) != std::string::npos);
    TEST_ASSERT_TRUE(shown.find("alphabeta") != std::string::npos);
    TEST_ASSERT_TRUE(shown.find("epsilon,") == std::string::npos);  // last synonym: no comma
    TEST_ASSERT_TRUE(shown.find("epsilon") != std::string::npos);
    TEST_ASSERT_TRUE(feedbackScrollable(layout.height, 82));
}

// A long mnemonic line (REV-03's hint sentence): every word shown, wrapped.
static void test_feedback_long_mnemonic_wraps_in_full() {
    std::string mnemonic;
    for (int i = 0; i < 40; ++i) mnemonic += "word" + std::to_string(i % 10) + " ";
    mnemonic += "end.";
    FeedbackContent c;
    c.warning = true;  // notes only: no label rows in the comparison
    c.notes = {mnemonic};
    const std::vector<FeedbackPiece> pieces = buildFeedbackPieces(c);
    const FeedbackLayout layout = layoutFeedback(pieces, 136, fixedMeasure());
    TEST_ASSERT_EQUAL_STRING(withoutSpaces(mnemonic).c_str(),
                             withoutSpaces(shownText(pieces, layout)).c_str());
    TEST_ASSERT_TRUE(layout.height > 82);
    for (const FeedbackRow& row : layout.rows) {
        int width = 0;
        for (size_t k = 0; k < row.count; ++k) {
            const FeedbackSegment& seg = layout.segments[row.first + k];
            width += seg.gapWidth + seg.width;
        }
        TEST_ASSERT_TRUE(width <= 136);
    }
}

// ------------------------------------------------------------ notes (REV-03)

static void test_feedback_note_lines_per_status() {
    Subject s;
    s.meaningHint = "Hint sentence. More text.";
    s.meaningNote = "Note text";
    s.readingNote = "reading note";

    const std::vector<std::string> wrong =
        feedbackNoteLines(&s, true, AnswerStatus::Incorrect, "Incorrect meaning");
    TEST_ASSERT_EQUAL_UINT(1, wrong.size());
    TEST_ASSERT_EQUAL_STRING("Hint sentence.", wrong[0].c_str());

    Subject noHint;
    noHint.meaningNote = "Note text";
    const std::vector<std::string> noteOnly =
        feedbackNoteLines(&noHint, true, AnswerStatus::Incorrect, "Incorrect meaning");
    TEST_ASSERT_EQUAL_UINT(1, noteOnly.size());
    TEST_ASSERT_EQUAL_STRING("Note text", noteOnly[0].c_str());

    const std::vector<std::string> correct =
        feedbackNoteLines(&s, true, AnswerStatus::Correct, "Close: Writing");
    TEST_ASSERT_EQUAL_UINT(2, correct.size());
    TEST_ASSERT_EQUAL_STRING("Close: Writing", correct[0].c_str());
    TEST_ASSERT_EQUAL_STRING("Note: Note text", correct[1].c_str());

    const std::vector<std::string> plain =
        feedbackNoteLines(&noHint, true, AnswerStatus::Correct, "Correct");
    TEST_ASSERT_EQUAL_UINT(1, plain.size());
    TEST_ASSERT_EQUAL_STRING("Note: Note text", plain[0].c_str());

    const std::vector<std::string> reading =
        feedbackNoteLines(&s, false, AnswerStatus::Correct, "Correct");
    TEST_ASSERT_EQUAL_UINT(1, reading.size());
    TEST_ASSERT_EQUAL_STRING("Reading note: reading note", reading[0].c_str());

    const std::vector<std::string> warn =
        feedbackNoteLines(&s, true, AnswerStatus::Warning, "Check the spelling");
    TEST_ASSERT_EQUAL_UINT(1, warn.size());
    TEST_ASSERT_EQUAL_STRING("Check the spelling", warn[0].c_str());

    TEST_ASSERT_TRUE(feedbackNoteLines(nullptr, true, AnswerStatus::Correct, "Correct").empty());
}

// ------------------------------------------------------------ scroll window

static void test_feedback_scroll_bounds_and_keys() {
    TEST_ASSERT_EQUAL(0, feedbackMaxScroll(82, 82));
    TEST_ASSERT_EQUAL(118, feedbackMaxScroll(200, 82));
    TEST_ASSERT_EQUAL(0, clampFeedbackScroll(-5, 200, 82));
    TEST_ASSERT_EQUAL(118, clampFeedbackScroll(500, 200, 82));
    TEST_ASSERT_EQUAL(0, clampFeedbackScroll(50, 80, 82));

    TEST_ASSERT_FALSE(feedbackScrollable(82, 82));
    TEST_ASSERT_TRUE(feedbackScrollable(83, 82));

    TEST_ASSERT_EQUAL(8, feedbackScrollKey(0, true));
    TEST_ASSERT_EQUAL(0, feedbackScrollKey(0, false));
    TEST_ASSERT_EQUAL(0, feedbackScrollKey(8, false));
    TEST_ASSERT_EQUAL(88, feedbackScrollKey(80, true));
}

static void test_feedback_row_visibility_window() {
    FeedbackRow row;
    row.height = 8;
    row.top = 0;
    TEST_ASSERT_FALSE(feedbackRowVisible(row, 10, 16));  // ends at 8, above 10
    row.top = 8;
    TEST_ASSERT_TRUE(feedbackRowVisible(row, 10, 16));   // 8..16 overlaps 10..26
    row.top = 24;
    TEST_ASSERT_TRUE(feedbackRowVisible(row, 10, 16));   // 24..32 overlaps 10..26
    row.top = 32;
    TEST_ASSERT_FALSE(feedbackRowVisible(row, 10, 16));  // below 26
}

static void test_feedback_thumb_geometry() {
    // 164 px of content in an 82 px box on an 82 px track: a half-height thumb.
    FeedbackThumb top = feedbackThumb(164, 82, 0, 82);
    TEST_ASSERT_EQUAL(41, top.height);
    TEST_ASSERT_EQUAL(0, top.y);
    FeedbackThumb bottom = feedbackThumb(164, 82, 82, 82);
    TEST_ASSERT_EQUAL(41, bottom.y);
    FeedbackThumb middle = feedbackThumb(164, 82, 41, 82);
    TEST_ASSERT_EQUAL(20, middle.y);
    // Very long content keeps the thumb grabbable (ListView's 10 px floor).
    TEST_ASSERT_EQUAL(10, feedbackThumb(10000, 82, 0, 82).height);
    // Content that fits: the whole track, no movement.
    FeedbackThumb fits = feedbackThumb(60, 82, 0, 82);
    TEST_ASSERT_EQUAL(82, fits.height);
    TEST_ASSERT_EQUAL(0, fits.y);
}

void run_feedback_tests() {
    RUN_TEST(test_feedback_wraps_latin_by_word);
    RUN_TEST(test_feedback_splits_only_a_word_wider_than_a_row);
    RUN_TEST(test_feedback_japanese_wraps_by_code_point);
    RUN_TEST(test_feedback_japanese_and_latin_never_share_a_row);
    RUN_TEST(test_feedback_gap_drawn_only_inside_a_row);
    RUN_TEST(test_feedback_zero_width_still_progresses);
    RUN_TEST(test_feedback_meaning_sections_and_synonyms);
    RUN_TEST(test_feedback_wrong_answers_use_wrong_tone);
    RUN_TEST(test_feedback_reading_groups_and_labels);
    RUN_TEST(test_feedback_builder_splits_japanese_and_latin_words);
    RUN_TEST(test_feedback_warning_shows_only_the_message);
    RUN_TEST(test_feedback_long_answer_list_is_all_there_and_scrolls);
    RUN_TEST(test_feedback_long_mnemonic_wraps_in_full);
    RUN_TEST(test_feedback_note_lines_per_status);
    RUN_TEST(test_feedback_scroll_bounds_and_keys);
    RUN_TEST(test_feedback_row_visibility_window);
    RUN_TEST(test_feedback_thumb_geometry);
}

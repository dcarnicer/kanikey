#pragma once
// Review screens: prompt/feedback, item detail and session summary, drawn
// from explicit view data. The app adapter fills the views from its session
// state and keeps startWrite()/present().
#include <cstdint>
#include <string>
#include <vector>

#include "board/Board.h"
#include "platform/GlyphPack.h"
#include "ui/Theme.h"
#include "wk/AnswerChecker.h"
#include "wk/AudioPlan.h"
#include "wk/ReviewQueue.h"
#include "wk/Types.h"

namespace kk::app {

struct ReviewView {
    const kk::Subject* subject = nullptr;
    const char* header = "REVIEWS";
    kk::ReviewPart part = kk::ReviewPart::Meaning;
    bool feedback = false;
    bool minimalHeader = false;  // R10: hide the SRS/accuracy text
    bool showDetailHint = true;
    // REV-04: compact `C ok  A later  Tab info` bar on Incorrect feedback
    // when R7 is On.
    bool wrongShortcuts = false;
    // AUD-03: R on reading feedback, or `no audio` when a vocabulary clip is
    // missing. None for meaning feedback and for kanji/radicals.
    kk::AudioHint audio = kk::AudioHint::None;
    const char* escHint = "Esc: quit";
    // REV-05: > 0 draws `WRAP UP N` instead of the header until the summary.
    int wrapUpCount = 0;
    uint8_t srsStage = 0;
    int position = 1;
    int total = 0;
    // SPEED-01: while a blocking send loop runs, the left hint (y=125) shows
    // `Sending n/total` in place of the normal key hint.
    bool sending = false;
    unsigned sent = 0;
    unsigned sendTotal = 0;
    std::string typed;   // meaning input or the reading shown in the field
    std::string romaji;  // IME raw text, reading prompt only
    kk::AnswerStatus status = kk::AnswerStatus::Incorrect;
    std::string message;
    // Accepted answers in display order. Reading lists may contain an empty
    // token as an on'yomi/kun'yomi group break.
    std::vector<std::string> answers;
    std::vector<std::string> synonyms;  // drawn faint after the answers
    // REV-01/03: the feedback body's Up/Down offset (px). Read and clamped by
    // the draw; the controller owns the value. Null outside feedback.
    int* feedbackScroll = nullptr;
};

// Draws the review prompt or feedback frame into d. `fontPack` draws glyphs;
// `radicalPack` supplies radical bitmaps.
void drawReviewScreen(lgfx::LovyanGFX& d, kk::GlyphPack& fontPack,
                      kk::GlyphPack& radicalPack, const ReviewView& view);

struct ReviewDetailView {
    const kk::Subject* subject = nullptr;
    int page = 0;
    int pages = 1;
    int kind = 0;  // detailPageKind() for this page
    kk::AudioHint audio = kk::AudioHint::None;
    std::vector<const kk::Subject*> components;
    std::vector<const kk::Subject*> similar;
    std::vector<const kk::Subject*> amalgamations;
};

// Draws one page of the review item detail into d.
void drawReviewDetailScreen(lgfx::LovyanGFX& d, kk::GlyphPack& fontPack,
                            kk::GlyphPack& radicalPack, const ReviewDetailView& view);

// REV-11: one row of the session summary's mistake list.
struct ReviewSummaryItem {
    const kk::Subject* subject = nullptr;
    uint8_t stageFrom = 0;
    uint8_t stageTo = 0;
    bool stageValid = false;
    uint8_t incorrectMeaning = 0;
    uint8_t incorrectReading = 0;
    std::string wrongMeaning;
    std::string wrongReading;
};

struct ReviewSummaryView {
    unsigned reviews = 0;
    unsigned accuracy = 0;
    char duration[16] = "";
    unsigned stageUps = 0;
    unsigned mistakes = 0;
    unsigned pending = 0;
    unsigned attention = 0;
    bool stageChangeValid = false;
    uint8_t stageFrom = 0;
    uint8_t stageTo = 0;
    bool lastHasStats = false;
    unsigned lastReviewCount = 0;
    unsigned lastPercentage = 0;
    bool practice = false;
    unsigned skipped = 0;
    // SPEED-01: `Sending n/total` replaces the outbox state at y=98 and a 3 px
    // bar at y=121 fills as the loop progresses.
    bool sending = false;
    unsigned sent = 0;
    unsigned sendTotal = 0;
    // REV-11: the scrollable mistake list. `focus` 0 is the stats (the
    // original layout), 1 is the list; `selected` indexes `items`.
    std::vector<ReviewSummaryItem> items;
    bool canNextBatch = false;
    int focus = 0;
    int selected = 0;
};

// Draws the session summary into d. `fontPack` draws prompt characters and
// readings; `radicalPack` supplies radical bitmaps for the mistake rows.
void drawReviewSummaryScreen(lgfx::LovyanGFX& d, kk::GlyphPack& fontPack,
                             kk::GlyphPack& radicalPack, const ReviewSummaryView& view);

}  // namespace kk::app

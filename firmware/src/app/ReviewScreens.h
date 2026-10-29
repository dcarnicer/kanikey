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
#include "wk/ReviewQueue.h"
#include "wk/Types.h"

namespace kk::app {

struct ReviewView {
    const kk::Subject* subject = nullptr;
    kk::ReviewPart part = kk::ReviewPart::Meaning;
    bool feedback = false;
    uint8_t srsStage = 0;
    int position = 1;
    int total = 0;
    std::string typed;   // meaning input or the reading shown in the field
    std::string romaji;  // IME raw text, reading prompt only
    kk::AnswerStatus status = kk::AnswerStatus::Incorrect;
    std::string message;
    std::string answer;
};

// Draws the review prompt or feedback frame into d.
void drawReviewScreen(lgfx::LovyanGFX& d, kk::GlyphPack& pack, const ReviewView& view);

struct ReviewDetailView {
    const kk::Subject* subject = nullptr;
    int page = 0;
    int pages = 1;
    int kind = 0;  // detailPageKind() for this page
    std::vector<const kk::Subject*> components;
    std::vector<const kk::Subject*> similar;
    std::vector<const kk::Subject*> amalgamations;
};

// Draws one page of the review item detail into d.
void drawReviewDetailScreen(lgfx::LovyanGFX& d, kk::GlyphPack& pack,
                            const ReviewDetailView& view);

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
};

// Draws the session summary into d.
void drawReviewSummaryScreen(lgfx::LovyanGFX& d, const ReviewSummaryView& view);

}  // namespace kk::app

#pragma once
// Lesson screens: list, empty state, card and practice quiz, drawn from
// explicit view data. The app adapter fills the views from its lesson state
// and keeps startWrite()/present().
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "board/Board.h"
#include "platform/GlyphPack.h"
#include "ui/Theme.h"
#include "wk/AnswerChecker.h"
#include "wk/AudioPlan.h"
#include "wk/Types.h"

namespace kk::app {

struct LessonRowView {
    const kk::Subject* subject = nullptr;  // null draws the missing row
    std::string meaning;
    bool selected = true;  // selected into the batch
};

struct LessonListView {
    std::vector<LessonRowView> rows;
    size_t selectedIndex = 0;
    std::string note;  // transient hint, e.g. "batch full"
};

// Draws the lesson list into d.
void drawLessonListScreen(lgfx::LovyanGFX& d, kk::GlyphPack& fontPack,
                          kk::GlyphPack& radicalPack, const LessonListView& view);

struct LessonEmptyView {
    const char* nextReview = "--:--";
};

// Draws the empty lesson state into d.
void drawLessonEmptyScreen(lgfx::LovyanGFX& d, const LessonEmptyView& view);

struct LessonPageView {
    std::string title;              // "Meaning", "Reading", ...
    std::vector<std::string> lines; // pre-wrapped; at most three per screen page
};

struct LessonCardView {
    const kk::Subject* subject = nullptr;
    std::string meaning;
    std::string reading;
    std::string header;  // "2/5 p1/3" from the lesson browser
    // One entry per drawn card page. A long topic body is split over several
    // entries, each carrying the topic title.
    std::vector<LessonPageView> pages;
    size_t page = 0;
    size_t index = 0;
    size_t total = 0;
    // AUD-03: R on the card when the reading line is shown; `no audio` when a
    // vocabulary clip is missing. None for kanji/radicals.
    kk::AudioHint audio = kk::AudioHint::None;
};

// Draws the lesson card into d.
void drawLessonCardScreen(lgfx::LovyanGFX& d, kk::GlyphPack& fontPack,
                          kk::GlyphPack& radicalPack, const LessonCardView& view);

struct LessonQuizView {
    const kk::Subject* subject = nullptr;
    bool complete = false;
    bool feedback = false;
    bool meaning = true;
    kk::AnswerStatus status = kk::AnswerStatus::Incorrect;
    std::string message;
    std::string typed;
    // Accepted answers in display order. Reading lists may contain an empty
    // token as an on'yomi/kun'yomi group break.
    std::vector<std::string> answers;
    std::vector<std::string> synonyms;  // drawn faint after the answers
    std::string romaji;
    uint16_t mistakes = 0;
    int position = 1;
    int total = 1;
};

// Draws the lesson practice quiz into d.
void drawLessonQuizScreen(lgfx::LovyanGFX& d, kk::GlyphPack& fontPack,
                          kk::GlyphPack& radicalPack, const LessonQuizView& view);

}  // namespace kk::app

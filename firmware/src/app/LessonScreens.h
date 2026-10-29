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
#include "wk/Types.h"

namespace kk::app {

struct LessonRowView {
    const kk::Subject* subject = nullptr;  // null draws the missing row
    std::string meaning;
};

struct LessonListView {
    std::vector<LessonRowView> rows;
    size_t selectedIndex = 0;
};

// Draws the lesson list into d.
void drawLessonListScreen(lgfx::LovyanGFX& d, kk::GlyphPack& pack, const LessonListView& view);

struct LessonEmptyView {
    const char* nextReview = "--:--";
};

// Draws the empty lesson state into d.
void drawLessonEmptyScreen(lgfx::LovyanGFX& d, const LessonEmptyView& view);

struct LessonCardView {
    const kk::Subject* subject = nullptr;
    std::string meaning;
    std::string reading;
    std::vector<std::string> lines;  // pre-wrapped mnemonic and notes
    size_t page = 0;
    size_t pages = 1;
    size_t index = 0;
    size_t total = 0;
};

// Draws the lesson card into d.
void drawLessonCardScreen(lgfx::LovyanGFX& d, kk::GlyphPack& pack, const LessonCardView& view);

struct LessonQuizView {
    const kk::Subject* subject = nullptr;
    bool complete = false;
    bool feedback = false;
    bool meaning = true;
    kk::AnswerStatus status = kk::AnswerStatus::Incorrect;
    std::string message;
    std::string typed;
    std::string answer;
    std::string romaji;
    uint16_t mistakes = 0;
    int position = 1;
    int total = 1;
};

// Draws the lesson practice quiz into d.
void drawLessonQuizScreen(lgfx::LovyanGFX& d, kk::GlyphPack& pack,
                          const LessonQuizView& view);

}  // namespace kk::app

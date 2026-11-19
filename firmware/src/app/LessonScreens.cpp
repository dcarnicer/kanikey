// Lesson screens drawing. Pure view code: no app globals, no present(), no
// startWrite()/endWrite() (the caller owns the transfer window).
#include "app/LessonScreens.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "ui/HintBar.h"
#include "ui/ListView.h"
#include "ui/Text.h"
#include "ui/Widgets.h"
#include "wk/Text.h"
#include "wk/Utf8.h"

namespace kk::app {

using namespace ui;

namespace {

std::string partHint(const kk::Subject& s, bool meaning) {
    return meaning ? s.meaningHint : s.readingHint;
}

std::string partNote(const kk::Subject& s, bool meaning) {
    return meaning ? s.meaningNote : s.readingNote;
}

// REV-03: a wrong answer shows the first hint sentence, then the user's note.
// A correct answer keeps the checker text (for example the close note).
std::string feedbackLine(const LessonQuizView& view) {
    if (!view.subject) return std::string();
    if (view.status == kk::AnswerStatus::Incorrect) {
        const std::string hint = kk::firstSentence(partHint(*view.subject, view.meaning));
        if (!hint.empty()) return hint;
        return partNote(*view.subject, view.meaning);
    }
    if (view.status == kk::AnswerStatus::Warning) return view.message;
    if (view.message.empty() || view.message == "Correct") return std::string();
    return view.message;
}

void drawFeedbackLine(lgfx::LovyanGFX& d, const LessonQuizView& view, int x, int y, int w) {
    if (view.status == kk::AnswerStatus::Correct) {
        // Keep the checker line (REV-02's close note) and the user's note on
        // separate lines so a long close note cannot push the note away.
        int line = 0;
        const std::string message = feedbackLine(view);
        if (!message.empty()) {
            drawWrapped(d, message, x, y, w, 1, ui::kFaint, ui::kBg, true);
            line = 1;
        }
        if (view.subject) {
            const std::string note = partNote(*view.subject, view.meaning);
            if (!note.empty()) {
                const std::string prefix = view.meaning ? "Note: " : "Reading note: ";
                drawWrapped(d, prefix + note, x, y + line * 8, w, 2 - line, ui::kFaint,
                            ui::kBg, true);
            }
        }
        return;
    }
    const std::string text = feedbackLine(view);
    if (!text.empty()) drawWrapped(d, text, x, y, w, 2, ui::kFaint, ui::kBg, true);
}

}  // namespace

void drawLessonListScreen(lgfx::LovyanGFX& d, kk::GlyphPack& fontPack,
                          kk::GlyphPack& radicalPack, const LessonListView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("LESSONS", 6, 3);

    const size_t total = view.rows.size();
    if (total == 0 || view.selectedIndex >= total) return;
    char pos[20];
    snprintf(pos, sizeof pos, "%u/%u", static_cast<unsigned>(view.selectedIndex + 1),
             static_cast<unsigned>(total));
    d.setTextFont(0);
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString(pos, 233, 5);
    d.setTextDatum(lgfx::top_left);
    d.fillRect(0, 14, 240, 2, ui::kSurfaceHi);

    constexpr int kVisible = 5;
    const ListWindow window =
        listWindow(static_cast<int>(total), static_cast<int>(view.selectedIndex), kVisible);
    for (int row = 0; row < window.rows; ++row) {
        const int i = window.first + row;
        const LessonRowView& item = view.rows[static_cast<size_t>(i)];
        const kk::Subject* s = item.subject;
        const int y = 18 + row * 20;
        if (i == static_cast<int>(view.selectedIndex)) {
            d.fillRect(4, y - 1, 228, 19, ui::kSurfaceHi);
            d.fillRect(4, y - 1, 2, 19, ui::kAccent);
        }
        d.fillRect(9, y, 22, 20, ui::kSurface);
        d.drawRect(9, y, 22, 20, ui::kLine);
        bool promptDrawn = false;
        if (s) {
            if (s->type == kk::SubjectType::Radical) {
                promptDrawn = radicalPack.drawRadical(d, s->id, 20, y + 10, 20, 18, ui::kText,
                                               ui::kSurface,
                                               kk::ClipRect{10, y + 1, 30, y + 19});
            } else if (!s->characters.empty()) {
                std::string first = s->characters;
                const char* p = first.c_str();
                kk::utf8Next(&p);
                first.resize(static_cast<size_t>(p - first.c_str()));
                promptDrawn = fontPack.drawTextCentered(d, first, 20, y + 10, 20, 20, 20,
                                                    ui::kText, ui::kSurface,
                                                    kk::ClipRect{10, y + 1, 30, y + 19});
            }
        }
        if (!promptDrawn) {
            d.setTextFont(1);
            d.setTextSize(1);
            d.setTextColor(ui::kWarn, ui::kSurface);
            d.setTextDatum(lgfx::middle_center);
            d.drawString("?", 20, y + 10);
            d.setTextDatum(lgfx::top_left);
        }

        const char* kind = !s ? "?"
                           : s->type == kk::SubjectType::Vocabulary ? "VOCAB"
                           : s->type == kk::SubjectType::Radical  ? "RAD"
                                                                  : "KANJI";
        char label[72];
        snprintf(label, sizeof label, "%s L%d  %s", kind, s ? s->level : 0,
                 item.meaning.c_str());
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(i == static_cast<int>(view.selectedIndex) ? ui::kText : ui::kDim,
                       i == static_cast<int>(view.selectedIndex) ? ui::kSurfaceHi : ui::kBg);
        char* end = label + strlen(label);
        while (end > label + 1 && d.textWidth(label) > 194) *--end = '\0';
        d.drawString(label, 34, y + 1);
        d.setTextColor(item.selected ? ui::kAccentHi : ui::kFaint,
                       i == static_cast<int>(view.selectedIndex) ? ui::kSurfaceHi : ui::kBg);
        d.setTextDatum(lgfx::top_right);
        d.drawString(item.selected ? "*" : ">", 230, y + 1);
        d.setTextDatum(lgfx::top_left);
    }

    if (total > kVisible) {
        constexpr int kTrackY = 18;
        constexpr int kTrackH = 100;
        d.fillRect(236, kTrackY, 2, kTrackH, ui::kLine);
        const int thumbH = std::max(10, kTrackH * kVisible / static_cast<int>(total));
        const int thumbY =
            kTrackY + (kTrackH - thumbH) * window.first / static_cast<int>(total - kVisible);
        d.fillRect(236, thumbY, 2, thumbH, ui::kAccentHi);
    }

    if (!view.note.empty()) {
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(ui::kWarn, ui::kBg);
        d.drawString(view.note.c_str(), 62, 5);
    }
    drawHintLeft(d, "Space: select  Enter: study", ui::kFaint, 6, 125);
}

void drawLessonEmptyScreen(lgfx::LovyanGFX& d, const LessonEmptyView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("LESSONS", 6, 3);
    d.setTextFont(4);
    d.setTextSize(1);
    d.setTextColor(ui::kText, ui::kBg);
    d.drawString("No lessons now", 8, 38);
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ui::kDim, ui::kBg);
    std::string next = "Next review  ";
    next += view.nextReview;
    d.drawString(next.c_str(), 8, 82);
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("New lessons unlock as you level up.", 8, 102);
    drawHintLeft(d, "Esc: home");
}

void drawLessonCardScreen(lgfx::LovyanGFX& d, kk::GlyphPack& fontPack,
                          kk::GlyphPack& radicalPack, const LessonCardView& view) {
    const kk::Subject* s = view.subject;
    if (!s) return;

    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("LESSON", 6, 3);
    // The browser owns both counters, so the drawn page count is the number of
    // pages it actually made for this card.
    char fallback[24];
    const char* header = view.header.c_str();
    if (view.header.empty()) {
        snprintf(fallback, sizeof fallback, "%u/%u p%u/%u",
                 static_cast<unsigned>(view.index + 1),
                 static_cast<unsigned>(view.total),
                 static_cast<unsigned>(view.page + 1),
                 static_cast<unsigned>(view.pages.size()));
        header = fallback;
    }
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString(header, 233, 5);
    d.setTextDatum(lgfx::top_left);

    d.fillRect(0, 14, 240, 2, ui::kSurfaceHi);
    if (view.total > 0) {
        const int progressWidth = static_cast<int>(240u * view.index / view.total);
        d.fillRect(0, 14, progressWidth, 2, ui::kAccentHi);
    }

    d.fillRect(4, 20, 74, 64, ui::kSurface);
    d.drawRect(4, 20, 74, 64, ui::kLine);
    if (!drawSubjectPrompt(d, fontPack, radicalPack, *s, 41, 52, 70, 62, 60, ui::kText,
                           ui::kSurface, kk::ClipRect{6, 22, 76, 82}))
        drawMissingGlyph(d, fontPack, radicalPack, 41, 52);

    const char* kind = s->type == kk::SubjectType::Vocabulary ? "VOCAB"
                       : s->type == kk::SubjectType::Radical ? "RADICAL"
                                                             : "KANJI";
    char type[24];
    snprintf(type, sizeof type, "%s L%d", kind, s->level);
    d.setTextFont(0);
    d.setTextSize(1);
    drawPill(d, 86, 20, type, ui::kDim, ui::kSurfaceHi);

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ui::kText, ui::kBg);
    std::string meaning = view.meaning;
    while (meaning.size() > 1 && d.textWidth(meaning.c_str()) > 148) meaning.pop_back();
    d.drawString(meaning.c_str(), 86, 38);

    if (!view.reading.empty()) {
        std::string reading = view.reading;
        if (!fontPack.drawTextFit(d, reading, 86, 80, 32, 148, ui::kReading, ui::kBg,
                                  kk::ClipRect{86, 50, 236, 88}) &&
            !drawJapaneseFit(d, fontPack, reading, 86, 232, 80, 148, ui::kReading, ui::kBg,
                             kk::ClipRect{86, 50, 236, 88})) {
            d.setTextFont(1);
            d.setTextSize(1);
            d.setTextColor(ui::kWarn, ui::kBg);
            d.drawString("no glyph", 86, 58);
        }
    }

    d.setTextFont(0);
    d.setTextSize(1);
    const LessonPageView* cardPage =
        view.page < view.pages.size() ? &view.pages[view.page] : nullptr;
    if (cardPage) {
        // The topic title sits above the three text lines so a continuation of
        // a long topic still says which section it belongs to.
        d.setTextColor(ui::kAccentHi, ui::kBg);
        d.drawString(cardPage->title.c_str(), 5, 87);
        d.setTextColor(ui::kDim, ui::kBg);
        for (size_t i = 0; i < 3 && i < cardPage->lines.size(); ++i)
            d.drawString(cardPage->lines[i].c_str(), 5, 96 + static_cast<int>(i) * 9);
    }

    drawHintLeft(d, "Q quiz", ui::kFaint, 5, 125);
    drawHintRight(d, "Enter/Space: next  Esc: list", ui::kFaint, 233, 125);
}

void drawLessonQuizScreen(lgfx::LovyanGFX& d, kk::GlyphPack& fontPack,
                          kk::GlyphPack& radicalPack, const LessonQuizView& view) {
    if (!view.subject) return;
    const kk::Subject& s = *view.subject;
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("LESSON QUIZ", 6, 3);
    char right[24];
    snprintf(right, sizeof right, "%d/%d", view.position, view.total);
    d.setTextFont(0);
    d.setTextDatum(lgfx::top_right);
    d.drawString(right, 233, 5);
    d.setTextDatum(lgfx::top_left);
    d.fillRect(0, 14, 240, 2, ui::kSurfaceHi);

    d.fillRect(4, 20, 74, 64, ui::kSurface);
    d.drawRect(4, 20, 74, 64, ui::kLine);
    if (!drawSubjectPrompt(d, fontPack, radicalPack, s, 41, 52, 70, 62, 60, ui::kText,
                           ui::kSurface, kk::ClipRect{6, 22, 76, 82}))
        drawMissingGlyph(d, fontPack, radicalPack, 41, 52);

    const bool meaning = view.meaning;
    if (view.complete) {
        drawPill(d, 86, 20, "DONE", ui::kOk, ui::kOkBg);
        d.setTextFont(1);
        d.setTextSize(1);
        d.setTextColor(ui::kText, ui::kBg);
        d.drawString("Practice complete", 86, 40);
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(ui::kDim, ui::kBg);
        char mistakes[32];
        snprintf(mistakes, sizeof mistakes, "%u mistake%s",
                 static_cast<unsigned>(view.mistakes), view.mistakes == 1 ? "" : "s");
        d.drawString(mistakes, 86, 58);
        d.setTextColor(ui::kFaint, ui::kBg);
        d.drawString("No /start sent.", 86, 76);
        drawHintLeft(d, "Enter: lesson card  Esc: card", ui::kFaint, 5, 125);
    } else if (view.feedback) {
        const bool ok = view.status == kk::AnswerStatus::Correct;
        const bool warn = view.status == kk::AnswerStatus::Warning;
        drawPill(d, 86, 20, ok ? "CORRECT" : warn ? "CHECK" : "NOT QUITE",
                 ok ? ui::kOk : warn ? ui::kWarn : ui::kErr,
                 ok ? ui::kOkBg : warn ? ui::kSurfaceHi : ui::kErrBg);
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(ui::kFaint, ui::kBg);
        if (warn) {
            // Warnings keep the typed input and never show the answer.
            drawWrapped(d, view.message, 86, 40, 150, 4, ui::kFaint, ui::kBg);
            drawHintLeft(d, "Enter: edit  Esc: card", ui::kFaint, 5, 125);
            return;
        }
        const bool japanese = !meaning;
        std::string typed = view.typed;
        if (japanese) {
            d.drawString("Y", 86, 40);
            bool typedDrawn = false;
            if (!typed.empty())
                typedDrawn = drawJapaneseFit(d, fontPack, typed, 94, 232, 56, 142, ui::kDim,
                                             ui::kBg,
                                             kk::ClipRect{94, 38, 236, 58});
            if (!typedDrawn) {
                d.setTextFont(1);
                d.setTextSize(1);
                d.setTextColor(ui::kWarn, ui::kBg);
                d.drawString(typed.empty() ? "-" : "no glyph", 94, 44);
            }
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(ui::kFaint, ui::kBg);
            d.drawString("A", 86, 72);
            const int lines = drawReadingTokens(
                d, fontPack, view.answers, 94, 68, 142, 2, ok ? ui::kOk : ui::kErr, ui::kBg,
                kk::ClipRect{94, 66, 236, 110});
            if (lines < 0) {
                d.setTextFont(1);
                d.setTextSize(1);
                d.setTextColor(ui::kWarn, ui::kBg);
                d.drawString("no glyph", 94, 74);
            } else if (lines <= 1) {
                // A second reading line would collide with the note line.
                drawFeedbackLine(d, view, 86, 96, 150);
            }
        } else {
            d.drawString("You typed", 86, 38);
            d.setTextFont(1);
            d.setTextSize(1);
            d.setTextColor(ui::kDim, ui::kBg);
            while (typed.size() > 1 && d.textWidth(typed.c_str()) > 150) typed.erase(0, 1);
            d.drawString(typed.empty() ? "-" : typed.c_str(), 86, 47);
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(ui::kFaint, ui::kBg);
            d.drawString("Answer", 86, 65);
            drawAnswerTokens(d, view.answers, view.synonyms, 86, 74, 150, 2,
                             ok ? ui::kOk : ui::kErr, ui::kFaint, ui::kBg);
            drawFeedbackLine(d, view, 86, 92, 150);
        }
        drawHintLeft(d, "Enter: continue  Esc: card", ui::kFaint, 5, 125);
    } else {
        drawPill(d, 86, 20, meaning ? "MEANING" : "READING",
                 meaning ? ui::kAccentHi : ui::kReading, ui::kSurfaceHi);
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(ui::kDim, ui::kBg);
        d.drawString(meaning ? "Type the meaning" : "Type the reading", 86, 38);

        d.fillRect(86, 52, 150, 32, ui::kSurfaceHi);
        d.drawRect(86, 52, 150, 32, ui::kLine);
        std::string shown = view.typed;
        int caret = 92;
        if (shown.empty()) {
            d.setTextFont(1);
            d.setTextSize(1);
            d.setTextColor(ui::kFaint, ui::kSurfaceHi);
            d.drawString(meaning ? "answer" : "kana", 92, 60);
        } else if (meaning) {
            d.setTextFont(1);
            d.setTextSize(1);
            trimFrontToWidth(d, shown, 138);
            d.setTextColor(ui::kText, ui::kSurfaceHi);
            d.drawString(shown.c_str(), 92, 60);
            caret = 92 + d.textWidth(shown.c_str());
        } else if (fontPack.drawTextRight(d, shown, 230, 80, 32, ui::kText, ui::kSurfaceHi,
                                      kk::ClipRect{88, 54, 234, 86}) ||
                   fontPack.drawTextRight(d, shown, 230, 80, 20, ui::kText, ui::kSurfaceHi,
                                      kk::ClipRect{88, 54, 234, 86})) {
            caret = 230;
        } else {
            d.setTextFont(1);
            d.setTextSize(1);
            d.setTextColor(ui::kWarn, ui::kSurfaceHi);
            d.drawString("no glyph", 92, 60);
        }
        if (caret < 232) d.fillRect(caret, 58, 2, 16, ui::kAccent);
        if (!meaning) {
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(ui::kFaint, ui::kBg);
            std::string raw = "romaji: " + view.romaji;
            d.drawString(raw.c_str(), 86, 88);
        }
        drawHintLeft(d, "Enter: check  Esc: card", ui::kFaint, 5, 125);
    }
}

}  // namespace kk::app

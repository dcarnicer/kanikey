// Lesson screens drawing. Pure view code: no app globals, no present(), no
// startWrite()/endWrite() (the caller owns the transfer window).
#include "app/LessonScreens.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "ui/Text.h"
#include "ui/Widgets.h"
#include "wk/Utf8.h"

namespace kk::app {

using namespace ui;

void drawLessonListScreen(lgfx::LovyanGFX& d, kk::GlyphPack& pack,
                          const LessonListView& view) {
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
    int top = static_cast<int>(view.selectedIndex) - kVisible / 2;
    if (top < 0) top = 0;
    if (total > static_cast<size_t>(kVisible) && top > static_cast<int>(total) - kVisible)
        top = static_cast<int>(total) - kVisible;
    if (total <= static_cast<size_t>(kVisible)) top = 0;
    for (int row = 0; row < kVisible && top + row < static_cast<int>(total); ++row) {
        const int i = top + row;
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
                promptDrawn = pack.drawRadical(d, s->id, 20, y + 10, 20, 18, ui::kText,
                                               ui::kSurface,
                                               kk::ClipRect{10, y + 1, 30, y + 19});
            } else if (!s->characters.empty()) {
                std::string first = s->characters;
                const char* p = first.c_str();
                kk::utf8Next(&p);
                first.resize(static_cast<size_t>(p - first.c_str()));
                promptDrawn = pack.drawTextCentered(d, first, 20, 20, y + 10, 20, 20,
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
        d.setTextColor(
            i == static_cast<int>(view.selectedIndex) ? ui::kAccentHi : ui::kFaint,
            i == static_cast<int>(view.selectedIndex) ? ui::kSurfaceHi : ui::kBg);
        d.setTextDatum(lgfx::top_right);
        d.drawString(">", 230, y + 1);
        d.setTextDatum(lgfx::top_left);
    }

    if (total > kVisible) {
        constexpr int kTrackY = 18;
        constexpr int kTrackH = 100;
        d.fillRect(236, kTrackY, 2, kTrackH, ui::kLine);
        const int thumbH = std::max(10, kTrackH * kVisible / static_cast<int>(total));
        const int thumbY =
            kTrackY + (kTrackH - thumbH) * top / static_cast<int>(total - kVisible);
        d.fillRect(236, thumbY, 2, thumbH, ui::kAccentHi);
    }

    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("Enter: open  Esc: home", 6, 125);
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
    d.drawString("Esc: home", 8, 125);
}

void drawLessonCardScreen(lgfx::LovyanGFX& d, kk::GlyphPack& pack,
                          const LessonCardView& view) {
    const kk::Subject* s = view.subject;
    if (!s) return;

    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("LESSON", 6, 3);
    char pos[20];
    snprintf(pos, sizeof pos, "%u/%u", static_cast<unsigned>(view.index + 1),
             static_cast<unsigned>(view.total));
    char page[20];
    snprintf(page, sizeof page, "p%u/%u", static_cast<unsigned>(view.page + 1),
             static_cast<unsigned>(view.pages));
    // Keep both counters in the header row: the page is right-aligned and the
    // item position sits to its left. In the 6 px font, "p12/12" plus "10/10"
    // and the 8 px gap still leave the header's left side free.
    constexpr int kHeaderRight = 233;
    constexpr int kHeaderGap = 8;
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString(page, kHeaderRight, 5);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString(pos, kHeaderRight - d.textWidth(page) - kHeaderGap, 5);
    d.setTextDatum(lgfx::top_left);

    d.fillRect(0, 14, 240, 2, ui::kSurfaceHi);
    if (view.total > 0) {
        const int progressWidth = static_cast<int>(240u * view.index / view.total);
        d.fillRect(0, 14, progressWidth, 2, ui::kAccentHi);
    }

    d.fillRect(4, 20, 74, 64, ui::kSurface);
    d.drawRect(4, 20, 74, 64, ui::kLine);
    if (!drawSubjectPrompt(d, pack, *s, 41, 52, 70, 62, 60, ui::kText, ui::kSurface,
                           kk::ClipRect{6, 22, 76, 82}))
        drawMissingGlyph(d, pack, 41, 52);

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
        if (!pack.drawTextFit(d, reading, 86, 80, 32, 148, ui::kReading, ui::kBg,
                              kk::ClipRect{86, 50, 236, 88}) &&
            !drawJapaneseFit(d, pack, reading, 86, 232, 80, 148, ui::kReading, ui::kBg,
                             kk::ClipRect{86, 50, 236, 88})) {
            d.setTextFont(1);
            d.setTextSize(1);
            d.setTextColor(ui::kWarn, ui::kBg);
            d.drawString("no glyph", 86, 58);
        }
    }

    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(ui::kDim, ui::kBg);
    for (size_t i = 0; i < 3; ++i) {
        const size_t idx = view.page * 3 + i;
        if (idx >= view.lines.size()) break;
        d.drawString(view.lines[idx].c_str(), 5, 92 + static_cast<int>(i) * 9);
    }

    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("Space p  Q quiz", 5, 125);
    d.setTextDatum(lgfx::top_right);
    d.drawString("Enter start  Esc list", 233, 125);
    d.setTextDatum(lgfx::top_left);
}

void drawLessonQuizScreen(lgfx::LovyanGFX& d, kk::GlyphPack& pack,
                          const LessonQuizView& view) {
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
    if (!drawSubjectPrompt(d, pack, s, 41, 52, 70, 62, 60, ui::kText, ui::kSurface,
                           kk::ClipRect{6, 22, 76, 82}))
        drawMissingGlyph(d, pack, 41, 52);

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
        d.drawString("Enter: lesson card  Esc: card", 5, 125);
    } else if (view.feedback) {
        const bool ok = view.status == kk::AnswerStatus::Correct;
        const bool warn = view.status == kk::AnswerStatus::Warning;
        drawPill(d, 86, 20, ok ? "CORRECT" : warn ? "CHECK" : "NOT QUITE",
                 ok ? ui::kOk : warn ? ui::kWarn : ui::kErr,
                 ok ? ui::kOkBg : warn ? ui::kSurfaceHi : ui::kErrBg);
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(ui::kFaint, ui::kBg);
        const bool japanese = !meaning;
        std::string typed = view.typed;
        std::string answer = view.answer;
        if (japanese) {
            d.drawString("Y", 86, 40);
            bool typedDrawn = false;
            if (!typed.empty())
                typedDrawn = drawJapaneseFit(d, pack, typed, 94, 232, 56, 142, ui::kDim,
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
            bool answerDrawn = drawJapaneseFit(
                d, pack, answer, 94, 232, 86, 142,
                ok ? ui::kOk : warn ? ui::kWarn : ui::kErr, ui::kBg,
                kk::ClipRect{94, 68, 236, 88});
            if (!answerDrawn) {
                d.setTextFont(1);
                d.setTextSize(1);
                d.setTextColor(ui::kWarn, ui::kBg);
                d.drawString("no glyph", 94, 74);
            }
            if (!ok)
                drawWrapped(d, view.message, 86, 96, 150, 2, ui::kFaint, ui::kBg);
            else if (!s.readingNote.empty())
                drawWrapped(d, "Reading note: " + s.readingNote, 86, 96, 150, 2, ui::kFaint,
                            ui::kBg);
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
            d.setTextFont(1);
            d.setTextSize(1);
            d.setTextColor(ok ? ui::kOk : warn ? ui::kWarn : ui::kErr, ui::kBg);
            while (answer.size() > 1 && d.textWidth(answer.c_str()) > 150) answer.pop_back();
            d.drawString(answer.c_str(), 86, 74);
            if (!ok)
                drawWrapped(d, view.message, 86, 96, 150, 2, ui::kFaint, ui::kBg);
            else if (!s.meaningNote.empty())
                drawWrapped(d, "Note: " + s.meaningNote, 86, 96, 150, 2, ui::kFaint,
                            ui::kBg);
        }
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(ui::kFaint, ui::kBg);
        d.drawString("Enter: continue  Esc: card", 5, 125);
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
        } else if (pack.drawTextRight(d, shown, 230, 80, 32, ui::kText, ui::kSurfaceHi,
                                      kk::ClipRect{88, 54, 234, 86}) ||
                   pack.drawTextRight(d, shown, 230, 80, 20, ui::kText, ui::kSurfaceHi,
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
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(ui::kFaint, ui::kBg);
        d.drawString("Enter: check  Esc: card", 5, 125);
    }
}

}  // namespace kk::app

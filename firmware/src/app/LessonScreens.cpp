// Lesson screens drawing. Pure view code: no app globals, no present(), no
// startWrite()/endWrite() (the caller owns the transfer window).
#include "app/LessonScreens.h"

#include <cstdio>
#include <cstring>

#include "app/ButtonHint.h"
#include "ui/HintBar.h"
#include "ui/ListView.h"
#include "ui/Text.h"
#include "ui/UiFont.h"
#include "ui/Widgets.h"
#include "wk/Text.h"
#include "wk/Utf8.h"

namespace kk::app {

using namespace ui;

namespace {

// The feedback body (REV-01/03): under the pill, above the hint bar, 150 px
// wide from x 86. Its scroll track sits at x 238, as on the review screen.
constexpr int kBodyX = 86;
constexpr int kBodyY = 36;
constexpr int kBodyW = 150;
constexpr int kBodyH = 82;

// The body's pieces, from the same fields the review feedback uses. A warning
// carries only its message: no typed row and no answer row.
kk::FeedbackContent feedbackContent(const LessonQuizView& view) {
    kk::FeedbackContent c;
    c.japanese = !view.meaning;
    c.warning = view.status == kk::AnswerStatus::Warning;
    c.correct = view.status == kk::AnswerStatus::Correct;
    // REV-10: an unconverted romaji tail stays in the typed row; the layout
    // splits it into a Latin piece, so it never goes through the kana pack.
    c.typed = view.typed + view.pending;
    c.answers = view.answers;
    c.synonyms = view.synonyms;
    c.notes = kk::feedbackNoteLines(view.subject, view.meaning, view.status, view.message);
    return c;
}

struct LessonListCtx {
    const LessonListView* view = nullptr;
    kk::GlyphPack* fontPack = nullptr;
    kk::GlyphPack* radicalPack = nullptr;
};

void drawLessonRow(lgfx::LovyanGFX& d, int index, int x, int y, int width, bool focus,
                   const void* ctx) {
    (void)width;
    const LessonListCtx& list = *static_cast<const LessonListCtx*>(ctx);
    const LessonRowView& item = list.view->rows[static_cast<size_t>(index)];
    const kk::Subject* s = item.subject;
    d.fillRect(x, y, 22, 20, ui::kSurface);
    d.drawRect(x, y, 22, 20, ui::kLine);
    bool promptDrawn = false;
    if (s) {
        if (s->type == kk::SubjectType::Radical) {
            promptDrawn = list.radicalPack->drawRadical(
                d, s->id, x + 11, y + 10, 20, 18, ui::kText, ui::kSurface,
                kk::ClipRect{x + 1, y + 1, x + 21, y + 19});
        } else if (!s->characters.empty()) {
            std::string first = s->characters;
            const char* p = first.c_str();
            kk::utf8Next(&p);
            first.resize(static_cast<size_t>(p - first.c_str()));
            // drawTextCentered(text, prefer, cx, cy, maxW, maxH, ...): the size
            // comes before the centre.
            promptDrawn = list.fontPack->drawTextCentered(
                d, first, 20, x + 11, y + 10, 20, 20, ui::kText, ui::kSurface,
                kk::ClipRect{x + 1, y + 1, x + 21, y + 19});
        }
    }
    if (!promptDrawn) {
        ui::applyFont(d, ui::UiFont::Base);
        d.setTextColor(ui::kWarn, ui::kSurface);
        d.setTextDatum(lgfx::middle_center);
        d.drawString("?", x + 11, y + 10);
        d.setTextDatum(lgfx::top_left);
    }

    const char* kind = !s ? "?"
                       : s->type == kk::SubjectType::Vocabulary ? "VOCAB"
                       : s->type == kk::SubjectType::Radical  ? "RAD"
                                                              : "KANJI";
    char label[72];
    snprintf(label, sizeof label, "%s L%d  %s", kind, s ? s->level : 0,
             item.meaning.c_str());
    ui::applyFont(d, ui::UiFont::Small);
    d.setTextColor(focus ? ui::kText : ui::kDim, focus ? ui::kSurfaceHi : ui::kBg);
    char* end = label + strlen(label);
    while (end > label + 1 && d.textWidth(label) > 194) *--end = '\0';
    d.drawString(label, x + 25, y + 1);
    d.setTextColor(item.selected ? ui::kAccentHi : ui::kFaint,
                   focus ? ui::kSurfaceHi : ui::kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString(item.selected ? "*" : ">", 230, y + 1);
    d.setTextDatum(lgfx::top_left);
}

}  // namespace

void drawLessonListScreen(lgfx::LovyanGFX& d, kk::GlyphPack& fontPack,
                          kk::GlyphPack& radicalPack, const LessonListView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    ui::applyFont(d, ui::UiFont::Base);
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
    ui::ListStyle style;
    style.x = 4;
    style.y = 18;
    style.width = 228;
    style.rowHeight = 20;
    style.textInset = 5;  // the prompt box starts at x=9
    style.focusOffsetY = -1;
    style.focusHeight = 19;
    style.trackX = 236;
    style.trackHeight = 100;
    LessonListCtx ctx{&view, &fontPack, &radicalPack};
    ui::drawList(d, window, drawLessonRow, &ctx, style);

    if (!view.note.empty()) {
        ui::applyFont(d, ui::UiFont::Small);
        d.setTextColor(ui::kWarn, ui::kBg);
        d.drawString(view.note.c_str(), 62, 5);
    }
    if (tgc::board::buttonsOnly())
        drawHintLeft(d,
                     ui::fitHint(d, {"A: next  hold B: prev  hold A: open",
                                     "A next  hold B prev  hold A open", "A: next  hold A: open"}),
                     ui::kFaint, 6, 125);
    else
        drawHintLeft(d, "Space: select  Enter: study", ui::kFaint, 6, 125);
}

void drawLessonEmptyScreen(lgfx::LovyanGFX& d, const LessonEmptyView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    ui::applyFont(d, ui::UiFont::Base);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("LESSONS", 6, 3);
    d.setTextFont(4);
    d.setTextSize(1);
    d.setTextColor(ui::kText, ui::kBg);
    d.drawString("No lessons now", 8, 38);
    ui::applyFont(d, ui::UiFont::Base);
    d.setTextColor(ui::kDim, ui::kBg);
    std::string next = "Next review  ";
    next += view.nextReview;
    d.drawString(next.c_str(), 8, 82);
    ui::applyFont(d, ui::UiFont::Small);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("New lessons unlock as you level up.", 8, 102);
    drawHintLeft(d, pickHint("Esc: home", "B: home"));
}

void drawLessonCardScreen(lgfx::LovyanGFX& d, kk::GlyphPack& fontPack,
                          kk::GlyphPack& radicalPack, const LessonCardView& view) {
    const kk::Subject* s = view.subject;
    if (!s) return;

    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    ui::applyFont(d, ui::UiFont::Base);
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
    ui::applyFont(d, ui::UiFont::Small);
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
    ui::applyFont(d, ui::UiFont::Small);
    drawPill(d, 86, 20, type, ui::kDim, ui::kSurfaceHi);

    ui::applyFont(d, ui::UiFont::Base);
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
            ui::applyFont(d, ui::UiFont::Base);
            d.setTextColor(ui::kWarn, ui::kBg);
            d.drawString("no glyph", 86, 58);
        }
    }

    ui::applyFont(d, ui::UiFont::Small);
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

    if (tgc::board::buttonsOnly()) {
        drawHintLeft(d, pickHint("Q quiz", "B: list"), ui::kFaint, 5, 125);
        drawHintRight(d, pickHint("Enter/Space: next  Esc: list", "hold B: prev  hold A: next"),
                      ui::kFaint, 233, 125);
    } else {
        // AUD-03: the card offers R while the reading line is drawn. The audio
        // hint takes the left slot next to Q; the shorter right hint leaves it
        // room, with Space/Esc: list still in the ? table.
        // The right hint keeps `Esc` in every variant; it shortens to fit the
        // room the left label leaves (x 5 .. 233, 8 px gap).
        const char* audio = audioHintText(view.audio);
        char left[32];
        if (*audio)
            snprintf(left, sizeof left, "Q quiz  %s", audio);
        else
            snprintf(left, sizeof left, "Q quiz");
        drawHintLeft(d, left, ui::kFaint, 5, 125);
        ui::applyFont(d, ui::UiFont::Small);
        const int leftWidth = d.textWidth(left);
        const char* right = ui::fitHint(
            d, {"Enter/Space: next  Esc: list", "Enter: next  Esc: list", "Enter: next  Esc"},
            228 - 8 - leftWidth);
        drawHintRight(d, right, ui::kFaint, 233, 125);
    }
}

void drawLessonQuizScreen(lgfx::LovyanGFX& d, kk::GlyphPack& fontPack,
                          kk::GlyphPack& radicalPack, const LessonQuizView& view) {
    if (!view.subject) return;
    const kk::Subject& s = *view.subject;
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    ui::applyFont(d, ui::UiFont::Base);
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
        ui::applyFont(d, ui::UiFont::Base);
        d.setTextColor(ui::kText, ui::kBg);
        d.drawString("Practice complete", 86, 40);
        ui::applyFont(d, ui::UiFont::Small);
        d.setTextColor(ui::kDim, ui::kBg);
        char mistakes[32];
        snprintf(mistakes, sizeof mistakes, "%u mistake%s",
                 static_cast<unsigned>(view.mistakes), view.mistakes == 1 ? "" : "s");
        d.drawString(mistakes, 86, 58);
        d.setTextColor(ui::kFaint, ui::kBg);
        d.drawString("No /start sent.", 86, 76);
        drawHintLeft(d, pickHint("Enter: lesson card  Esc: card", "hold A: card  B: back"),
                     ui::kFaint, 5, 125);
    } else if (view.feedback) {
        const bool ok = view.status == kk::AnswerStatus::Correct;
        const bool warn = view.status == kk::AnswerStatus::Warning;
        drawPill(d, 86, 20, ok ? "CORRECT" : warn ? "CHECK" : "NOT QUITE",
                 ok ? ui::kOk : warn ? ui::kWarn : ui::kErr,
                 ok ? ui::kOkBg : warn ? ui::kSurfaceHi : ui::kErrBg,
                 ok ? &ui::kIconCheck : warn ? nullptr : &ui::kIconCross);
        // REV-01/03: the body scrolls with Up/Down and is never cut to a line
        // count. A warning keeps the typed input and shows only its message, so
        // the answer stays hidden.
        const bool scrollable =
            drawFeedbackBody(d, fontPack, kk::buildFeedbackPieces(feedbackContent(view)), kBodyX,
                             kBodyY, kBodyW, kBodyH, view.feedbackScroll, ui::kBg);
        // The scroll keys are named only when the body overflows. The long
        // form keeps Esc when it fits (230 px from x 5).
        if (warn) {
            drawHintLeft(d,
                         scrollable ? fitHint(d, {"Enter: edit  ;/. scroll  Esc: card",
                                                  "Enter: edit  ;/. scroll"},
                                              230)
                                    : pickHint("Enter: edit  Esc: card", "hold A: edit  B: card"),
                         ui::kFaint, 5, 125);
        } else {
            drawHintLeft(d,
                         scrollable ? fitHint(d, {"Enter: continue  ;/. scroll  Esc: card",
                                                  "Enter: continue  ;/. scroll"},
                                              230)
                                    : "Enter: continue  Esc: card",
                         ui::kFaint, 5, 125);
        }
    } else {
        drawPill(d, 86, 20, meaning ? "MEANING" : "READING",
                 meaning ? ui::kAccentHi : ui::kReading, ui::kSurfaceHi);
        ui::applyFont(d, ui::UiFont::Small);
        d.setTextColor(ui::kDim, ui::kBg);
        d.drawString(meaning ? "Type the meaning" : "Type the reading", 86, 38);

        d.fillRect(86, 52, 150, 32, ui::kSurfaceHi);
        d.drawRect(86, 52, 150, 32, ui::kLine);
        std::string shown = view.typed;
        int caret = 92;
        if (shown.empty() && view.pending.empty()) {
            ui::applyFont(d, ui::UiFont::Base);
            d.setTextColor(ui::kFaint, ui::kSurfaceHi);
            d.drawString(meaning ? "answer" : "kana", 92, 60);
        } else if (meaning) {
            ui::applyFont(d, ui::UiFont::Base);
            trimFrontToWidth(d, shown, 138);
            d.setTextColor(ui::kText, ui::kSurfaceHi);
            d.drawString(shown.c_str(), 92, 60);
            caret = 92 + d.textWidth(shown.c_str());
        } else if (drawReadingText(d, fontPack, shown, view.pending, 92, 76, 138, 60,
                                   /*alignRight=*/true, ui::kText, ui::kDim, ui::kSurfaceHi,
                                   kk::ClipRect{88, 54, 234, 86})) {
            // REV-10: 20 px kana (was 32 px), right-aligned as before.
            caret = 230;
        } else {
            ui::applyFont(d, ui::UiFont::Base);
            d.setTextColor(ui::kWarn, ui::kSurfaceHi);
            d.drawString("no glyph", 92, 60);
        }
        if (caret < 232) d.fillRect(caret, 58, 2, 16, ui::kAccent);
        if (!meaning) {
            ui::applyFont(d, ui::UiFont::Small);
            d.setTextColor(ui::kFaint, ui::kBg);
            std::string raw = "romaji: " + view.romaji;
            d.drawString(raw.c_str(), 86, 88);
        }
        drawHintLeft(d, "Enter: check  Esc: card", ui::kFaint, 5, 125);
    }
}

}  // namespace kk::app

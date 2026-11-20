// Review screens drawing. Pure view code: no app globals, no present(), no
// startWrite()/endWrite() (the caller owns the transfer window).
#include "app/ReviewScreens.h"

#include <algorithm>
#include <cstdio>
#include <string_view>

#include "ui/HintBar.h"
#include "ui/Text.h"
#include "ui/HintBar.h"
#include "ui/UiFont.h"
#include "ui/Widgets.h"
#include "wk/SessionStats.h"
#include "wk/Text.h"

namespace kk::app {
namespace {

using namespace ui;

std::string subjectMainMeaning(const kk::Subject& s) {
    for (const kk::Meaning& m : s.meanings)
        if (m.accepted) return m.text;
    if (!s.meanings.empty()) return s.meanings[0].text;
    return std::string();
}

// REV-11: the reading shown on a summary row, primary first.
std::string subjectPrimaryReading(const kk::Subject& s) {
    for (const kk::Reading& r : s.readings)
        if (r.accepted && r.primary) return r.text;
    for (const kk::Reading& r : s.readings)
        if (r.accepted) return r.text;
    return std::string();
}

// REV-11: the meaning column is the subject's primary meaning, not just the
// first accepted one.
std::string subjectPrimaryMeaning(const kk::Subject& s) {
    for (const kk::Meaning& m : s.meanings)
        if (m.accepted && m.primary) return m.text;
    return subjectMainMeaning(s);
}

const char* kindName(kk::SubjectType type) {
    return type == kk::SubjectType::Vocabulary ? "VOCAB"
           : type == kk::SubjectType::Radical  ? "RADICAL"
                                                : "KANJI";
}

// The feedback body (REV-01/03): under the pill, above the hint bar, 136 px
// wide from x 100. Its scroll track sits at x 238.
constexpr int kBodyX = 100;
constexpr int kBodyY = 36;
constexpr int kBodyW = 136;
constexpr int kBodyH = 82;

// The body's pieces: the typed text, the accepted answers and the REV-03 note
// lines. A warning carries only its message, so the answer cannot leak.
kk::FeedbackContent feedbackContent(const ReviewView& view) {
    kk::FeedbackContent c;
    c.japanese = view.part == kk::ReviewPart::Reading;
    c.warning = view.status == kk::AnswerStatus::Warning;
    c.correct = view.status == kk::AnswerStatus::Correct;
    // REV-10: an unconverted romaji tail stays in the typed row; the layout
    // splits it into a Latin piece, so it never goes through the kana pack.
    c.typed = view.typed + view.pending;
    c.answers = view.answers;
    c.synonyms = view.synonyms;
    c.notes = kk::feedbackNoteLines(view.subject, view.part == kk::ReviewPart::Meaning,
                                    view.status, view.message, view.hideKana);
    return c;
}

// REV-11: the first code point of a subject's character string, for the small
// 20 px prompt on a mistake row. Multi-character vocabulary shows its first
// character, like the review prompt's compact form.
std::string firstCodePoint(const std::string& text) {
    const char* p = text.c_str();
    const char* start = p;
    if (kk::utf8Next(&p) == 0) return std::string();
    return std::string(start, static_cast<size_t>(p - start));
}

// REV-11: one summary mistake row. Left to right: the prompt character, the
// primary meaning plus the wrong-answer/stage line, and the reading. The
// selected row gets the highlight background the whole way across.
void drawSummaryMistakeRow(lgfx::LovyanGFX& d, kk::GlyphPack& fontPack,
                           kk::GlyphPack& radicalPack, const ReviewSummaryItem& item,
                           int x, int y, int w, int h, bool selected) {
    const uint16_t bg = selected ? ui::kSurfaceHi : ui::kBg;
    if (selected) d.fillRect(x, y, w, h, bg);
    const kk::Subject* s = item.subject;
    const kk::ClipRect clip{x, y, x + w, y + h};
    if (s) {
        const int cx = x + 12;
        const int cy = y + h / 2;
        // A 20 px type-coloured chip behind the glyph (WaniKani-style). On
        // paperdiy the chip is skipped and the row stays as it was.
        const uint16_t chip = subjectTypeColor(s->type);
        const uint16_t glyphBg = kTypeColours ? chip : bg;
        if (kTypeColours) d.fillRect(cx - 10, cy - 10, 20, 20, chip);
        const std::string prompt = firstCodePoint(s->characters);
        const bool drawn =
            (s->type == kk::SubjectType::Radical &&
             radicalPack.drawRadical(d, s->id, cx, cy, 20, 20, ui::kText, glyphBg, clip)) ||
            (!prompt.empty() &&
             fontPack.drawTextCentered(d, prompt, 20, cx, cy, 20, 20, ui::kText, glyphBg, clip));
        if (!drawn) {
            if (kTypeColours) d.fillRect(cx - 10, cy - 10, 20, 20, bg);
            ui::applyFont(d, ui::UiFont::Small);
            d.setTextColor(ui::kWarn, bg);
            d.drawString("?", x + 9, y + 8);
        }
    }

    // The reading column is fixed so rows line up; a missing reading simply
    // leaves it empty (radicals).
    const int textX = x + 26;
    const int right = x + w - 4;
    const int readLeft = right - 78;
    const int textRight = readLeft - 5;

    ui::applyFont(d, ui::UiFont::Base);
    std::string meaning = s ? subjectPrimaryMeaning(*s) : std::string();
    if (meaning.empty()) meaning = "-";
    while (meaning.size() > 1 && d.textWidth(meaning.c_str()) > textRight - textX)
        meaning.pop_back();
    d.setTextColor(ui::kText, bg);
    d.drawString(meaning.c_str(), textX, y + 2);

    if (s) {
        const std::string reading = subjectPrimaryReading(*s);
        const kk::ClipRect readClip{readLeft, y, right, y + h};
        if (!reading.empty() &&
            !drawJapaneseFit(d, fontPack, reading, readLeft, right, y + 18, 78,
                             ui::kReading, bg, readClip)) {
            ui::applyFont(d, ui::UiFont::Small);
            d.setTextColor(ui::kWarn, bg);
            d.drawString("no glyph", readLeft, y + 8);
        }
    }

    // The stage sits at the right edge of the text column; `pending` when the
    // POST has not answered this assignment yet.
    char stage[24];
    kk::formatSummaryStage(item.stageValid, item.stageFrom, item.stageTo, stage, sizeof stage);
    ui::applyFont(d, ui::UiFont::Small);
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(item.stageValid && item.stageTo > item.stageFrom ? ui::kOk : ui::kFaint,
                   bg);
    d.drawString(stage, textRight, y + 13);
    d.setTextDatum(lgfx::top_left);

    // The last typed wrong meaning when it is known; otherwise both archived
    // counters. The typed reading is kept but not drawn: kana needs a 20 px
    // pack glyph and the reading column already shows the accepted answer.
    char count[40];
    snprintf(count, sizeof count, "M%u R%u", static_cast<unsigned>(item.incorrectMeaning),
             static_cast<unsigned>(item.incorrectReading));
    std::string wrong =
        item.incorrectMeaning > 0 && !item.wrongMeaning.empty()
            ? "x " + item.wrongMeaning
            : std::string(count);
    const int wrongMax = textRight - textX - 50;  // reserve the stage text
    while (wrong.size() > 1 && d.textWidth(wrong.c_str()) > wrongMax) wrong.pop_back();
    d.setTextColor(item.incorrectMeaning || item.incorrectReading ? ui::kWarn : ui::kFaint, bg);
    d.drawString(wrong.c_str(), textX, y + 13);
}

}  // namespace

void drawReviewScreen(lgfx::LovyanGFX& d, kk::GlyphPack& fontPack,
                      kk::GlyphPack& radicalPack, const ReviewView& view) {
    const kk::Subject* s = view.subject;
    if (!s) return;
    const kk::ReviewPart part = view.part;

    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    ui::applyFont(d, ui::UiFont::Base);
    d.setTextColor(ui::kDim, ui::kBg);
    if (view.wrapUpCount > 0) {
        // REV-05: the count is the pruned queue, so the counter below matches.
        char wrap[24];
        snprintf(wrap, sizeof wrap, "WRAP UP %d", view.wrapUpCount);
        d.drawString(wrap, 6, 3);
    } else {
        d.drawString(view.header ? view.header : "REVIEWS", 6, 3);
    }
    if (view.srsStage > 0 && !view.minimalHeader) {
        char srs[24];
        if (s->hasReviewStats)
            snprintf(srs, sizeof srs, "SRS %u  %u%%", static_cast<unsigned>(view.srsStage),
                     static_cast<unsigned>(s->percentageCorrect));
        else
            snprintf(srs, sizeof srs, "SRS %u", static_cast<unsigned>(view.srsStage));
        d.setTextFont(0);
        d.setTextColor(ui::kFaint, ui::kBg);
        d.drawString(srs, 62, 5);
    }
    char right[24];
    snprintf(right, sizeof right, "%d/%d", view.position, view.total);
    d.setTextFont(0);
    d.setTextDatum(lgfx::top_right);
    d.drawString(right, 233, 5);
    d.setTextDatum(lgfx::top_left);
    d.fillRect(0, 14, 240, 2, ui::kSurfaceHi);
    if (view.total > 0)
        d.fillRect(0, 14, 240 * (view.position - 1) / view.total, 2, ui::kAccentHi);

    drawSubjectPanel(d, fontPack, radicalPack, *s, 4, 20, 88, 88, 48, 56, 78, 80, 64);
    char type[24];
    snprintf(type, sizeof type, "%s L%d", kindName(s->type), s->level);
    ui::applyFont(d, ui::UiFont::Small);
    drawPill(d, 48 - (d.textWidth(type) + 8) / 2, 92, type, ui::kDim, ui::kSurfaceHi);

    bool scrollable = false;  // the feedback body overflows: the hint names the keys
    if (!view.feedback) {
        const bool meaning = part == kk::ReviewPart::Meaning;
        drawPill(d, 100, 20, meaning ? "MEANING" : "READING",
                 meaning ? ui::kAccentHi : ui::kReading, ui::kSurfaceHi);
        ui::applyFont(d, ui::UiFont::Small);
        d.setTextColor(ui::kDim, ui::kBg);
        d.drawString(meaning ? "Type the meaning" : "Type the reading", 100, 38);

        d.fillRect(100, 52, 136, 32, ui::kSurfaceHi);
        d.drawRect(100, 52, 136, 32, ui::kLine);
        std::string shown = view.typed;
        int caret = 106;
        if (shown.empty() && view.pending.empty()) {
            ui::applyFont(d, ui::UiFont::Base);
            d.setTextColor(ui::kFaint, ui::kSurfaceHi);
            d.drawString(meaning ? "answer" : "kana", 106, 60);
        } else if (meaning) {
            ui::applyFont(d, ui::UiFont::Base);
            trimFrontToWidth(d, shown, 128);
            d.setTextColor(ui::kText, ui::kSurfaceHi);
            d.drawString(shown.c_str(), 106, 60);
            caret = 106 + d.textWidth(shown.c_str());
        } else if (drawReadingText(d, fontPack, shown, view.pending, 106, 76, 122, 60,
                                   /*alignRight=*/true, ui::kText, ui::kDim, ui::kSurfaceHi,
                                   kk::ClipRect{102, 54, 234, 86})) {
            // REV-10: 20 px kana, right-aligned so the caret end stays in view.
            caret = 230;
        } else {
            ui::applyFont(d, ui::UiFont::Base);
            d.setTextColor(ui::kWarn, ui::kSurfaceHi);
            d.drawString("no glyph", 106, 60);
        }
        if (caret < 232) d.fillRect(caret, 58, 2, 16, ui::kAccent);

        if (!meaning) {
            ui::applyFont(d, ui::UiFont::Small);
            d.setTextColor(ui::kFaint, ui::kBg);
            std::string raw = "romaji: " + view.romaji;
            d.drawString(raw.c_str(), 100, 88);
        }
    } else {
        const bool ok = view.status == kk::AnswerStatus::Correct;
        const bool warn = view.status == kk::AnswerStatus::Warning;
        drawPill(d, 100, 20, ok ? "CORRECT" : warn ? "CHECK" : "NOT QUITE",
                 ok ? ui::kOk : warn ? ui::kWarn : ui::kErr,
                 ok ? ui::kOkBg : warn ? ui::kSurfaceHi : ui::kErrBg,
                 ok ? &ui::kIconCheck : warn ? nullptr : &ui::kIconCross);
        // REV-01/03: the body wraps to the full box and scrolls with Up/Down;
        // nothing is cut to a line count. A warning shows only its message, so
        // the answer cannot leak and the user can fix the typed text.
        scrollable = drawFeedbackBody(d, fontPack,
                                      kk::buildFeedbackPieces(feedbackContent(view)), kBodyX,
                                      kBodyY, kBodyW, kBodyH, view.feedbackScroll, ui::kBg);
    }

    ui::applyFont(d, ui::UiFont::Small);
    d.setTextColor(ui::kFaint, ui::kBg);
    // The warning frame always says Esc: pause, as before.
    const bool warnFrame = view.feedback && view.status == kk::AnswerStatus::Warning;
    const char* esc = warnFrame ? "Esc: pause" : (view.escHint ? view.escHint : "Esc: quit");
    // The left hint may run from x 6 up to 8 px short of the Esc text.
    const int leftMax = 233 - d.textWidth(esc) - 8 - 6;
    if (view.sending) {
        char sending[24];
        snprintf(sending, sizeof sending, "Sending %u/%u", view.sent, view.sendTotal);
        d.drawString(sending, 6, 125);
    } else if (warnFrame) {
        d.drawString(scrollable ? "Enter: edit  ;/. scroll" : "Enter: edit", 6, 125);
    } else if (view.feedback) {
        // AUD-03: R takes the space of the detail hint when a clip is
        // available (or says no audio when it is missing); Tab: details stays
        // in the help table.
        const char* audio = audioHintText(view.audio);
        char base[48];
        if (*audio) {
            if (view.wrongShortcuts)
                snprintf(base, sizeof base, "C ok  A later  %s", audio);
            else
                snprintf(base, sizeof base, "Enter: next  %s", audio);
        } else {
            snprintf(base, sizeof base, "%s",
                     view.wrongShortcuts          ? "C ok  A later  Tab info"
                     : view.showDetailHint        ? "Enter: next  Tab: details"
                                                  : "Enter: next");
        }
        if (scrollable) {
            // The scroll keys are named only when the body overflows. The long
            // form keeps the card's key when it fits; the short form keeps the
            // answer keys (C ok / A later, or Enter: next).
            char longForm[64];
            snprintf(longForm, sizeof longForm, "%s  ;/. scroll", base);
            const char* shortForm = view.wrongShortcuts ? "C ok  A later  ;/. scroll"
                                                        : "Enter: next  ;/. scroll";
            d.drawString(ui::fitHint(d, {longForm, shortForm}, leftMax), 6, 125);
        } else {
            d.drawString(base, 6, 125);
        }
    } else {
        d.drawString("Enter: check", 6, 125);
    }
    d.setTextDatum(lgfx::top_right);
    d.drawString(esc, 233, 125);
    d.setTextDatum(lgfx::top_left);
}

void drawReviewDetailScreen(lgfx::LovyanGFX& d, kk::GlyphPack& fontPack,
                            kk::GlyphPack& radicalPack, const ReviewDetailView& view) {
    const kk::Subject* s = view.subject;
    if (!s) return;

    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    ui::applyFont(d, ui::UiFont::Base);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("ITEM DETAIL", 6, 3);
    char right[16];
    snprintf(right, sizeof right, "%d/%d", view.page + 1, view.pages);
    d.setTextFont(0);
    d.setTextDatum(lgfx::top_right);
    d.drawString(right, 233, 5);
    d.setTextDatum(lgfx::top_left);
    d.fillRect(0, 14, 240, 2, ui::kSurfaceHi);

    drawSubjectPanel(d, fontPack, radicalPack, *s, 4, 20, 74, 64, 41, 52, 70, 62, 60);
    char type[24];
    snprintf(type, sizeof type, "%s L%d", kindName(s->type), s->level);
    ui::applyFont(d, ui::UiFont::Small);
    drawPill(d, 86, 20, type, ui::kDim, ui::kSurfaceHi);

    switch (view.kind) {
        case 0: {
            ui::applyFont(d, ui::UiFont::Small);
            d.setTextColor(ui::kFaint, ui::kBg);
            d.drawString("MEANINGS", 86, 38);
            std::string meanings;
            for (const kk::Meaning& m : s->meanings)
                if (m.accepted) {
                    if (!meanings.empty()) meanings += " / ";
                    meanings += m.text;
                }
            if (!s->meaningSynonyms.empty()) {
                meanings += "  + ";
                for (size_t i = 0; i < s->meaningSynonyms.size(); ++i) {
                    if (i) meanings += ", ";
                    meanings += s->meaningSynonyms[i];
                }
            }
            if (meanings.empty()) meanings = "-";
            drawWrapped(d, meanings, 86, 48, 150, 3, ui::kText, ui::kBg);
            ui::applyFont(d, ui::UiFont::Small);
            d.setTextColor(ui::kFaint, ui::kBg);
            d.drawString("R", 86, 82);
            std::string reading;
            for (const kk::Reading& r : s->readings)
                if (r.accepted && r.primary) {
                    reading = r.text;
                    break;
                }
            if (reading.empty())
                for (const kk::Reading& r : s->readings)
                    if (r.accepted) {
                        reading = r.text;
                        break;
                    }
            if (reading.empty()) {
                d.drawString("none", 94, 88);
            } else if (!drawJapaneseFit(d, fontPack, reading, 94, 232, 98, 142, ui::kReading,
                                        ui::kBg, kk::ClipRect{94, 80, 236, 100})) {
                ui::applyFont(d, ui::UiFont::Base);
                d.setTextColor(ui::kWarn, ui::kBg);
                d.drawString("no glyph", 94, 88);
            }
            break;
        }
        case 1:
        case 2: {
            const bool meaning = view.page == 1;
            ui::applyFont(d, ui::UiFont::Small);
            d.setTextColor(ui::kFaint, ui::kBg);
            d.drawString(meaning ? "MEANING MNEMONIC" : "READING MNEMONIC", 86, 38);
            std::string text = meaning ? s->meaningMnemonic : s->readingMnemonic;
            if (text.empty()) text = "No mnemonic cached.";
            const std::string hint = meaning ? s->meaningHint : s->readingHint;
            if (!hint.empty()) text += "  Hint: " + hint;
            drawWrapped(d, text, 86, 48, 150, 6, ui::kText, ui::kBg);
            break;
        }
        case 3: {
            ui::applyFont(d, ui::UiFont::Small);
            d.setTextColor(ui::kFaint, ui::kBg);
            d.drawString("NOTES", 86, 38);
            std::string notes;
            if (!s->meaningNote.empty()) notes += "Meaning: " + s->meaningNote + " ";
            if (!s->readingNote.empty()) notes += "Reading: " + s->readingNote;
            if (notes.empty()) notes = "No study notes.";
            drawWrapped(d, notes, 86, 48, 150, 6, ui::kText, ui::kBg);
            break;
        }
        case 4: {
            ui::applyFont(d, ui::UiFont::Small);
            d.setTextColor(ui::kFaint, ui::kBg);
            d.drawString("COMPONENTS", 86, 38);
            int row = 0;
            for (const kk::SubjectRef& c : view.components) {
                if (row >= 4) break;
                const int y = 50 + row * 18;
                if (c.characters[0] != '\0')
                    fontPack.drawTextFit(d, std::string_view(c.characters), 86, y + 15, 20, 24,
                                         ui::kText, ui::kBg, kk::ClipRect{86, y, 112, y + 17});
                else {
                    ui::applyFont(d, ui::UiFont::Small);
                    d.setTextColor(ui::kFaint, ui::kBg);
                    d.drawString("RAD", 86, y + 4);
                }
                ui::applyFont(d, ui::UiFont::Base);
                d.setTextColor(ui::kDim, ui::kBg);
                std::string meaning(c.meaning);
                while (meaning.size() > 1 && d.textWidth(meaning.c_str()) > 116)
                    meaning.pop_back();
                d.drawString(meaning.c_str(), 116, y + 3);
                ++row;
            }
            if (row == 0) {
                ui::applyFont(d, ui::UiFont::Small);
                d.setTextColor(ui::kFaint, ui::kBg);
                d.drawString("No components cached.", 86, 50);
            }
            break;
        }
        case 5: {
            ui::applyFont(d, ui::UiFont::Small);
            d.setTextColor(ui::kFaint, ui::kBg);
            d.drawString("PARTS OF SPEECH", 86, 38);
            std::string parts;
            for (const std::string& p : s->partsOfSpeech) {
                if (!parts.empty()) parts += ", ";
                parts += p;
            }
            if (parts.empty()) parts = "No parts of speech.";
            drawWrapped(d, parts, 86, 48, 150, 2, ui::kText, ui::kBg);
            ui::applyFont(d, ui::UiFont::Small);
            d.setTextColor(ui::kFaint, ui::kBg);
            d.drawString("CONTEXT", 86, 68);
            std::string context;
            for (size_t i = 0; i < s->contextSentences.size() && i < 2; ++i) {
                const std::string& en = s->contextSentences[i].en;
                if (en.empty()) continue;
                if (!context.empty()) context += " ";
                context += en;
            }
            if (context.empty()) context = "No context sentences.";
            drawWrapped(d, context, 86, 78, 150, 3, ui::kDim, ui::kBg);
            break;
        }
        case 6: {
            ui::applyFont(d, ui::UiFont::Small);
            d.setTextColor(ui::kFaint, ui::kBg);
            d.drawString("RELATED", 86, 38);
            auto relatedLine = [&](const char* label, const std::vector<const kk::Subject*>& items,
                                   int y) {
                char head[40];
                snprintf(head, sizeof head, "%s: %u", label,
                         static_cast<unsigned>(items.size()));
                ui::applyFont(d, ui::UiFont::Small);
                d.setTextColor(ui::kDim, ui::kBg);
                d.drawString(head, 86, y);
                std::string names;
                for (size_t i = 0; i < items.size() && i < 2; ++i) {
                    if (!items[i]) continue;
                    const std::string meaning = subjectMainMeaning(*items[i]);
                    if (meaning.empty()) continue;
                    if (!names.empty()) names += ", ";
                    names += meaning;
                }
                if (!names.empty())
                    drawWrapped(d, names, 86, y + 10, 150, 1, ui::kFaint, ui::kBg);
            };
            relatedLine("Similar", view.similar, 48);
            relatedLine("Amalgamations", view.amalgamations, 78);
            break;
        }
    }

    ui::applyFont(d, ui::UiFont::Small);
    d.setTextColor(ui::kFaint, ui::kBg);
    // AUD-03: the audio part is added only when it fits beside the exit hint.
    // Longest first; every variant keeps Tab/Esc: back, so the card still says
    // how to leave. Widths are measured by fitHint() on the device.
    const char* audio = audioHintText(view.audio);
    char withAudio[64];
    char withAudioBack[32];
    if (*audio) {
        snprintf(withAudio, sizeof withAudio, "%s  Space: page  Tab/Esc: back", audio);
        snprintf(withAudioBack, sizeof withAudioBack, "%s  Tab/Esc: back", audio);
    } else {
        withAudio[0] = '\0';
        withAudioBack[0] = '\0';
    }
    const char* hint = ui::fitHint(d, {withAudio, "Space: page  Tab/Esc: back", withAudioBack,
                                       "Tab/Esc: back"},
                                   /* x 5 .. 233 */ 228);
    d.drawString(hint, 5, 125);
}

void drawReviewSummaryScreen(lgfx::LovyanGFX& d, kk::GlyphPack& fontPack,
                             kk::GlyphPack& radicalPack, const ReviewSummaryView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    // REV-11: the mistake list is a page of at most three rows. The selection
    // drives the window so Up/Down can walk a longer list.
    const int count = static_cast<int>(view.items.size());
    int selected = view.selected;
    if (selected < 0) selected = 0;
    if (count > 0 && selected >= count) selected = count - 1;
    const int first = kk::summaryWindowStart(selected, count, 3);

    ui::applyFont(d, ui::UiFont::Base);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString(view.practice ? "PRACTICE" : "SESSION", 6, 3);

    if (view.focus == 1) {
        // The header carries the page counter and the way back to the stats;
        // the footer below stays the same as the unfocused summary.
        ui::applyFont(d, ui::UiFont::Small);
        d.setTextDatum(lgfx::top_right);
        if (count > 0) {
            char counter[16];
            const int last = std::min(first + 3, count);
            snprintf(counter, sizeof counter, "%d-%d/%d", first + 1, last, count);
            d.drawString(counter, 233, 5);
            d.drawString("Esc: stats", 233 - d.textWidth(counter) - 8, 5);
        } else {
            d.drawString("Esc: stats", 233, 5);
        }
        d.setTextDatum(lgfx::top_left);
    }

    // REV-11: the list needs three 20 px prompt glyphs, so while it is focused
    // the big values collapse to two compact lines. Esc brings the original
    // stats layout back.
    if (view.focus == 1) {
        char line[64];
        ui::applyFont(d, ui::UiFont::Small);
        d.setTextColor(ui::kDim, ui::kBg);
        snprintf(line, sizeof line, "%u reviews  %u%%  %s", view.reviews, view.accuracy,
                 view.duration);
        d.drawString(line, 8, 15);

        if (view.practice) {
            d.setTextColor(ui::kFaint, ui::kBg);
            d.drawString("Nothing was sent to WaniKani", 8, 24);
            if (view.skipped) {
                snprintf(line, sizeof line, "%u skipped", view.skipped);
                d.setTextDatum(lgfx::top_right);
                d.setTextColor(ui::kWarn, ui::kBg);
                d.drawString(line, 232, 24);
                d.setTextDatum(lgfx::top_left);
            }
        } else {
            if (view.stageUps) {
                snprintf(line, sizeof line, "%u item%s moved up", view.stageUps,
                         view.stageUps == 1 ? "" : "s");
                d.setTextColor(ui::kOk, ui::kBg);
            } else {
                snprintf(line, sizeof line, "No stage changes");
                d.setTextColor(ui::kFaint, ui::kBg);
            }
            d.drawString(line, 8, 24);

            char outbox[40];
            if (view.sending)
                snprintf(outbox, sizeof outbox, "Sending %u/%u", view.sent, view.sendTotal);
            else if (view.attention)
                snprintf(outbox, sizeof outbox, "%u need attention", view.attention);
            else if (view.pending)
                snprintf(outbox, sizeof outbox, "%u pending", view.pending);
            else
                snprintf(outbox, sizeof outbox, "All reviews sent");
            d.setTextColor(view.sending ? ui::kWarn
                           : view.attention ? ui::kErr
                           : view.pending   ? ui::kWarn
                                            : ui::kFaint,
                           ui::kBg);
            d.setTextDatum(lgfx::top_right);
            d.drawString(outbox, 232, 24);
            d.setTextDatum(lgfx::top_left);
        }
        d.drawFastHLine(8, 33, 224, ui::kLine);

        if (count == 0) {
            ui::applyFont(d, ui::UiFont::Small);
            d.setTextColor(ui::kFaint, ui::kBg);
            d.drawString("No mistakes", 8, 50);
        } else {
            for (int i = 0; i < 3; ++i) {
                const int index = first + i;
                if (index >= count) break;
                drawSummaryMistakeRow(d, fontPack, radicalPack, view.items[index], 4,
                                      38 + i * 26, 232, 24, index == selected);
            }
        }

        if (view.sending) {
            d.fillRect(8, 121, 224, 3, ui::kSurfaceHi);
            const unsigned total = view.sendTotal ? view.sendTotal : 1;
            const unsigned sent = view.sent > total ? total : view.sent;
            d.fillRect(8, 121, static_cast<int>(224u * sent / total), 3, ui::kAccentHi);
            ui::applyFont(d, ui::UiFont::Small);
            d.setTextColor(ui::kFaint, ui::kBg);
            d.drawString("Esc: stop", 8, 125);
            return;
        }
        ui::applyFont(d, ui::UiFont::Small);
        d.setTextColor(ui::kFaint, ui::kBg);
        d.drawString(view.canNextBatch ? "Enter: home  R: next batch" : "Enter: home", 8, 125);
        return;
    }

    char reviews[16];
    snprintf(reviews, sizeof reviews, "%u", view.reviews);
    char accuracy[16];
    snprintf(accuracy, sizeof accuracy, "%u%%", view.accuracy);

    // 32 px values; a value that is too wide for its column falls back to 16 px
    // so the three columns never collide.
    auto value = [&d](const char* text, int x, int maxWidth) {
        d.setTextFont(2);
        d.setTextSize(2);
        if (d.textWidth(text) > maxWidth) d.setTextSize(1);
        d.setTextColor(ui::kText, ui::kBg);
        d.drawString(text, x, 30);
    };
    value(reviews, 8, 72);
    value(accuracy, 84, 74);
    value(view.duration, 164, 72);

    ui::applyFont(d, ui::UiFont::Small);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("REVIEWS", 8, 66);
    d.drawString("ACCURACY", 84, 66);
    d.drawString("TIME", 164, 66);

    d.drawFastHLine(8, 77, 224, ui::kLine);

    char line[48];
    if (view.practice) {
        d.setTextColor(ui::kFaint, ui::kBg);
        d.drawString("Nothing was sent to WaniKani", 8, 84);
        snprintf(line, sizeof line, "%u mistake%s", view.mistakes,
                 view.mistakes == 1 ? "" : "s");
        d.drawString(line, 8, 98);
        if (view.skipped) {
            snprintf(line, sizeof line, "%u item%s skipped", view.skipped,
                     view.skipped == 1 ? "" : "s");
            d.setTextColor(ui::kWarn, ui::kBg);
            d.drawString(line, 8, 112);
        }
        d.setTextColor(ui::kFaint, ui::kBg);
        d.drawString("Enter: home", 8, 125);
        return;
    }
    if (view.stageUps) {
        snprintf(line, sizeof line, "%u item%s moved up", view.stageUps,
                 view.stageUps == 1 ? "" : "s");
        d.setTextColor(ui::kOk, ui::kBg);
    } else {
        snprintf(line, sizeof line, "No stage changes");
        d.setTextColor(ui::kFaint, ui::kBg);
    }
    d.drawString(line, 8, 84);

    snprintf(line, sizeof line, "%u mistake%s", view.mistakes,
             view.mistakes == 1 ? "" : "s");
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString(line, 8, 98);

    char outbox[40];
    if (view.sending)
        snprintf(outbox, sizeof outbox, "Sending %u/%u", view.sent, view.sendTotal);
    else if (view.attention)
        snprintf(outbox, sizeof outbox, "%u need attention", view.attention);
    else if (view.pending)
        snprintf(outbox, sizeof outbox, "%u pending", view.pending);
    else
        snprintf(outbox, sizeof outbox, "All reviews sent");
    d.setTextColor(view.sending ? ui::kWarn
                   : view.attention ? ui::kErr
                   : view.pending   ? ui::kWarn
                                    : ui::kFaint,
                   ui::kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString(outbox, 232, 98);
    d.setTextDatum(lgfx::top_left);

    if (view.stageChangeValid || view.lastHasStats) {
        char srs[48];
        if (view.stageChangeValid && view.lastHasStats) {
            if (view.stageFrom && view.stageFrom != view.stageTo)
                snprintf(srs, sizeof srs, "SRS %u -> %u  %u%%",
                         static_cast<unsigned>(view.stageFrom),
                         static_cast<unsigned>(view.stageTo), view.lastPercentage);
            else
                snprintf(srs, sizeof srs, "SRS %u  %u%%", static_cast<unsigned>(view.stageTo),
                         view.lastPercentage);
        } else if (view.stageChangeValid) {
            if (view.stageFrom && view.stageFrom != view.stageTo)
                snprintf(srs, sizeof srs, "SRS %u -> %u",
                         static_cast<unsigned>(view.stageFrom),
                         static_cast<unsigned>(view.stageTo));
            else
                snprintf(srs, sizeof srs, "SRS stage %u", static_cast<unsigned>(view.stageTo));
        } else {
            snprintf(srs, sizeof srs, "History %u reviews, %u%%", view.lastReviewCount,
                     view.lastPercentage);
        }
        d.setTextColor(ui::kOk, ui::kBg);
        d.drawString(srs, 8, 112);
    }
    if (view.sending) {
        // SPEED-01: a 3 px progress bar under the stage line; Esc stops the
        // loop and leaves the rest pending for the next sync.
        d.fillRect(8, 121, 224, 3, ui::kSurfaceHi);
        const unsigned total = view.sendTotal ? view.sendTotal : 1;
        const unsigned sent = view.sent > total ? total : view.sent;
        d.fillRect(8, 121, static_cast<int>(224u * sent / total), 3, ui::kAccentHi);
        d.setTextColor(ui::kFaint, ui::kBg);
        d.drawString("Esc: stop", 8, 125);
        return;
    }
    d.setTextColor(ui::kFaint, ui::kBg);
    // REV-11: the stats view names the key that opens the mistake list, so the
    // list is discoverable without the help table.
    const char* hint;
    if (view.items.empty())
        hint = view.canNextBatch ? "Enter: home  R: next batch" : "Enter: home";
    else if (view.canNextBatch)
        hint = ui::fitHint(d, {"Enter: home  Tab: mistakes  R: next", "Enter  Tab: mistakes  R: next",
                               "Enter  Tab  R: next batch"});
    else
        hint = ui::fitHint(d, {"Enter: home  Tab: mistakes", "Enter  Tab: mistakes"});
    d.drawString(hint, 8, 125);
}

}  // namespace kk::app

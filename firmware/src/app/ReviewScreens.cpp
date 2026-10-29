// Review screens drawing. Pure view code: no app globals, no present(), no
// startWrite()/endWrite() (the caller owns the transfer window).
#include "app/ReviewScreens.h"

#include <cstdio>

#include "ui/Text.h"
#include "ui/Widgets.h"

namespace kk::app {
namespace {

using namespace ui;

std::string subjectMainMeaning(const kk::Subject& s) {
    for (const kk::Meaning& m : s.meanings)
        if (m.accepted) return m.text;
    if (!s.meanings.empty()) return s.meanings[0].text;
    return std::string();
}

const char* kindName(kk::SubjectType type) {
    return type == kk::SubjectType::Vocabulary ? "VOCAB"
           : type == kk::SubjectType::Radical  ? "RADICAL"
                                                : "KANJI";
}

}  // namespace

void drawReviewScreen(lgfx::LovyanGFX& d, kk::GlyphPack& pack, const ReviewView& view) {
    const kk::Subject* s = view.subject;
    if (!s) return;
    const kk::ReviewPart part = view.part;

    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("REVIEWS", 6, 3);
    char srs[24];
    if (s->hasReviewStats)
        snprintf(srs, sizeof srs, "SRS %u  %u%%", static_cast<unsigned>(view.srsStage),
                 static_cast<unsigned>(s->percentageCorrect));
    else
        snprintf(srs, sizeof srs, "SRS %u", static_cast<unsigned>(view.srsStage));
    d.setTextFont(0);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString(srs, 62, 5);
    char right[24];
    snprintf(right, sizeof right, "%d/%d", view.position, view.total);
    d.setTextFont(0);
    d.setTextDatum(lgfx::top_right);
    d.drawString(right, 233, 5);
    d.setTextDatum(lgfx::top_left);
    d.fillRect(0, 14, 240, 2, ui::kSurfaceHi);
    if (view.total > 0)
        d.fillRect(0, 14, 240 * (view.position - 1) / view.total, 2, ui::kAccentHi);

    d.fillRect(4, 20, 88, 88, ui::kSurface);
    d.drawRect(4, 20, 88, 88, ui::kLine);
    if (!drawSubjectPrompt(d, pack, *s, 48, 56, 78, 80, 64, ui::kText, ui::kSurface,
                           kk::ClipRect{6, 22, 90, 106}))
        drawMissingGlyph(d, pack, 48, 56);
    char type[24];
    snprintf(type, sizeof type, "%s L%d", kindName(s->type), s->level);
    d.setTextFont(0);
    d.setTextSize(1);
    drawPill(d, 48 - (d.textWidth(type) + 8) / 2, 92, type, ui::kDim, ui::kSurfaceHi);

    if (!view.feedback) {
        const bool meaning = part == kk::ReviewPart::Meaning;
        drawPill(d, 100, 20, meaning ? "MEANING" : "READING",
                 meaning ? ui::kAccentHi : ui::kReading, ui::kSurfaceHi);
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(ui::kDim, ui::kBg);
        d.drawString(meaning ? "Type the meaning" : "Type the reading", 100, 38);

        d.fillRect(100, 52, 136, 32, ui::kSurfaceHi);
        d.drawRect(100, 52, 136, 32, ui::kLine);
        std::string shown = view.typed;
        int caret = 106;
        if (shown.empty()) {
            d.setTextFont(1);
            d.setTextSize(1);
            d.setTextColor(ui::kFaint, ui::kSurfaceHi);
            d.drawString(meaning ? "answer" : "kana", 106, 60);
        } else if (meaning) {
            d.setTextFont(1);
            d.setTextSize(1);
            trimFrontToWidth(d, shown, 128);
            d.setTextColor(ui::kText, ui::kSurfaceHi);
            d.drawString(shown.c_str(), 106, 60);
            caret = 106 + d.textWidth(shown.c_str());
        } else if (pack.drawTextRight(d, shown, 230, 80, 32, ui::kText, ui::kSurfaceHi,
                                      kk::ClipRect{102, 54, 234, 86}) ||
                   pack.drawTextRight(d, shown, 230, 80, 20, ui::kText, ui::kSurfaceHi,
                                      kk::ClipRect{102, 54, 234, 86})) {
            caret = 230;
        } else {
            d.setTextFont(1);
            d.setTextSize(1);
            d.setTextColor(ui::kWarn, ui::kSurfaceHi);
            d.drawString("no glyph", 106, 60);
        }
        if (caret < 232) d.fillRect(caret, 58, 2, 16, ui::kAccent);

        if (!meaning) {
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(ui::kFaint, ui::kBg);
            std::string raw = "romaji: " + view.romaji;
            d.drawString(raw.c_str(), 100, 88);
        }
    } else {
        const bool ok = view.status == kk::AnswerStatus::Correct;
        const bool warn = view.status == kk::AnswerStatus::Warning;
        drawPill(d, 100, 20, ok ? "CORRECT" : warn ? "CHECK" : "NOT QUITE",
                 ok ? ui::kOk : warn ? ui::kWarn : ui::kErr,
                 ok ? ui::kOkBg : warn ? ui::kSurfaceHi : ui::kErrBg);
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(ui::kFaint, ui::kBg);
        const bool japanese = part == kk::ReviewPart::Reading;
        std::string typed = view.typed;
        std::string answer = view.answer;
        if (japanese) {
            // One-letter side labels leave the full panel width for long kana.
            d.drawString("Y", 100, 40);
            bool typedDrawn = false;
            if (!typed.empty())
                typedDrawn = drawJapaneseFit(d, pack, typed, 108, 232, 56, 128, ui::kDim,
                                             ui::kBg,
                                             kk::ClipRect{108, 38, 236, 58});
            if (!typedDrawn) {
                d.setTextFont(1);
                d.setTextSize(1);
                d.setTextColor(ui::kWarn, ui::kBg);
                d.drawString(typed.empty() ? "-" : "no glyph", 108, 44);
            }
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(ui::kFaint, ui::kBg);
            d.drawString("A", 100, 72);
            bool answerDrawn = drawJapaneseFit(
                d, pack, answer, 108, 232, 86, 128,
                ok ? ui::kOk : warn ? ui::kWarn : ui::kErr, ui::kBg,
                kk::ClipRect{108, 68, 236, 88});
            if (!answerDrawn) {
                d.setTextFont(1);
                d.setTextSize(1);
                d.setTextColor(ui::kWarn, ui::kBg);
                d.drawString("no glyph", 108, 74);
            }
            if (view.status == kk::AnswerStatus::Correct) {
                if (!s->readingNote.empty())
                    drawWrapped(d, "Reading note: " + s->readingNote, 100, 96, 136, 2,
                                ui::kFaint, ui::kBg);
            } else {
                drawWrapped(d, view.message, 100, 96, 136, 2, ui::kFaint, ui::kBg);
            }
        } else {
            d.drawString("You typed", 100, 38);
            d.setTextFont(1);
            d.setTextSize(1);
            d.setTextColor(ui::kDim, ui::kBg);
            while (typed.size() > 1 && d.textWidth(typed.c_str()) > 136) typed.erase(0, 1);
            d.drawString(typed.empty() ? "-" : typed.c_str(), 100, 47);
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(ui::kFaint, ui::kBg);
            d.drawString("Answer", 100, 65);
            d.setTextFont(1);
            d.setTextSize(1);
            d.setTextColor(ok ? ui::kOk : warn ? ui::kWarn : ui::kErr, ui::kBg);
            if (d.textWidth(answer.c_str()) > 136) d.setTextFont(0);
            while (answer.size() > 1 && d.textWidth(answer.c_str()) > 136) answer.pop_back();
            if (answer.size() < view.answer.size()) answer += "...";
            d.drawString(answer.c_str(), 100, 74);
            if (view.status == kk::AnswerStatus::Correct) {
                if (!s->meaningNote.empty())
                    drawWrapped(d, "Note: " + s->meaningNote, 100, 92, 136, 2, ui::kFaint,
                                ui::kBg);
            } else {
                drawWrapped(d, view.message, 100, 92, 136, 2, ui::kFaint, ui::kBg);
            }
        }
    }

    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString(view.feedback ? "Enter: next  Tab: details" : "Enter: check", 6, 125);
    d.setTextDatum(lgfx::top_right);
    d.drawString("Esc: quit", 233, 125);
    d.setTextDatum(lgfx::top_left);
}

void drawReviewDetailScreen(lgfx::LovyanGFX& d, kk::GlyphPack& pack,
                            const ReviewDetailView& view) {
    const kk::Subject* s = view.subject;
    if (!s) return;

    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("ITEM DETAIL", 6, 3);
    char right[16];
    snprintf(right, sizeof right, "%d/%d", view.page + 1, view.pages);
    d.setTextFont(0);
    d.setTextDatum(lgfx::top_right);
    d.drawString(right, 233, 5);
    d.setTextDatum(lgfx::top_left);
    d.fillRect(0, 14, 240, 2, ui::kSurfaceHi);

    d.fillRect(4, 20, 74, 64, ui::kSurface);
    d.drawRect(4, 20, 74, 64, ui::kLine);
    if (!drawSubjectPrompt(d, pack, *s, 41, 52, 70, 62, 60, ui::kText, ui::kSurface,
                           kk::ClipRect{6, 22, 76, 82}))
        drawMissingGlyph(d, pack, 41, 52);
    char type[24];
    snprintf(type, sizeof type, "%s L%d", kindName(s->type), s->level);
    d.setTextFont(0);
    d.setTextSize(1);
    drawPill(d, 86, 20, type, ui::kDim, ui::kSurfaceHi);

    switch (view.kind) {
        case 0: {
            d.setTextFont(0);
            d.setTextSize(1);
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
            d.setTextFont(0);
            d.setTextSize(1);
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
            } else if (!drawJapaneseFit(d, pack, reading, 94, 232, 98, 142, ui::kReading,
                                        ui::kBg, kk::ClipRect{94, 80, 236, 100})) {
                d.setTextFont(1);
                d.setTextSize(1);
                d.setTextColor(ui::kWarn, ui::kBg);
                d.drawString("no glyph", 94, 88);
            }
            break;
        }
        case 1:
        case 2: {
            const bool meaning = view.page == 1;
            d.setTextFont(0);
            d.setTextSize(1);
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
            d.setTextFont(0);
            d.setTextSize(1);
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
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(ui::kFaint, ui::kBg);
            d.drawString("COMPONENTS", 86, 38);
            int row = 0;
            for (const kk::Subject* c : view.components) {
                if (!c) continue;
                if (row >= 4) break;
                const int y = 50 + row * 18;
                if (!c->characters.empty())
                    pack.drawTextFit(d, c->characters, 86, y + 15, 20, 24, ui::kText, ui::kBg,
                                     kk::ClipRect{86, y, 112, y + 17});
                else {
                    d.setTextFont(0);
                    d.setTextSize(1);
                    d.setTextColor(ui::kFaint, ui::kBg);
                    d.drawString("RAD", 86, y + 4);
                }
                d.setTextFont(1);
                d.setTextSize(1);
                d.setTextColor(ui::kDim, ui::kBg);
                std::string meaning = subjectMainMeaning(*c);
                while (meaning.size() > 1 && d.textWidth(meaning.c_str()) > 116)
                    meaning.pop_back();
                d.drawString(meaning.c_str(), 116, y + 3);
                ++row;
            }
            if (row == 0) {
                d.setTextFont(0);
                d.setTextSize(1);
                d.setTextColor(ui::kFaint, ui::kBg);
                d.drawString("No components cached.", 86, 50);
            }
            break;
        }
        case 5: {
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(ui::kFaint, ui::kBg);
            d.drawString("PARTS OF SPEECH", 86, 38);
            std::string parts;
            for (const std::string& p : s->partsOfSpeech) {
                if (!parts.empty()) parts += ", ";
                parts += p;
            }
            if (parts.empty()) parts = "No parts of speech.";
            drawWrapped(d, parts, 86, 48, 150, 2, ui::kText, ui::kBg);
            d.setTextFont(0);
            d.setTextSize(1);
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
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(ui::kFaint, ui::kBg);
            d.drawString("RELATED", 86, 38);
            auto relatedLine = [&](const char* label, const std::vector<const kk::Subject*>& items,
                                   int y) {
                char head[40];
                snprintf(head, sizeof head, "%s: %u", label,
                         static_cast<unsigned>(items.size()));
                d.setTextFont(0);
                d.setTextSize(1);
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

    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("Space: page  Tab/Esc: back", 5, 125);
}

void drawReviewSummaryScreen(lgfx::LovyanGFX& d, const ReviewSummaryView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("SESSION", 6, 3);

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

    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("REVIEWS", 8, 66);
    d.drawString("ACCURACY", 84, 66);
    d.drawString("TIME", 164, 66);

    d.drawFastHLine(8, 77, 224, ui::kLine);

    char line[48];
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
    if (view.attention)
        snprintf(outbox, sizeof outbox, "%u need attention", view.attention);
    else if (view.pending)
        snprintf(outbox, sizeof outbox, "%u pending", view.pending);
    else
        snprintf(outbox, sizeof outbox, "All reviews sent");
    d.setTextColor(view.attention ? ui::kErr : view.pending ? ui::kWarn : ui::kFaint, ui::kBg);
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
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("Enter: home", 8, 125);
}

}  // namespace kk::app

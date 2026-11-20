// Home screen drawing. Pure view code: no app globals, no present(), no
// startWrite()/endWrite() (the caller owns the transfer window).
#include "app/HomeScreen.h"

#include <cstdio>

#include "ui/HintBar.h"
#include "ui/UiFont.h"

namespace kk::app {

void drawHomeScreen(lgfx::LovyanGFX& d, const HomeView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    ui::applyFont(d, ui::UiFont::Title);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);

    char sync[24];
    if (view.syncTotal > 0)
        snprintf(sync, sizeof sync, "sync %u/%u", static_cast<unsigned>(view.syncStep),
                 static_cast<unsigned>(view.syncTotal));
    else
        snprintf(sync, sizeof sync, "Synced %s", view.syncClock);
    ui::applyFont(d, ui::UiFont::Base);
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(view.syncTotal > 0 ? ui::kAccentHi : (view.offline ? ui::kWarn : ui::kDim),
                   ui::kBg);
    d.drawString(sync, 198, 5);

    char right[20];
    if (view.batteryPercent >= 0)
        snprintf(right, sizeof right, "%d%%", view.batteryPercent);
    else
        snprintf(right, sizeof right, "USB");
    d.setTextColor(view.batteryPercent >= 0 && view.batteryPercent < 20 ? ui::kWarn : ui::kDim,
                   ui::kBg);
    d.drawString(right, 233, 5);

    d.setTextDatum(lgfx::top_left);
    d.setTextColor(GOLD, ui::kBg);
    char lvl[16];
    snprintf(lvl, sizeof lvl, "LEVEL %d", view.signedIn ? view.level : 0);
    d.drawString(lvl, 7, 21);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString(view.signedIn ? view.username : "not signed in", 80, 21);

    d.setTextFont(2);
    d.setTextSize(2);
    d.setTextColor(ui::kText, ui::kBg);
    char due[16];
    snprintf(due, sizeof due, "%u", static_cast<unsigned>(view.dueReviews));
    d.drawString(due, 8, 36);
    char lessons[16];
    snprintf(lessons, sizeof lessons, "%u", static_cast<unsigned>(view.lessons));
    d.drawString(lessons, 164, 36);

    ui::applyFont(d, ui::UiFont::Small);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("REVIEWS DUE", 8, 73);
    d.drawString("LESSONS", 164, 73);

    ui::applyFont(d, ui::UiFont::Base);
    d.setTextColor(view.offline ? ui::kWarn : ui::kDim, ui::kBg);
    d.drawString("Next review", 8, 86);
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(ui::kText, ui::kBg);
    d.drawString(view.nextReview, 232, 86);
    d.setTextDatum(lgfx::top_left);

    // SRS distribution: one segment per stage group, then A/G/M/E/B counts.
    ui::applyFont(d, ui::UiFont::Small);
    if (view.srs.valid) {
        const uint32_t bucket[5] = {view.srs.apprentice, view.srs.guru, view.srs.master,
                                    view.srs.enlightened, view.srs.burned};
        const uint16_t colors[5] = {ui::kErr, ui::kWarn, ui::kAccentHi, ui::kReading, ui::kFaint};
        const char label[5] = {'A', 'G', 'M', 'E', 'B'};
        const uint32_t total = view.srs.total();
        if (total > 0) {
            int x = 8;
            for (int i = 0; i < 5; ++i) {
                int w = 0;
                if (i == 4) {
                    w = 232 - x;
                } else {
                    w = static_cast<int>((static_cast<uint64_t>(bucket[i]) * 224u + total / 2u) /
                                         total);
                    if (bucket[i] > 0 && w < 3) w = 3;
                }
                if (w > 232 - x) w = 232 - x;
                if (w > 0) d.fillRect(x, 100, w - 1, 3, colors[i]);
                x += w;
            }
        }
        int x = 8;
        for (int i = 0; i < 5; ++i) {
            d.fillRect(x, 107, 4, 4, colors[i]);
            d.setTextColor(ui::kDim, ui::kBg);
            char count[16];
            snprintf(count, sizeof count, "%c %u", label[i], static_cast<unsigned>(bucket[i]));
            d.drawString(count, x + 5, 106);
            x += 43;
        }
    } else {
        d.setTextColor(ui::kFaint, ui::kBg);
        d.drawString("SRS: sync for counts", 8, 106);
    }

    ui::applyFont(d, ui::UiFont::Small);
    if (view.statusText[0]) {
        char status[56];
        snprintf(status, sizeof status, "%s%s", view.statusText,
                 view.outboxNearlyFull ? " (nearly full)" : "");
        d.setTextColor(view.levelUp ? ui::kOk : view.statusColor, ui::kBg);
        d.drawString(status, 8, 117);
    } else if (view.outboxNearlyFull) {
        d.setTextColor(ui::kWarn, ui::kBg);
        d.drawString("Outbox nearly full", 8, 117);
    } else if (view.levelLine[0]) {
        d.setTextColor(ui::kFaint, ui::kBg);
        d.drawString(view.levelLine, 8, 117);
    }
    const char* hint;
    if (view.outboxAttention) {
        hint = ui::fitHint(d, {"O: outbox  Enter  S  Esc: menu",
                               "O  Enter  S  Esc: menu", "O  S  Esc: menu"});
    } else {
        hint = ui::fitHint(d, {"Enter: reviews  L  S  Esc: menu",
                               "Enter  L  S  Esc: menu", "Enter  S  Esc: menu"});
    }
    ui::drawHintLeft(d, hint);
}

}  // namespace kk::app

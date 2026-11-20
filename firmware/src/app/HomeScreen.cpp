// Home screen drawing. Pure view code: no app globals, no present(), no
// startWrite()/endWrite() (the caller owns the transfer window).
#include "app/HomeScreen.h"

#include "app/ButtonHint.h"

#include <cstdio>

#include "ui/HintBar.h"
#include "ui/Icons.h"
#include "ui/UiFont.h"

namespace kk::app {

void drawHomeScreen(lgfx::LovyanGFX& d, const HomeView& view) {
    // The button layout only exists on a two-button board: compile-time false
    // on the Cardputer, so its image keeps no button strings.
    const bool buttons = tgc::board::buttonsOnly() && view.buttonMenu;
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
    const uint16_t syncColor =
        view.syncTotal > 0 ? ui::kAccentHi : (view.offline ? ui::kWarn : ui::kDim);
    const int syncWidth = d.textWidth(sync);
    ui::drawIcon(d, ui::kIconSync, 198 - syncWidth - 4 - ui::kIconWidth, 3, syncColor);
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(syncColor, ui::kBg);
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
    // On a button board the status moves here (right side), so the username
    // yields to it while a status is shown.
    if (buttons && (view.statusText[0] || view.outboxNearlyFull)) {
        ui::applyFont(d, ui::UiFont::Small);
        char status[56];
        if (view.statusText[0])
            snprintf(status, sizeof status, "%s%s", view.statusText,
                     view.outboxNearlyFull ? " (nearly full)" : "");
        else
            snprintf(status, sizeof status, "Outbox nearly full");
        d.setTextDatum(lgfx::top_right);
        d.setTextColor(view.statusText[0] ? (view.levelUp ? ui::kOk : view.statusColor) : ui::kWarn,
                       ui::kBg);
        d.drawString(status, 232, 22);
        d.setTextDatum(lgfx::top_left);
        ui::applyFont(d, ui::UiFont::Base);
    } else {
        d.setTextColor(ui::kDim, ui::kBg);
        d.drawString(view.signedIn ? view.username : "not signed in", 80, 21);
    }

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
    if (view.reminderActive) {
        d.setTextColor(ui::kAccentHi, ui::kBg);
        d.drawString(view.reminder, 8, 86);
    } else {
        d.setTextColor(view.offline ? ui::kWarn : ui::kDim, ui::kBg);
        d.drawString("Next review", 8, 86);
        d.setTextDatum(lgfx::top_right);
        d.setTextColor(ui::kText, ui::kBg);
        d.drawString(view.nextReview, 232, 86);
        d.setTextDatum(lgfx::top_left);
    }

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
    if (buttons) {
        // The focus strip: the focused destination is a filled tab. Positions
        // are measured, so the four labels fit the 224 px hint width.
        static const char* const kItems[4] = {"Lessons", "Stats", "Sync", "Settings"};
        int x = 8;
        for (int i = 0; i < 4; ++i) {
            const int w = d.textWidth(kItems[i]);
            const bool on = i == view.focus;
            if (on) d.fillRect(x - 3, 115, w + 6, 12, ui::kAccentHi);
            d.setTextColor(on ? ui::kBg : ui::kDim, on ? ui::kAccentHi : ui::kBg);
            d.drawString(kItems[i], x, 117);
            x += w + 14;
        }
    } else if (view.statusText[0]) {
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
    if (view.reminderActive) {
        // REM-02 banner: any input continues the normal boot.
        hint = buttons ? ui::fitHint(d, {"any button: continue", "any button"})
                       : ui::fitHint(d, {"any key: continue", "any key"});
    } else if (buttons && view.asleep) {
        hint = ui::fitHint(d, {"A or B: wake", "A or B"});
    } else if (buttons) {
        hint = ui::fitHint(d, {"A: next  hold A: open  B: settings",
                               "A next  hold A open  B settings", "A: next  hold A: open"});
    } else if (view.outboxAttention) {
        hint = ui::fitHint(d, {"O: outbox  Enter  S  Esc: menu",
                               "O  Enter  S  Esc: menu", "O  S  Esc: menu"});
    } else {
        hint = ui::fitHint(d, {"Enter: reviews  L  S  Esc: menu",
                               "Enter  L  S  Esc: menu", "Enter  S  Esc: menu"});
    }
    ui::drawHintLeft(d, hint);
}

}  // namespace kk::app

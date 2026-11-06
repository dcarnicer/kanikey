#include "app/StatsScreen.h"

#include <algorithm>
#include <cstdio>

namespace kk::app {
namespace {

void drawForecast(lgfx::LovyanGFX& d, const StatsView& view) {
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("Next 24 hours", 8, 27);

    constexpr int kLeft = 8;
    constexpr int kBarWidth = 8;
    constexpr int kStep = 9;
    constexpr int kBaseY = 104;
    constexpr int kMaxHeight = 70;
    d.drawFastHLine(kLeft, kBaseY, 224, ui::kLine);
    if (view.forecast.empty()) {
        d.setTextFont(1);
        d.setTextColor(ui::kFaint, ui::kBg);
        d.drawString("No reviews in the next 24 h", 8, 58);
        return;
    }
    const uint16_t max = view.forecast.max;
    for (size_t i = 0; i < kk::kForecastSlots; ++i) {
        const uint16_t count = view.forecast.counts[i];
        if (count == 0) continue;
        int height = (static_cast<int>(count) * kMaxHeight + max - 1) / max;
        if (height < 1) height = 1;
        d.fillRect(kLeft + static_cast<int>(i) * kStep, kBaseY - height, kBarWidth, height,
                   i == 0 ? ui::kBrand : ui::kAccentHi);
    }
    d.setTextFont(0);
    d.setTextColor(ui::kFaint, ui::kBg);
    for (int i = 0; i < 4; ++i)
        d.drawString(view.labels[i], kLeft + i * 6 * kStep, kBaseY + 3);
}

void drawSrs(lgfx::LovyanGFX& d, const StatsView& view) {
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("SRS distribution", 8, 27);
    if (!view.srs.valid) {
        d.setTextColor(ui::kFaint, ui::kBg);
        d.drawString("Sync for counts", 8, 58);
        return;
    }
    const char* names[5] = {"Apprentice", "Guru", "Master", "Enlightened", "Burned"};
    const uint16_t colors[5] = {ui::kErr, ui::kWarn, ui::kAccentHi, ui::kReading, ui::kFaint};
    const uint32_t counts[5] = {view.srs.apprentice, view.srs.guru, view.srs.master,
                                view.srs.enlightened, view.srs.burned};
    const uint32_t total = view.srs.total();
    for (int i = 0; i < 5; ++i) {
        const int y = 38 + i * 15;
        d.setTextFont(0);
        d.setTextColor(ui::kDim, ui::kBg);
        d.drawString(names[i], 8, y);
        d.setTextDatum(lgfx::top_right);
        d.setTextColor(ui::kText, ui::kBg);
        char count[12];
        snprintf(count, sizeof count, "%u", static_cast<unsigned>(counts[i]));
        d.drawString(count, 92, y);
        d.setTextDatum(lgfx::top_left);
        d.fillRect(98, y, 134, 6, ui::kSurface);
        int width = total ? static_cast<int>((static_cast<uint64_t>(counts[i]) * 134u) / total) : 0;
        if (counts[i] > 0 && width < 2) width = 2;
        if (width > 134) width = 134;
        if (width > 0) d.fillRect(98, y, width, 6, colors[i]);
    }
}

void drawLevel(lgfx::LovyanGFX& d, const StatsView& view) {
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("Level progress", 8, 27);
    if (view.level.level <= 0) {
        d.setTextColor(ui::kFaint, ui::kBg);
        d.drawString("Sync for level data", 8, 58);
        return;
    }
    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(GOLD, ui::kBg);
    char level[16];
    snprintf(level, sizeof level, "Level %d", view.level.level);
    d.drawString(level, 8, 36);

    d.setTextFont(1);
    d.setTextColor(ui::kDim, ui::kBg);
    if (view.level.days[0]) d.drawString(view.level.days, 8, 58);

    d.setTextFont(0);
    d.setTextColor(ui::kText, ui::kBg);
    if (view.level.kanjiTotal) {
        char kanji[48];
        snprintf(kanji, sizeof kanji, "kanji %u/%u (%u%%)",
                 static_cast<unsigned>(view.level.kanjiPassed),
                 static_cast<unsigned>(view.level.kanjiTotal),
                 static_cast<unsigned>(view.level.kanjiPercent));
        d.drawString(kanji, 8, 74);
        if (view.level.ninetyReached)
            d.drawString("90% reached", 8, 86);
        else {
            snprintf(kanji, sizeof kanji, "Need %u kanji for 90%%",
                     static_cast<unsigned>(view.level.needed));
            d.drawString(kanji, 8, 86);
        }
    }
    char apprentice[24];
    if (view.level.srsValid)
        snprintf(apprentice, sizeof apprentice, "A %u total",
                 static_cast<unsigned>(view.level.apprentice));
    else
        snprintf(apprentice, sizeof apprentice, "A -- total");
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString(apprentice, 8, 100);
}

}  // namespace

void drawStatsScreen(lgfx::LovyanGFX& d, const StatsView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("STATS", 6, 2);
    char page[16];
    snprintf(page, sizeof page, "%u/%u", static_cast<unsigned>(view.page + 1),
             static_cast<unsigned>(view.pages));
    d.setTextFont(0);
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString(page, 232, 5);
    d.setTextDatum(lgfx::top_left);
    d.drawFastHLine(4, 22, 232, ui::kLine);

    if (view.page == 0)
        drawForecast(d, view);
    else if (view.page == 1)
        drawSrs(d, view);
    else
        drawLevel(d, view);

    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("Esc: back  Left/Right: page", 8, 125);
}

}  // namespace kk::app

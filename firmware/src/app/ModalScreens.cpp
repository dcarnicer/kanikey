// Modal screen drawing. Pure view code: no app globals, no present(), no
// startWrite()/endWrite() (the caller owns the transfer window).
#include "app/ModalScreens.h"

#include <cstdio>

#include "ui/UiFont.h"

namespace kk::app {

void drawPauseScreen(lgfx::LovyanGFX& d, const PauseView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    ui::applyFont(d, ui::UiFont::Title);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("paused", 7, 22);

    char progress[24];
    snprintf(progress, sizeof progress, "%u/%u", view.completed, view.total);
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString(progress, 232, 5);
    d.setTextDatum(lgfx::top_left);

    static const char* kRows[] = {"Continue", "Wrap up", "End session", "Settings"};
    for (int i = 0; i < 4; ++i) {
        const int y = 40 + i * 20;
        if (i == view.selectedIndex) {
            d.fillRect(4, y - 2, 232, 16, ui::kSurfaceHi);
            d.fillRect(4, y - 2, 2, 16, ui::kAccent);
        }
        ui::applyFont(d, ui::UiFont::Base);
        d.setTextColor(i == view.selectedIndex ? ui::kText : ui::kDim,
                       i == view.selectedIndex ? ui::kSurfaceHi : ui::kBg);
        d.drawString(kRows[i], 12, y);
    }
    ui::applyFont(d, ui::UiFont::Small);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("Enter: select  Esc: continue", 8, 125);
}

void drawLowBatteryScreen(lgfx::LovyanGFX& d, const LowBatteryView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    ui::applyFont(d, ui::UiFont::Title);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextColor(ui::kWarn, ui::kBg);
    d.drawString("battery low", 7, 22);

    char pct[16];
    snprintf(pct, sizeof pct, "%d%%", view.batteryPercent >= 0 ? view.batteryPercent : 0);
    d.setTextFont(4);
    d.setTextSize(1);
    d.setTextColor(ui::kWarn, ui::kBg);
    d.setTextDatum(lgfx::middle_center);
    d.drawString(pct, 120, 62);
    d.setTextDatum(lgfx::top_left);

    ui::applyFont(d, ui::UiFont::Base);
    d.setTextColor(ui::kText, ui::kBg);
    d.drawString(view.lessonAction ? "A lesson batch may not finish."
                                   : "A review session may not finish.",
                 8, 86);
    d.setTextFont(0);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("Connect power or continue anyway.", 8, 102);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("Enter: continue  Esc: cancel", 8, 125);
}

void drawWorkloadScreen(lgfx::LovyanGFX& d, const WorkloadView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    ui::applyFont(d, ui::UiFont::Title);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextColor(ui::kWarn, ui::kBg);
    d.drawString("workload check", 7, 22);

    ui::applyFont(d, ui::UiFont::Small);
    d.setTextColor(ui::kText, ui::kBg);
    int y = 52;
    for (const std::string& reason : view.reasons) {
        if (y > 104) break;
        d.drawString(reason.c_str(), 8, y);
        y += 12;
    }
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("Enter: continue  Esc: back", 8, 125);
}

void drawLessonStartScreen(lgfx::LovyanGFX& d, const LessonStartView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    ui::applyFont(d, ui::UiFont::Title);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);
    ui::applyFont(d, ui::UiFont::Base);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("lessons", 7, 22);

    if (view.offline) {
        ui::applyFont(d, ui::UiFont::Small);
        d.setTextColor(ui::kWarn, ui::kBg);
        d.drawString("Needs Wi-Fi - your batch stays here", 8, 46);
        d.setTextColor(ui::kFaint, ui::kBg);
        d.drawString("Enter: retry  Esc: back", 8, 125);
        return;
    }

    if (view.failed) {
        d.setTextFont(1);
        d.setTextColor(ui::kWarn, ui::kBg);
        d.drawString("Could not start", 8, 42);
        ui::applyFont(d, ui::UiFont::Small);
        d.setTextColor(ui::kText, ui::kBg);
        char kept[40];
        snprintf(kept, sizeof kept, "%u item%s kept", view.total - view.started,
                 view.total - view.started == 1 ? "" : "s");
        d.drawString(kept, 8, 56);
        int y = 70;
        for (size_t i = 0; i < view.failedItems.size() && i < 3; ++i) {
            std::string line = view.failedItems[i];
            while (line.size() > 1 && d.textWidth(line.c_str()) > 224) line.pop_back();
            d.setTextColor(ui::kDim, ui::kBg);
            d.drawString(line.c_str(), 8, y);
            y += 10;
        }
        d.setTextColor(ui::kFaint, ui::kBg);
        d.drawString("Enter: retry  Esc: back", 8, 125);
        return;
    }

    char title[32];
    snprintf(title, sizeof title, "Start %u lesson%s?", view.total,
             view.total == 1 ? "" : "s");
    ui::applyFont(d, ui::UiFont::Title);
    d.setTextColor(ui::kText, ui::kBg);
    d.drawString(title, 8, 46);
    ui::applyFont(d, ui::UiFont::Base);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("They join your reviews", 8, 70);
    d.drawString("in about 4 hours.", 8, 84);
    ui::applyFont(d, ui::UiFont::Small);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("Enter: start  Esc: back", 8, 125);
}

void drawHelpScreen(lgfx::LovyanGFX& d, const HelpView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    ui::applyFont(d, ui::UiFont::Title);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);
    ui::applyFont(d, ui::UiFont::Base);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString(view.title, 7, 22);
    d.drawFastHLine(4, 36, 232, ui::kLine);

    size_t rows = view.count;
    if (rows > kHelpMaxRows) rows = kHelpMaxRows;
    for (size_t i = 0; i < rows; ++i) {
        const int y = 42 + static_cast<int>(i) * 11;
        ui::applyFont(d, ui::UiFont::Small);
        d.setTextColor(ui::kAccentHi, ui::kBg);
        d.drawString(view.rows[i].keys, 8, y);
        d.setTextDatum(lgfx::top_right);
        d.setTextColor(ui::kText, ui::kBg);
        d.drawString(view.rows[i].action, 232, y);
        d.setTextDatum(lgfx::top_left);
    }

    ui::applyFont(d, ui::UiFont::Small);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("Esc: close", 8, 125);
}

void drawSignOutScreen(lgfx::LovyanGFX& d) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    ui::applyFont(d, ui::UiFont::Title);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextColor(ui::kErr, ui::kBg);
    d.drawString("sign out", 7, 22);
    ui::applyFont(d, ui::UiFont::Base);
    d.setTextColor(ui::kText, ui::kBg);
    d.drawString("Token, cache and", 8, 46);
    d.drawString("pending reviews will", 8, 60);
    d.drawString("be removed.", 8, 74);
    d.setTextFont(0);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString("Wi-Fi stays saved.", 8, 92);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString("Enter: sign out", 8, 112);
    d.drawString("Esc: cancel", 8, 125);
}

}  // namespace kk::app

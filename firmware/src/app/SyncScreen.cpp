// Sync screen drawing. Pure view code: no app globals, no present(), no
// startWrite()/endWrite() (the caller owns the transfer window).
#include "app/SyncScreen.h"

#include <cstdio>
#include <string>

namespace kk::app {
namespace {

void drawSyncText(lgfx::LovyanGFX& d, const char* text, int x, int y, uint16_t fg,
                  int maxWidth) {
    if (!text || !text[0]) return;
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(fg, ui::kBg);
    std::string s = text;
    while (s.size() > 1 && d.textWidth(s.c_str()) > maxWidth) s.pop_back();
    d.drawString(s.c_str(), x, y);
}

}  // namespace

void drawSyncScreen(lgfx::LovyanGFX& d, const SyncView& view) {
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(ui::kBrand, ui::kBg);
    d.drawString("KaniKey", 6, 2);

    const int bat = view.batteryPercent;
    char right[16];
    if (bat >= 0)
        snprintf(right, sizeof right, "%d%%", bat);
    else
        snprintf(right, sizeof right, "USB");
    d.setTextFont(1);
    d.setTextColor(bat >= 0 && bat < 20 ? ui::kWarn : ui::kDim, ui::kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString(right, 233, 5);
    d.setTextDatum(lgfx::top_left);

    d.setTextFont(2);
    d.setTextSize(1);
    const bool allDone = view.completed >= kSyncRowCount;
    d.setTextColor(view.finished ? ui::kWarn : allDone ? ui::kOk : ui::kText, ui::kBg);
    d.drawString(view.finished ? "Sync failed" : allDone ? "Synced" : "Syncing...", 8, 24);
    char count[12];
    snprintf(count, sizeof count, "%u/%u", static_cast<unsigned>(view.completed),
             static_cast<unsigned>(kSyncRowCount));
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString(count, 233, 32);
    d.setTextDatum(lgfx::top_left);

    for (int i = 0; i < kSyncRowCount; ++i) {
        const SyncRowView& row = view.rows[i];
        const int y = 46 + i * 11;
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(ui::kDim, ui::kBg);
        d.drawString(row.label, 8, y);

        uint16_t valueColor = ui::kFaint;
        const char* status = "--";
        uint16_t statusColor = ui::kFaint;
        switch (row.state) {
            case SyncRowState::Pending: break;
            case SyncRowState::Active:
                valueColor = ui::kText;
                status = "..";
                statusColor = ui::kAccentHi;
                break;
            case SyncRowState::Done:
                valueColor = ui::kText;
                status = "ok";
                statusColor = ui::kOk;
                break;
            case SyncRowState::Warn:
                valueColor = ui::kWarn;
                status = "!";
                statusColor = ui::kWarn;
                break;
            case SyncRowState::Failed:
                valueColor = ui::kErr;
                status = "X";
                statusColor = ui::kErr;
                break;
        }
        if (row.value[0]) drawSyncText(d, row.value, 64, y, valueColor, 140);
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(statusColor, ui::kBg);
        d.setTextDatum(lgfx::top_right);
        d.drawString(status, 233, y);
        d.setTextDatum(lgfx::top_left);
    }

    if (view.finished && view.issue != SyncIssueKind::None) {
        if (view.issue == SyncIssueKind::RateLimit) {
            char rateLine[48];
            if (view.rateLimitWaitSeconds)
                snprintf(rateLine, sizeof rateLine, "Rate limited; retry in %us",
                         static_cast<unsigned>(view.rateLimitWaitSeconds));
            else
                snprintf(rateLine, sizeof rateLine, "Rate limited; retry later");
            drawSyncText(d, rateLine, 8, 112, ui::kWarn, 224);
        } else {
            drawSyncText(d, view.errorLine, 8, 112, ui::kWarn, 224);
        }
    } else {
        d.fillRect(8, 113, 224, 3, ui::kSurfaceHi);
        if (view.completed)
            d.fillRect(8, 113, 224 * view.completed / kSyncRowCount, 3, ui::kAccentHi);
    }

    const char* hint = "Esc: cancel";
    if (view.finished)
        hint = (view.issue == SyncIssueKind::Auth || view.issue == SyncIssueKind::Forbidden)
                   ? "Enter: setup  Esc: back"
                   : "Enter: retry  Esc: back";
    else if (allDone)
        hint = "";
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString(hint, 8, 125);
}

}  // namespace kk::app

// PD-04 device side: the PaperDIY sync cadence. See CadenceController.h.
#include "app/CadenceController.h"

#if defined(TGC_BOARD_PAPERDIY)
#include <esp_attr.h>

#include "board/Board.h"
#include "platform/DeviceSettings.h"
#include "platform/DevToken.h"
#include "platform/DevWifi.h"
#include "platform/WkClient.h"
#include "wk/SyncPlan.h"

namespace kk::app {
namespace {

// RTC memory: survives deep sleep, initialised to 0 by a power-on. The
// failure count drives the back-off; the retry mark ends a rate-limit wait.
RTC_DATA_ATTR uint32_t gCadFailures = 0;
RTC_DATA_ATTR uint32_t gCadRetryUtc = 0;

}  // namespace

kk::CadenceWake cadenceWakeAfter(uint32_t nowUtc, const kk::Summary* summary,
                                 const kk::ReminderRules& quiet, kk::UtcOffsetFn offsetAt) {
    // Y1 "Auto sync on wake" Off: an automatic sync is what the cadence is, so none.
    if (kk::settingsAutoSyncMode() == kk::kAutoSyncOff) return kk::CadenceWake{};
    // Not set up: no token or no saved Wi-Fi, so a sync could not run.
    if (!kk::DevToken::has() || !kk::DevWifi::hasSaved()) return kk::CadenceWake{};
    kk::CadenceInputs in;
    in.nowUtc = nowUtc;
    in.nextReviewUtc = summary ? summary->nextReviewAt(nowUtc) : 0;
    in.batteryPercent = tgc::board::batteryPercent();
    in.failures = gCadFailures;
    in.retryAfterS = gCadRetryUtc > nowUtc ? gCadRetryUtc - nowUtc : 0;
    return kk::cadenceNextWake(in, kk::CadenceConfig{}, quiet, offsetAt);
}

void cadenceNoteSync(bool ok, uint32_t nowUtc) {
    if (ok) {
        gCadFailures = 0;
        gCadRetryUtc = 0;
        return;
    }
    ++gCadFailures;
    const uint32_t wait = kk::wkRateLimitWaitSeconds();
    gCadRetryUtc = wait ? nowUtc + wait : 0;
}

uint32_t cadenceFailures() { return gCadFailures; }

}  // namespace kk::app
#endif

#pragma once
// Sync screen: the entire Sync frame, drawn from explicit view data. The app
// adapter fills SyncView from its state and keeps startWrite()/present().
#include <cstdint>

#include "board/Board.h"
#include "ui/Theme.h"

namespace kk::app {

constexpr int kSyncRowCount = 6;

enum class SyncRowState : uint8_t { Pending, Active, Done, Warn, Failed };

struct SyncRowView {
    const char* label = "";
    const char* value = "";
    SyncRowState state = SyncRowState::Pending;
};

// The issue affects the error/rate-limit line and the hint; the adapter maps
// its own sync status onto these kinds.
enum class SyncIssueKind : uint8_t { None, RateLimit, Auth, Forbidden, Other };

struct SyncView {
    int batteryPercent = -1;  // -1 draws USB instead of a percentage
    bool finished = false;
    uint8_t completed = 0;
    SyncRowView rows[kSyncRowCount];
    const char* errorLine = "";
    uint32_t rateLimitWaitSeconds = 0;
    SyncIssueKind issue = SyncIssueKind::None;
};

// Draws the Sync frame into d. The caller owns startWrite()/endWrite() and
// present(); this function only draws.
void drawSyncScreen(lgfx::LovyanGFX& d, const SyncView& view);

}  // namespace kk::app

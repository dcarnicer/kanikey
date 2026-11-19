#pragma once
// The sync flow: the blocking manual Sync screen, the non-modal automatic job
// and the per-step requests that both share.
#include <cstdint>

#include "app/AppState.h"
#include "wk/SyncPlan.h"

namespace kk::app {

void setSyncIssue(int httpCode, bool connected, int tlsError, const char* what);
void applyDeferredAuthIssue();
void clearOutboxAuthIssue();
void runProbe();
void runSummary();
void formatLevelProgressLine(char* out, size_t n);
bool runBatch();
// Manual Sync now: full plan on the six-row Sync screen.
void startSync();
// Automatic boot sync: Quick or Full, Home stays on screen (SPEED-03).
void startSyncAuto(kk::SyncKind kind, bool coldBoot);
// Performs at most one request per call; the app calls it from loop().
void syncAdvance();
void syncScreenKey(const tgc::KeyEvent& e);
void drawSync();

}  // namespace kk::app

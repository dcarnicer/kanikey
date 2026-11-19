#pragma once
// The blocking sync flow and its screen state.
#include <cstdint>

#include "app/AppState.h"

namespace kk::app {

void setSyncIssue(int httpCode, bool connected, int tlsError, const char* what);
void applyDeferredAuthIssue();
void clearOutboxAuthIssue();
void runProbe();
void runSummary();
void formatLevelProgressLine(char* out, size_t n);
bool runBatch();
void startSync();
void syncScreenKey(const tgc::KeyEvent& e);
void drawSync();

}  // namespace kk::app

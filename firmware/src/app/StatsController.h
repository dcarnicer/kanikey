#pragma once
// The read-only stats screen (forecast, SRS, level, history).
#include "app/AppState.h"

namespace kk::app {

void openStats();
void statsKey(const tgc::KeyEvent& e);
void drawStats();

}  // namespace kk::app

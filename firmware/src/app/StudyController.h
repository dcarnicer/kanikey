#pragma once
// Extra study: the menu, the critical-items list and local practice sessions.
#include "app/AppState.h"

namespace kk::app {

void loadCritical();
bool refreshPracticePool();
void openStudyMenu();
void studyMenuKey(const tgc::KeyEvent& e);
void drawStudyMenu();
void drawCriticalList();
void criticalListKey(const tgc::KeyEvent& e);

}  // namespace kk::app

#pragma once
// Boot-screen status, screen wake and the idle dim/blank/sleep tick.
#include <cstdint>

namespace kk::app {

void drawStatus();
void wakeScreen();
bool powerWakeCheck(uint32_t now);
bool powerTick(uint32_t now);
bool devForceBattery();

}  // namespace kk::app

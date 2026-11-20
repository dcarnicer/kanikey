#pragma once
// Boot-screen status, screen wake and the idle dim/blank/sleep tick.
#include <cstdint>

namespace kk {
struct Summary;
}

namespace kk::app {

void drawStatus();
void wakeScreen();
bool powerWakeCheck(uint32_t now);
bool powerTick(uint32_t now);
bool devForceBattery();

#if defined(TGC_BOARD_PAPERDIY)
// PD-04: the idle deep sleep on the PaperDIY. Draws the sleeping Home frame with
// a full-quality refresh, then arms the one timer (sync cadence and reminder)
// and returns it in microseconds for deepSleep(); 0 is no timer.
uint64_t paperIdleSleep(const kk::Summary* summary);
#endif

}  // namespace kk::app

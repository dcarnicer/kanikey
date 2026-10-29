#pragma once
// Development screens: the M0 hardware check and the radical bitmap test.
// Compiled only for KANIKEY_DEV builds; the caller owns startWrite()/present().
#ifdef KANIKEY_DEV
#include <cstdint>

#include "board/Board.h"
#include "platform/GlyphPack.h"
#include "ui/Theme.h"

namespace kk::app {

struct DebugView {
    int batteryPercent = -1;
    const char* netLine = "";
    const char* sdLine = "";
    bool sdOk = false;
    char heapLine[40] = "";
    const char* log[3] = {"", "", ""};
    bool shift = false;
    bool fn = false;
    bool ctrl = false;
    bool opt = false;
    bool alt = false;
};

// Draws the M0 debug frame into d.
void drawDebugScreen(lgfx::LovyanGFX& d, const DebugView& view);

struct RadicalTestView {
    uint32_t subjectId = 1;
};

// Draws the radical bitmap test frame into d.
void drawRadicalTestScreen(lgfx::LovyanGFX& d, kk::GlyphPack& pack,
                           const RadicalTestView& view);

}  // namespace kk::app
#endif

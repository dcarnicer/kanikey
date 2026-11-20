#pragma once
// Extra study menu and the critical-items list. Pure view code.
#include <string>
#include <vector>

#include "board/Board.h"
#include "platform/GlyphPack.h"
#include "ui/Theme.h"
#include "wk/Types.h"

namespace kk::app {

constexpr int kStudyRows = 4;

struct StudyMenuView {
    int selectedIndex = 0;
    char counts[kStudyRows][12] = {};
    char note[48] = "";
};

void drawStudyMenuScreen(lgfx::LovyanGFX& d, const StudyMenuView& view);

struct CriticalRowView {
    const kk::Subject* subject = nullptr;
    std::string meaning;
    uint8_t percentage = 0;
};

struct CriticalListView {
    std::vector<CriticalRowView> rows;
    int selectedIndex = 0;
    // Critical items whose subject could not be loaded (not on the SD catalog
    // and no network); the empty state says so instead of "No critical items".
    unsigned unavailable = 0;
};

void drawCriticalListScreen(lgfx::LovyanGFX& d, kk::GlyphPack& fontPack,
                            kk::GlyphPack& radicalPack, const CriticalListView& view);

}  // namespace kk::app

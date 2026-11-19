#pragma once
// KaniKey application: session state, input handling, services and the screen
// router. main.cpp only wires the Arduino entry points to these functions.
namespace kk::app {

void setup();
void loop();

// ONB-01: the split visual packs. openGlyphPacks() opens the font pack first,
// then the radical pack, and falls back to a v1 combined glyphs.kgp for the
// radical role when no radicals.kgp exists. `checkCrc` is only for an explicit
// check or a newly written file; the boot path skips the full-file CRC.
bool openFontPack(bool checkCrc = false);
bool openRadicalPack(bool checkCrc = false);
void openGlyphPacks(bool checkCrc = false);

}  // namespace kk::app

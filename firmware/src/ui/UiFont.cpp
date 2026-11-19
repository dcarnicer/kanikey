// Applies one of the built-in UI fonts. Only this part needs a display; the id
// mapping and metrics in UiFont.h are pure and native-tested.
#include "ui/UiFont.h"

#include "board/Board.h"

namespace kk::ui {

void applyFont(lgfx::LovyanGFX& d, UiFont font, uint8_t scale) {
    d.setTextFont(fontId(font));
    d.setTextSize(scale ? scale : 1);
}

}  // namespace kk::ui

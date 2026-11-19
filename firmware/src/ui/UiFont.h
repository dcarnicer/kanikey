#pragma once
// The built-in M5GFX/LovyanGFX fonts the UI uses, with their real pixel
// metrics. This is a primitive: screens keep their own layout and replace each
// setTextFont()/setTextSize() pair with applyFont() only when they migrate.
//
// There are no built-in 10/14/20 px fonts: the Latin UI sizes that exist are
// the 6x8 GLCD font and the 16 px bitmap font below, plus the 26 px Font4
// outside this primitive. The names map to the fonts the screens already use:
// Font0 and Font1 are the same 6x8 GLCD font (M5GFX maps setTextFont(1) to
// Font0), and Font2 is the 16 px proportional bitmap font.
#include <cstdint>

namespace lgfx {
inline namespace v1 {
class LovyanGFX;
}
}  // namespace lgfx

namespace kk::ui {

// Values are the ids setTextFont() takes.
enum class UiFont : uint8_t {
    Small = 0,  // Font0: 6x8 GLCD, hints and dense labels
    Base = 1,   // Font1: Font0 again in M5GFX, body text and labels
    Title = 2,  // Font2: 16 px, screen titles
};

// The LovyanGFX font id passed to setTextFont().
constexpr int fontId(UiFont font) { return static_cast<int>(font); }

// Unscaled line height: 8 px for Font0/Font1, 16 px for Font2.
constexpr int fontHeight(UiFont font) { return font == UiFont::Title ? 16 : 8; }

// Unscaled advance of one glyph for a monospaced font, else 0 (Title is
// proportional; measure a string with d.textWidth()). Mirrors M5GFX's own
// fontWidth(), which is 6 for Font0/Font1 and 0 for Font2.
constexpr int fontWidth(UiFont font) { return font == UiFont::Title ? 0 : 6; }

constexpr bool fontIsMonospace(UiFont font) { return fontWidth(font) != 0; }

// Line pitch in pixels for applyFont(d, font, scale). A scale of 0 is treated
// as 1, as in applyFont().
constexpr int fontLineHeight(UiFont font, uint8_t scale = 1) {
    return fontHeight(font) * (scale ? scale : 1);
}

// Sets the font and resets the text size in one call, so a screen cannot
// inherit a stale size from the last draw. The caller still owns the transfer
// window, the color and the text datum.
void applyFont(lgfx::LovyanGFX& d, UiFont font, uint8_t scale = 1);

}  // namespace kk::ui

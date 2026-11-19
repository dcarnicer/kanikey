#pragma once
// Small 1bpp UI icons for the header and status rows. The tables live in this
// header so the native tests can validate them without a display; `drawIcon`
// is the only part that needs LovyanGFX. No screen uses an icon yet.
//
// Rows are packed MSB first: pixel (0, y) is bit 7 of byte y*stride, pixel
// (8, y) is bit 7 of byte y*stride + 1. Unused low bits in the last byte are 0.
#include <cstdint>

namespace lgfx {
inline namespace v1 {
class LovyanGFX;
}
}  // namespace lgfx

namespace kk::ui {

constexpr uint8_t kIconWidth = 11;
constexpr uint8_t kIconHeight = 11;

struct Icon {
    const uint8_t* bits = nullptr;
    uint8_t width = 0;
    uint8_t height = 0;
    uint8_t stride = 0;
};

// True when (x, y) is inside the icon and its bit is set. Out-of-range
// coordinates read as clear, so a caller can clip a partial icon safely.
inline bool iconPixel(const Icon& icon, int x, int y) {
    if (!icon.bits || x < 0 || y < 0 || x >= icon.width || y >= icon.height) return false;
    return (icon.bits[y * icon.stride + (x >> 3)] >> (7 - (x & 7))) & 1;
}

// check
inline constexpr uint8_t kIconCheckBits[] = {
    0x00, 0x00, 0x00, 0x60, 0x00, 0xC0, 0x01, 0x80, 0x03, 0x00, 0xC6,
    0x00, 0x6C, 0x00, 0x38, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// cross
inline constexpr uint8_t kIconCrossBits[] = {
    0x00, 0x00, 0x60, 0xC0, 0x31, 0x80, 0x1B, 0x00, 0x0E, 0x00, 0x04,
    0x00, 0x0E, 0x00, 0x1B, 0x00, 0x31, 0x80, 0x60, 0xC0, 0x00, 0x00,
};

// Wi-Fi arcs and dot
inline constexpr uint8_t kIconWifiBits[] = {
    0x0E, 0x00, 0x31, 0x80, 0x60, 0xC0, 0xC0, 0x60, 0x00, 0x00, 0x1F,
    0x00, 0x31, 0x80, 0x60, 0xC0, 0x00, 0x00, 0x0E, 0x00, 0x04, 0x00,
};

// Circular sync arrows
inline constexpr uint8_t kIconSyncBits[] = {
    0x0F, 0x80, 0x30, 0xE0, 0x60, 0x60, 0xC0, 0x00, 0x80, 0x00, 0x80,
    0x00, 0xC0, 0xC0, 0x60, 0xE0, 0x31, 0x80, 0x1F, 0x00, 0x00, 0x00,
};

// microSD card with a cut top-right corner
inline constexpr uint8_t kIconSdBits[] = {
    0x3F, 0x80, 0x20, 0xC0, 0x20, 0x60, 0x20, 0x60, 0x20, 0x60, 0x20,
    0x60, 0x20, 0x60, 0x20, 0x60, 0x20, 0x60, 0x3F, 0x80, 0x00, 0x00,
};

// Key with two teeth
inline constexpr uint8_t kIconKeyBits[] = {
    0x1E, 0x00, 0x21, 0x00, 0x21, 0x00, 0x1E, 0x00, 0x04, 0x00, 0x04,
    0x00, 0x07, 0x00, 0x04, 0x00, 0x07, 0x00, 0x04, 0x00, 0x00, 0x00,
};

// Outbox: upload arrow above an open tray
inline constexpr uint8_t kIconOutboxBits[] = {
    0x04, 0x00, 0x0E, 0x00, 0x15, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00,
    0x00, 0x40, 0x40, 0xC0, 0x60, 0x80, 0x20, 0xFF, 0xE0, 0x00, 0x00,
};

inline constexpr Icon kIconCheck{kIconCheckBits, kIconWidth, kIconHeight, 2};
inline constexpr Icon kIconCross{kIconCrossBits, kIconWidth, kIconHeight, 2};
inline constexpr Icon kIconWifi{kIconWifiBits, kIconWidth, kIconHeight, 2};
inline constexpr Icon kIconSync{kIconSyncBits, kIconWidth, kIconHeight, 2};
inline constexpr Icon kIconSd{kIconSdBits, kIconWidth, kIconHeight, 2};
inline constexpr Icon kIconKey{kIconKeyBits, kIconWidth, kIconHeight, 2};
inline constexpr Icon kIconOutbox{kIconOutboxBits, kIconWidth, kIconHeight, 2};

// Names for tests and a future icon picker; drawIcon() takes an Icon directly.
inline constexpr const Icon* kIcons[] = {
    &kIconCheck, &kIconCross, &kIconWifi, &kIconSync, &kIconSd, &kIconKey, &kIconOutbox,
};
inline constexpr int kIconCount = sizeof(kIcons) / sizeof(kIcons[0]);

// Draws the set pixels in fg and leaves the clear pixels untouched, so an icon
// can sit on any background. `scale` repeats each bit as a scale x scale block.
// The caller owns the transfer window (startWrite()/present()).
void drawIcon(lgfx::LovyanGFX& d, const Icon& icon, int x, int y, uint16_t fg,
              uint8_t scale = 1);

// Draws a bg block the size of the scaled icon, then drawIcon() on top. Use
// this when the icon replaces text or another icon instead of overlapping it.
void drawIconBox(lgfx::LovyanGFX& d, const Icon& icon, int x, int y, uint16_t fg,
                 uint16_t bg, uint8_t scale = 1);

}  // namespace kk::ui

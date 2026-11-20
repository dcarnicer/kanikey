#pragma once
// Shared KaniKey palette. The canvas is RGB332 (8-bit); every stored RGB565
// value is quantized to that palette. See docs/design.md §8.1 for the audit.
#include <cstdint>

namespace kk::ui {

constexpr uint16_t kBg = 0x0000;
constexpr uint16_t kSurface = 0x0108;   // 8-bit: very dark teal-slate
constexpr uint16_t kSurfaceHi = 0x2128; // 8-bit: dark navy
constexpr uint16_t kLine = 0x2208;      // 8-bit: muted teal line
constexpr uint16_t kText = 0xFFFF;
constexpr uint16_t kDim = 0xA577;
constexpr uint16_t kFaint = 0x636F;
constexpr uint16_t kBrand = 0xE308; // #FF6D55, 8-bit coral (design #FF7A59)
constexpr uint16_t kOk = 0x5EF1;
constexpr uint16_t kOkBg = 0x0183;
constexpr uint16_t kAccent = 0x2B1B;
constexpr uint16_t kAccentHi = 0x7D5F;
constexpr uint16_t kReading = 0x2D14;
constexpr uint16_t kErr = 0xFB60; // #FF6D00, 8-bit red-orange (was 0xC945)
constexpr uint16_t kErrBg = 0x3882;
constexpr uint16_t kWarn = 0xFE26;

}  // namespace kk::ui

#pragma once
// PD-02: the PaperDIY 16-level grayscale palette. ui/Theme.h uses these roles
// only when TGC_BOARD_PAPERDIY is set, so the Cardputer palette is unchanged.
//
// The panel keeps the top four bits of each 8-bit gray, so every level here is
// a multiple of 0x10 and lands on exactly one of the 16 panel grays. The
// canvas is grayscale_8bit; a 16-bit colour converts to its luma, and gray565()
// builds a colour whose luma is the level. Meaning is never colour-only: the
// screens' words ("Auth:", "Outbox:", "Level up!") say what a status is, and
// the gray roles only separate it from its neighbours.
#include <cstdint>

namespace kk::ui::epd {

constexpr uint8_t kLevelBlack = 0x00;
constexpr uint8_t kLevelGray1 = 0x20;
constexpr uint8_t kLevelGray2 = 0x30;
constexpr uint8_t kLevelGray3 = 0x50;
constexpr uint8_t kLevelGray4 = 0x80;
constexpr uint8_t kLevelGray5 = 0xB0;
constexpr uint8_t kLevelGray6 = 0xD0;
constexpr uint8_t kLevelWhite = 0xF0;

// Roles, as the Cardputer Theme names them. Body text and the title are white
// on black; secondary text is gray5 and hints are gray4; the highlight is a
// dark gray band with white text.
constexpr uint8_t kRoleBg = kLevelBlack;
constexpr uint8_t kRoleSurface = kLevelGray1;
constexpr uint8_t kRoleSurfaceHi = kLevelGray2;
constexpr uint8_t kRoleLine = kLevelGray3;
constexpr uint8_t kRoleText = kLevelWhite;
constexpr uint8_t kRoleDim = kLevelGray5;
constexpr uint8_t kRoleFaint = kLevelGray4;
constexpr uint8_t kRoleBrand = kLevelWhite;
constexpr uint8_t kRoleOk = kLevelGray6;
constexpr uint8_t kRoleOkBg = kLevelGray1;
constexpr uint8_t kRoleAccent = kLevelGray3;
constexpr uint8_t kRoleAccentHi = kLevelGray4;
constexpr uint8_t kRoleReading = kLevelGray2;
constexpr uint8_t kRoleErr = kLevelWhite;
constexpr uint8_t kRoleErrBg = kLevelGray3;
constexpr uint8_t kRoleWarn = kLevelGray5;

// An RGB565 colour whose luma is `level`: the grayscale canvas stores it as
// that gray.
constexpr uint16_t gray565(uint8_t level) {
    return static_cast<uint16_t>(((level >> 3) << 11) | ((level >> 2) << 5) | (level >> 3));
}

}  // namespace kk::ui::epd

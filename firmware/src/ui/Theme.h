#pragma once
// Shared KaniKey palette. The canvas is RGB332 (8-bit); every stored RGB565
// value is quantized to that palette. See docs/design.md §8.1 for the audit.
#include <cstdint>

#include "wk/Types.h"

#if defined(TGC_BOARD_PAPERDIY)
#include "ui/EpdPalette.h"
#endif

namespace kk::ui {

#if defined(TGC_BOARD_PAPERDIY)
// PD-02: the PaperDIY canvas is a 16-level grayscale (see ui/EpdPalette.h).
// Every role keeps its Cardputer name; the colour a role meant is gone, so the
// screens' words carry the status.
constexpr uint16_t kBg = epd::gray565(epd::kRoleBg);
constexpr uint16_t kSurface = epd::gray565(epd::kRoleSurface);
constexpr uint16_t kSurfaceHi = epd::gray565(epd::kRoleSurfaceHi);
constexpr uint16_t kLine = epd::gray565(epd::kRoleLine);
constexpr uint16_t kText = epd::gray565(epd::kRoleText);
constexpr uint16_t kDim = epd::gray565(epd::kRoleDim);
constexpr uint16_t kFaint = epd::gray565(epd::kRoleFaint);
constexpr uint16_t kBrand = epd::gray565(epd::kRoleBrand);
constexpr uint16_t kOk = epd::gray565(epd::kRoleOk);
constexpr uint16_t kOkBg = epd::gray565(epd::kRoleOkBg);
constexpr uint16_t kAccent = epd::gray565(epd::kRoleAccent);
constexpr uint16_t kAccentHi = epd::gray565(epd::kRoleAccentHi);
constexpr uint16_t kReading = epd::gray565(epd::kRoleReading);
constexpr uint16_t kErr = epd::gray565(epd::kRoleErr);
constexpr uint16_t kErrBg = epd::gray565(epd::kRoleErrBg);
constexpr uint16_t kWarn = epd::gray565(epd::kRoleWarn);
#else
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
#endif

// Subject type colours, WaniKani-style: the prompt panel is filled with the
// type colour and the subject is drawn in kText on it; list rows carry a small
// chip of the same colour. Design values: radical #00AAFF, kanji #FF00AA,
// vocabulary #AA00FF (kana vocabulary takes the vocabulary colour). The 8-bit
// canvas shows #00B6FF, #FF00AA and #B600FF (docs/design.md §8.1).
//
// paperdiy has no hue: every type keeps the panel surface, so its screens look
// as they did, and kTypeColours tells the row code to skip the extra chip.
#if defined(TGC_BOARD_PAPERDIY)
constexpr bool kTypeColours = false;
constexpr uint16_t kRadical = kSurface;
constexpr uint16_t kKanji = kSurface;
constexpr uint16_t kVocab = kSurface;
#else
constexpr bool kTypeColours = true;
constexpr uint16_t kRadical = 0x055F;  // #00AAFF; 8-bit #00B6FF
constexpr uint16_t kKanji = 0xF815;    // #FF00AA; 8-bit #FF00AA
constexpr uint16_t kVocab = 0xA81F;    // #AA00FF; 8-bit #B600FF
#endif

constexpr uint16_t subjectTypeColor(kk::SubjectType type) {
    return type == kk::SubjectType::Radical ? kRadical
           : type == kk::SubjectType::Kanji ? kKanji
                                            : kVocab;
}

}  // namespace kk::ui

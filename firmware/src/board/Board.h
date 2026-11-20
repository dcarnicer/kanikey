// From Tamagotcho (MIT, Copyright (c) 2026 Tamagotcho contributors).
#pragma once
// Board HAL: one implementation per target, selected by build flag
// (TGC_BOARD_STICKS3 / TGC_BOARD_TDISPLAY_S3 / TGC_BOARD_CARDPUTER_ADV /
// TGC_BOARD_PAPERDIY), or TGC_BOARD_M5: the StickS3 and the Cardputer ADV in
// one image, the board chosen at boot (board/BoardM5.cpp). UI code is written
// against the LovyanGFX API only, so it is identical on every board.
#include <cstdint>

#include "ui/KeyInput.h"

#if defined(TGC_BOARD_STICKS3) || defined(TGC_BOARD_CARDPUTER_ADV) || defined(TGC_BOARD_PAPERDIY) || \
    defined(TGC_BOARD_M5)
#include <M5GFX.h>
#elif defined(TGC_BOARD_TDISPLAY_S3)
#include <LovyanGFX.hpp>
#else
#error "Define TGC_BOARD_STICKS3, TGC_BOARD_TDISPLAY_S3, TGC_BOARD_CARDPUTER_ADV, TGC_BOARD_PAPERDIY or TGC_BOARD_M5"
#endif

namespace tgc {
namespace board {

enum class Button : uint8_t {
    Next,    // StickS3: side button (B). T-Display-S3: GPIO14. Cardputer ADV: none
             // (never down; its keyboard has the actions, see nextKeyEvent()).
             // PaperDIY: B (G3).
    Select,  // StickS3: front button (A). T-Display-S3: GPIO0 (BOOT). Cardputer
             // ADV: the G0 button. PaperDIY: A (G4).
};

// --- Capabilities and keyboard ------------------------------------------------
// fixedLandscape(): the panel is used one way only (Cardputer ADV: landscape,
// rotation 0, over the keyboard; PaperDIY: landscape, rotation 1), so the app
// has no Orientation setting.
// hasKeyboard(): the board has a full keyboard (Cardputer ADV); the hint bar
// names keys and nextKeyEvent() delivers them. PaperDIY has no keyboard, but
// its A/B buttons arrive through the same queue as KeySource::Button.
// nextKeyEvent(): the next key event update() read, oldest first; false when
// there is none. Key::None going up means every key is up (events were lost).
#if defined(TGC_BOARD_CARDPUTER_ADV) || defined(TGC_BOARD_M5) || defined(TGC_BOARD_PAPERDIY)
bool fixedLandscape();
bool hasKeyboard();
bool nextKeyEvent(KeyEvent* out);
#ifdef KANIKEY_DEV
// Test hook: append an event to the same ring queue the board fills, so the
// overflow-marker order can be checked without a physical key storm.
void devPushKeyEvent(const KeyEvent& e);
#endif
#else
// No keyboard, every orientation: the StickS3 and the T-Display-S3.
inline bool fixedLandscape() { return false; }
inline bool hasKeyboard() { return false; }
inline bool nextKeyEvent(KeyEvent*) { return false; }
#endif

// PD-03: true when the board's only input is its buttons, so the app shows
// button hints and keeps to the read-only scope (no review, quiz, start or
// text entry). Compile-time on the PaperDIY (true) and the Cardputer ADV
// (false), so the Cardputer image drops the button paths; the M5 image asks
// the board it found at boot.
#if defined(TGC_BOARD_PAPERDIY)
constexpr bool buttonsOnly() { return true; }
#elif defined(TGC_BOARD_CARDPUTER_ADV)
constexpr bool buttonsOnly() { return false; }
#else
inline bool buttonsOnly() { return !hasKeyboard(); }
#endif

// --- microSD slot --------------------------------------------------------------
// The SPI pins of the board's microSD slot (docs/microsd.md §1), -1 for none.
// The card has a bus of its own, SPI2 (an SPIClass on FSPI in mountSd()),
// apart from the display's SPI3. `otherCs` is the select
// line of another chip that can sit on the same lines (an expansion module),
// driven high before the card is used so that chip never answers with it.
// sdPins() is false on a board without a slot (the StickS3, the
// T-Display-S3); in the M5 image the board found at boot answers.
struct SdPins {
    int8_t sck = -1, miso = -1, mosi = -1, cs = -1;
    int8_t otherCs = -1;
};
#if defined(TGC_BOARD_CARDPUTER_ADV) || defined(TGC_BOARD_M5) || defined(TGC_BOARD_PAPERDIY)
bool sdPins(SdPins* out);
#else
inline bool sdPins(SdPins*) { return false; }
#endif

constexpr uint32_t kHoldMs = 600;  // threshold for wasHeld()

// Initialise display (landscape; the Cardputer ADV panel), buttons, the
// keyboard, power. Call once from setup().
void begin();
// Poll inputs; call once per loop() iteration before querying buttons.
void update();

lgfx::LGFX_Device& display();
int width();   // width of the current rotation
int height();  // height of the current rotation

// Display rotation. Landscape is the default (begin() sets it); portrait swaps
// the panel's width and height (e.g. 240x135 -> 135x240). width()/height()
// follow the current rotation. The landscape rotation is board-specific:
// Cardputer ADV 0, PaperDIY 1.
void setOrientation(bool landscape);
bool landscape();
// Rotation 0-3 as LovyanGFX's setRotation(): the top of the picture is at the
// panel's top (0, native portrait), right (1, landscape), bottom (2) or left
// (3, landscape the other way) edge, as seen in rotation 0.
void setRotation(uint8_t r);
uint8_t rotation();

// --- Accelerometer ---------------------------------------------------------
// True when the board has a working accelerometer (StickS3: BMI270).
bool hasAccel();
// Latest acceleration in g in the *panel frame*: +x toward the panel's right
// edge, +y toward its top edge, +z out of the screen, as seen in rotation 0. At
// rest the axis that points up reads +1 g. Non-blocking; false when the board
// has no accelerometer or no sample has arrived yet.
bool readAccel(float* x, float* y, float* z);

// True once on the press edge (this update() cycle only).
bool wasPressed(Button b);
// True while the button is down.
bool isHeld(Button b);
// True once when the button has been down for kHoldMs (long press).
bool wasHeld(Button b);

// Battery charge 0..100, or -1 if the board cannot report it.
int batteryPercent();
// Whether external power / charging is detected; false if unknown.
bool isCharging();

// 0..255. PaperDIY has no backlight; the level is ignored.
void setBrightness(uint8_t level);
// Plays a short test tone through the ES8311. Returns false when the speaker is
// not available in this build (PaperDIY has none).
bool playTone(uint16_t frequency, uint16_t durationMs, uint8_t volume = 128);

// --- Power -----------------------------------------------------------------
// True when this boot is a wake-up from deepSleep() rather than a power-on or
// a reset: the key wake (EXT1) or the timer armed by deepSleep(timerUs) (REM-02).
bool wokeFromSleep();
// True when USB/external power is present; false if unknown. The app then only
// blanks the panel when idle instead of sleeping (keeps the serial port up).
bool externalPower();
// Panel and backlight on/off without sleeping; off keeps the frame buffer.
void setDisplayOn(bool on);
// Blanks the panel and puts the chip in deep sleep. The Select button wakes it
// (on the Cardputer ADV any key too, which is not delivered as a key event;
// on PaperDIY both A and B), and the chip then boots through setup() again.
// timerUs > 0 also arms a timer wake (the dev REM-01 probe; release passes
// nothing, so it never arms one). Never returns.
[[noreturn]] void deepSleep(uint64_t timerUs = 0);
// Blanks the panel, turns Wi-Fi off and enters light sleep. RAM and the current
// screen stay; the Select button and the keyboard's INT line wake it
// (PaperDIY: A and B), and with maxMs > 0 a timer does too (0 = key only). The
// panel is on again when this returns. True when a key/button woke it, false on
// the timer; the key that woke the chip is consumed, exactly like the
// blank-panel wake path.
bool lightSleep(uint32_t maxMs);
// True when the last sleep was ended by its timer. A key wake from deepSleep()
// is EXT1 and is reported by wokeFromSleep(), not here.
bool wokeFromTimer();
// Microseconds since the RTC timer started. It keeps counting through deep
// sleep and resets only on a power cut, so differences measure sleep time.
uint64_t rtcMicros();
// True once when the power button was pressed since the last report, meaning
// "save and sleep now". StickS3: begin() disables the M5PM1's single-click
// reset (which cuts the chip's power and with it the RTC timer) and this polls
// the PM1's press flag; the double-click power-off stays as the hardware
// escape hatch. PaperDIY: the same PM1 flag for the PY_BTN power button; the
// wake-vs-power-cycle behaviour is unverified (PD-01 TODO). False when the PM1
// is missing, unknown or failed to configure (the button then keeps its
// factory behaviour), and always on the T-Display-S3. A press does not wake
// the chip from deepSleep(); only Select (PaperDIY: A/B) does.
bool powerButtonClicked();

// Hardware random seed.
uint32_t randomSeed();

// The board this image runs on, for the log and About ("StickS3", ...).
const char* name();

}  // namespace board
}  // namespace tgc

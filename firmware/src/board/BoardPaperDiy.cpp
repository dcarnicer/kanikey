// From Tamagotcho (MIT, Copyright (c) 2026 Tamagotcho contributors).
// M5Stack PaperDIY implementation via M5Unified / M5GFX.
//
// ESP32-S3-WROOM-1-N16R8 (16 MB flash, 8 MB octal PSRAM), 4.7" ET047TC1
// 540x960 e-paper used in landscape (960x540, rotation 1), no touch, no
// backlight, no keyboard. Two buttons, A on G4 and B on G3, and a microSD slot
// on SPI (SCK G39, MOSI G38, MISO G40, CS G47, card detect G48). The M5PM1
// PMIC (I2C SDA G41 / SCL G42, address 0x6E) owns the EPD power rail (GPIO2),
// the charge-status input (GPIO3), battery voltage and the power button.
// M5GFX autodetects board_M5PaperDIY by probing the PM1 and supplies the
// panel, bus and PM1 setup; M5Unified supplies the buttons, battery and
// charging state.
//
// NEEDS HARDWARE CONFIRMATION (PD-01; no PaperDIY was at hand when this was
// written):
// - the panel init and rotation 1 (M5GFX's offset_rotation is 3),
// - SD mount on the pins above,
// - A/B polarity, hold threshold and the temporary key mapping (PD-03 owns the
//   final mapping),
// - the 140 mAh cell's percentage curve (M5Unified uses its generic LiPo
//   curve),
// - PM1 PY_BTN wake-vs-power-cycle and whether getKeyState() reports it,
// - deep sleep current and PSRAM retention across light sleep,
// - the G3/G4 ext1 wake level (buttons are active low, assumed pulled up),
// - the G45/G46 (EPD OE/BST_EN) and G3 strapping-pin boot behaviour.
#include "board/Board.h"

#include "wk/RingQueue.h"

#if defined(TGC_BOARD_PAPERDIY)
#include <M5Unified.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_random.h>
#include <esp_sleep.h>
#include <soc/esp32s3/rtc.h>

namespace tgc {
namespace board {

namespace {

constexpr gpio_num_t kBtnAPin = GPIO_NUM_4;  // A, active low
constexpr gpio_num_t kBtnBPin = GPIO_NUM_3;  // B, active low
constexpr int kQueueLen = 16;

// The key events update() read, for nextKeyEvent(). PD-03 may replace this
// mapping; PD-01 only needs the buttons reachable while the UI is brought up.
KeyEvent gQueue[kQueueLen];
int gQueueHead = 0, gQueueCount = 0;

// Short press fires on release, hold at kHoldMs; longSent suppresses the
// short action when the hold already fired.
struct PressState {
    bool longSent = false;
};
PressState gA, gB;

void push(const KeyEvent& e) {
    // Same overflow rule as the Cardputer board: the oldest event goes and
    // the "events lost" marker is the newest one.
    const bool lost = gQueueCount == kQueueLen;
    gQueue[kk::ringAppendIndex(&gQueueHead, &gQueueCount, kQueueLen)] =
        lost ? KeyEvent{Key::None, false} : e;
}

// A short press is a tap (A: Next/Down, B: Back/Esc); a hold is the second
// action (A: Confirm, B: Prev/Up). This is the map proposed in the PD spec and
// remains TODO(PD-03): hints, repeat and the final product mapping.
void emitButtonKey(Key k) {
    KeyEvent down;
    down.key = k;
    down.down = true;
    down.source = KeySource::Button;
    push(down);
    KeyEvent up = down;
    up.down = false;
    push(up);
}

void pollButtons() {
    if (M5.BtnA.wasPressed()) gA.longSent = false;
    if (M5.BtnA.wasHold() && !gA.longSent) {
        gA.longSent = true;
        emitButtonKey(Key::Enter);
    }
    if (M5.BtnA.wasReleased()) {
        if (!gA.longSent) emitButtonKey(Key::Down);
        gA.longSent = false;
    }

    if (M5.BtnB.wasPressed()) gB.longSent = false;
    if (M5.BtnB.wasHold() && !gB.longSent) {
        gB.longSent = true;
        emitButtonKey(Key::Up);
    }
    if (M5.BtnB.wasReleased()) {
        if (!gB.longSent) emitButtonKey(Key::Esc);
        gB.longSent = false;
    }
}

// Both buttons up first, or the active-low wake level is there at once. A
// button still down after 3 s leaves the sleep entry anyway; the wake will
// then repeat immediately, which is no worse than not sleeping.
void waitForButtonsUp() {
    const uint32_t t0 = millis();
    while (millis() - t0 < 3000) {
        if (digitalRead(kBtnAPin) == HIGH && digitalRead(kBtnBPin) == HIGH) return;
        delay(10);
    }
}

void armButtonWake() {
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    rtc_gpio_pullup_en(kBtnAPin);
    rtc_gpio_pulldown_dis(kBtnAPin);
    rtc_gpio_pullup_en(kBtnBPin);
    rtc_gpio_pulldown_dis(kBtnBPin);
    const uint64_t mask = (1ull << kBtnAPin) | (1ull << kBtnBPin);
    esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
}

}  // namespace

void begin() {
    // A wake from deepSleep() leaves G3/G4 in the RTC domain; give them back
    // to the digital pads before M5Unified reads the buttons.
    rtc_gpio_deinit(kBtnAPin);
    rtc_gpio_deinit(kBtnBPin);

    auto cfg = M5.config();
    // No touch, IMU, microphone or speaker on this board. Keep the panel
    // uncleared so the first app frame is not preceded by a white flash.
    cfg.clear_display = false;
    cfg.internal_imu = false;
    cfg.internal_spk = false;
    cfg.internal_mic = false;
    M5.begin(cfg);
    if (M5.getBoard() != m5::board_t::board_M5PaperDIY)
        Serial.printf("board: expected a PaperDIY, M5Unified found board %d\n",
                      static_cast<int>(M5.getBoard()));

    // ET047TC1 is 540x960; M5GFX sets offset_rotation = 3, so rotation 1 is
    // the 960x540 landscape orientation.
    M5.Display.setRotation(1);
    M5.Display.setEpdMode(lgfx::epd_quality);  // first frame; PD-02 picks modes
    Serial.printf("display: %dx%d rotation %u, M5 board %d\n", width(), height(), rotation(),
                  static_cast<int>(M5.getBoard()));
    M5.BtnA.setHoldThresh(kHoldMs);
    M5.BtnB.setHoldThresh(kHoldMs);
}

void update() {
    M5.update();
    pollButtons();
}

bool fixedLandscape() { return true; }
bool hasKeyboard() { return false; }

// The microSD slot of the PaperDIY: SCK G39, MISO G40, MOSI G38, CS G47,
// card detect G48 (not represented in SdPins). No other chip on these lines.
bool sdPins(SdPins* out) {
    out->sck = 39;
    out->miso = 40;
    out->mosi = 38;
    out->cs = 47;
    out->otherCs = -1;
    return true;
}

bool nextKeyEvent(KeyEvent* out) {
    if (gQueueCount == 0) return false;
    *out = gQueue[gQueueHead];
    gQueueHead = (gQueueHead + 1) % kQueueLen;
    --gQueueCount;
    return true;
}

#ifdef KANIKEY_DEV
void devPushKeyEvent(const KeyEvent& e) { push(e); }
#endif

lgfx::LGFX_Device& display() { return M5.Display; }
int width() { return M5.Display.width(); }
int height() { return M5.Display.height(); }

// Rotation 1 is landscape on this panel (M5GFX offset_rotation = 3).
void setOrientation(bool landscape) { setRotation(landscape ? 1 : 0); }
bool landscape() { return M5.Display.width() > M5.Display.height(); }
void setRotation(uint8_t r) { M5.Display.setRotation(r & 3); }
uint8_t rotation() { return M5.Display.getRotation() & 3; }

bool hasAccel() { return false; }
bool readAccel(float*, float*, float*) { return false; }

// A, G4 -> Select; B, G3 -> Next. M5Unified already debounces and holds these.
bool wasPressed(Button b) {
    switch (b) {
        case Button::Select: return M5.BtnA.wasPressed();
        case Button::Next: return M5.BtnB.wasPressed();
    }
    return false;
}
bool isHeld(Button b) {
    switch (b) {
        case Button::Select: return M5.BtnA.isPressed();
        case Button::Next: return M5.BtnB.isPressed();
    }
    return false;
}
bool wasHeld(Button b) {
    switch (b) {
        case Button::Select: return M5.BtnA.wasHold();
        case Button::Next: return M5.BtnB.wasHold();
    }
    return false;
}

// M5Unified reads the cell through the M5PM1's battery-voltage register. The
// generic LiPo curve is used; verify it against the 140 mAh cell (PD-01).
int batteryPercent() {
    const int lvl = M5.Power.getBatteryLevel();
    return (lvl < 0 || lvl > 100) ? -1 : lvl;
}

// PM1 GPIO3 is CHG_STAT, active low (M5Unified knows the board).
bool isCharging() { return M5.Power.isCharging() == m5::Power_Class::is_charging; }

// No speaker or codec on this board.
bool playTone(uint16_t, uint16_t, uint8_t) { return false; }

// No backlight: the e-paper has nothing to dim, so the setting is a no-op.
void setBrightness(uint8_t) {}

bool wokeFromSleep() {
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    return cause == ESP_SLEEP_WAKEUP_EXT1 || cause == ESP_SLEEP_WAKEUP_TIMER;
}

// VIN/VINOUT in the PM1's power-source bitmap means external 5 V is present.
// TODO(PD-01): verify on the Complete Kit and decide whether a charger-only
// cable reports VIN the same way as USB data.
bool externalPower() {
    const uint8_t src = static_cast<uint8_t>(M5.Power.M5pm1.getPowerSource());
    const uint8_t external = static_cast<uint8_t>(m5::M5PM1_Class::vin) |
                             static_cast<uint8_t>(m5::M5PM1_Class::vinout);
    return (src & external) != 0;
}

// E-paper keeps its image when the boost is off, so blanking means powerSave.
// PD-02 owns the refresh/present policy around these calls.
void setDisplayOn(bool on) {
    if (on) {
        M5.Display.powerSaveOff();
    } else {
        // Cutting the boost mid-refresh leaves a half-written frame; let it end.
        M5.Display.waitDisplay();
        M5.Display.powerSaveOn();
    }
}

void deepSleep(uint64_t timerUs) {
    M5.Display.waitDisplay();
    M5.Display.powerSaveOn();  // turns off the EPD boost/OE (BST_EN, OE)
    M5.Display.waitDisplay();
    waitForButtonsUp();
    armButtonWake();
    if (timerUs) esp_sleep_enable_timer_wakeup(timerUs);
    Serial.flush();
    esp_deep_sleep_start();
}

// Light sleep keeps the chip and the PM1 in place. TODO(PD-01): PSRAM
// retention across light sleep is unverified; if it is lost, PD-04 should use
// deep sleep (or PM1 shutdown) instead.
bool lightSleep(uint32_t maxMs) {
    M5.Display.waitDisplay();
    M5.Display.powerSaveOn();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    waitForButtonsUp();
    pinMode(kBtnAPin, INPUT_PULLUP);
    pinMode(kBtnBPin, INPUT_PULLUP);
    gpio_wakeup_enable(kBtnAPin, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable(kBtnBPin, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    // Keep VDD_SPI (PSRAM) powered so M5GFX's PSRAM frame buffers survive the
    // slice. TODO(PD-01): confirm on hardware; PD-04 may prefer deep sleep or
    // PM1 shutdown over light sleep.
    esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_ON);
    if (maxMs)
        esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(maxMs) * 1000ull);
    else
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    Serial.flush();
    esp_light_sleep_start();
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    gpio_wakeup_disable(kBtnAPin);
    gpio_wakeup_disable(kBtnBPin);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    // Do not keep VDD_SPI on in a later deep sleep.
    esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_AUTO);
    M5.Display.powerSaveOff();
    return cause != ESP_SLEEP_WAKEUP_TIMER;
}

bool wokeFromTimer() { return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER; }

uint64_t rtcMicros() { return esp_rtc_get_time_us(); }

// The PM1 power button (PY_BTN). M5PM1 reports only "none" (0) or "short
// click" (2) here. TODO(PD-01): verify that PY_BTN is readable this way and
// whether it wakes the ESP32 or only power-cycles through the PM1.
bool powerButtonClicked() { return M5.Power.getKeyState() == 2; }

uint32_t randomSeed() { return esp_random(); }
const char* name() { return "PaperDIY"; }

}  // namespace board
}  // namespace tgc
#endif

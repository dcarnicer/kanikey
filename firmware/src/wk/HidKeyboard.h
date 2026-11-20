#pragma once
// Source-neutral mapping for a Bluetooth HID boot keyboard (BT-02).
//
// A BLE keyboard sends a stateful report: a modifier byte and up to six HID
// usages. This file turns one report into tgc::KeyEvent values and diffs
// successive reports into modifier changes, presses and releases, so App and
// the screens only ever see the same KeyEvent a matrix key produces. No
// Arduino, no BLE, no allocation: native-tested in test_hid.cpp.
//
// The boot layout is the 8-byte report (modifier, reserved, six usages). A
// caller whose raw report starts with a report ID passes `data + 1`; other
// report layouts are not parsed here (see docs/spec/10-bluetooth-paperdiy.md
// §1.5, "Boot vs report protocol").
#include <cstddef>
#include <cstdint>

#include "ui/KeyInput.h"

namespace kk {

// USB HID Usage Tables, Keyboard/Keypad page (0x07).
constexpr uint8_t kHidUsageRollover = 0x01;
constexpr uint8_t kHidUsagePostFail = 0x02;
constexpr uint8_t kHidUsageErrorUndefined = 0x03;

// True for the error/rollover usages a keyboard puts in its key array when it
// cannot report the real keys. They are never mapped to a key.
constexpr bool hidUsageIsError(uint8_t usage) {
    return usage >= kHidUsageRollover && usage <= kHidUsageErrorUndefined;
}

// The unshifted US character of a usage, or 0 for a key that prints nothing
// (Enter, Esc, Tab, Backspace, arrows, Caps Lock, ...). Shift is applied later
// by tgc::keyChar().
constexpr char hidUsageChar(uint8_t usage) {
    if (usage >= 0x04 && usage <= 0x1D)  // a-z
        return static_cast<char>('a' + (usage - 0x04));
    if (usage >= 0x1E && usage <= 0x27)  // 1-9, 0
        return usage == 0x27 ? '0' : static_cast<char>('1' + (usage - 0x1E));
    switch (usage) {
        case 0x2C: return ' ';
        case 0x2D: return '-';
        case 0x2E: return '=';
        case 0x2F: return '[';
        case 0x30: return ']';
        case 0x31: return '\\';
        case 0x33: return ';';
        case 0x34: return '\'';
        case 0x35: return '`';
        case 0x36: return ',';
        case 0x37: return '.';
        case 0x38: return '/';
        default: return 0;
    }
}

// The action key a usage is, or Key::None when the app cannot use it. Letters,
// digits, punctuation and Space are Key::Other plus ch, exactly like the matrix
// letters; arrows and the four editing keys keep their Key members.
constexpr tgc::Key hidUsageToKey(uint8_t usage) {
    switch (usage) {
        case 0x28: return tgc::Key::Enter;
        case 0x29: return tgc::Key::Esc;
        case 0x2A: return tgc::Key::Backspace;
        case 0x2B: return tgc::Key::Tab;
        case 0x4F: return tgc::Key::Right;
        case 0x50: return tgc::Key::Left;
        case 0x51: return tgc::Key::Down;
        case 0x52: return tgc::Key::Up;
        default: return hidUsageChar(usage) ? tgc::Key::Other : tgc::Key::None;
    }
}

constexpr bool hidUsageSupported(uint8_t usage) {
    return hidUsageToKey(usage) != tgc::Key::None;
}

// HID modifier byte -> KeyEvent::mods. The HID byte has left/right bits for
// Ctrl, Shift, Alt and Gui; the app tracks one state per modifier, so either
// side sets the same kMod* bit.
constexpr uint8_t hidModifierBits(uint8_t hidMods) {
    uint8_t mods = 0;
    if (hidMods & (0x01 | 0x10)) mods |= tgc::kModCtrl;
    if (hidMods & (0x02 | 0x20)) mods |= tgc::kModShift;
    if (hidMods & (0x04 | 0x40)) mods |= tgc::kModAlt;
    if (hidMods & (0x08 | 0x80)) mods |= tgc::kModGui;
    return mods;
}

// The source-neutral event for one usage. `mods` is a kMod* mask (already
// passed through hidModifierBits), not the raw HID byte.
constexpr tgc::KeyEvent hidKeyEvent(uint8_t usage, uint8_t mods, bool down) {
    tgc::KeyEvent e;
    e.key = hidUsageToKey(usage);
    e.down = down;
    e.ch = hidUsageChar(usage);
    e.mods = mods;
    e.source = tgc::KeySource::Ble;
    return e;
}

// A boot-protocol keyboard report: the modifier byte and the six usage slots.
struct HidBootReport {
    uint8_t modifiers = 0;
    uint8_t keys[6] = {0, 0, 0, 0, 0, 0};
};

// Parse 8 raw report bytes into `out`. False for a short or null buffer (the
// bytes after the first eight are ignored, so a padded buffer is fine).
constexpr bool hidBootReportParse(const uint8_t* data, size_t len, HidBootReport* out) {
    if (!data || !out || len < 8) return false;
    out->modifiers = data[0];
    for (int i = 0; i < 6; ++i) out->keys[i] = data[2 + i];
    return true;
}

// The most events one report diff can produce: six releases, six presses and
// one modifier change. A 16-slot buffer leaves room without allocation.
constexpr int kHidMaxEvents = 16;

struct HidDiff {
    tgc::KeyEvent events[kHidMaxEvents];
    uint8_t count = 0;
    // A rollover/error report was seen. The held keys have been released, so
    // the caller can surface the loss (BT-03) without any key sticking.
    bool lost = false;
};

// Diffs successive boot reports into a source-neutral event stream.
//
// Order per report: modifier-down first (so gShift is right before the first
// letter), then key releases, then key presses, then modifier-up last (so a
// key-up is seen before Shift comes up). A no-key report releases each held
// key individually. Key::None is never emitted; it stays reserved for a
// matrix overflow and a BLE disconnect.
class HidKeyboard {
public:
    // Forget the held state. The next report starts fresh (used before the
    // first report after connect).
    void reset();

    // Diff a parsed report. A rollover/error report releases everything held
    // and sets HidDiff::lost.
    HidDiff diff(const HidBootReport& report);

    // Release every held key and then the modifiers, for a BLE disconnect.
    // Does not emit Key::None: the link pushes that after the releases when it
    // wants the app to clear every modifier global.
    HidDiff releaseAll();

    bool hasHeldKeys() const { return count_ != 0; }
    bool hasHeldModifiers() const { return mods_ != 0; }

private:
    HidDiff diffKeys(uint8_t nextMods, const uint8_t* nextKeys, uint8_t nextCount);

    uint8_t mods_ = 0;  // kMod* mask
    uint8_t keys_[6] = {0, 0, 0, 0, 0, 0};
    uint8_t count_ = 0;
};

}  // namespace kk

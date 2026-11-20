#include "wk/HidKeyboard.h"

namespace kk {
namespace {

bool contains(const uint8_t* keys, uint8_t count, uint8_t usage) {
    for (uint8_t i = 0; i < count; ++i)
        if (keys[i] == usage) return true;
    return false;
}

void emit(HidDiff& d, const tgc::KeyEvent& e) {
    if (d.count >= kHidMaxEvents) {
        d.lost = true;
        return;
    }
    d.events[d.count++] = e;
}

// A report changed the modifier state but no key produced an event. Key::Other
// is the same carrier a matrix Shift/Ctrl key uses; ch is 0, so no screen turns
// it into text.
tgc::KeyEvent modifierEvent(uint8_t mods, bool down) {
    tgc::KeyEvent e;
    e.key = tgc::Key::Other;
    e.down = down;
    e.mods = mods;
    e.source = tgc::KeySource::Ble;
    return e;
}

}  // namespace

void HidKeyboard::reset() {
    mods_ = 0;
    count_ = 0;
    for (int i = 0; i < 6; ++i) keys_[i] = 0;
}

HidDiff HidKeyboard::diff(const HidBootReport& report) {
    uint8_t nextKeys[6] = {0, 0, 0, 0, 0, 0};
    uint8_t nextCount = 0;
    bool rollover = false;
    for (int i = 0; i < 6; ++i) {
        const uint8_t usage = report.keys[i];
        if (usage == 0) continue;
        if (hidUsageIsError(usage)) {
            rollover = true;
            break;
        }
        // Caps Lock, media keys and anything else the app has no action for
        // are not tracked, so they never produce a Key::None event.
        if (!hidUsageSupported(usage)) continue;
        if (!contains(nextKeys, nextCount, usage)) nextKeys[nextCount++] = usage;
    }
    if (rollover) {
        HidDiff d = releaseAll();
        d.lost = true;
        return d;
    }
    return diffKeys(hidModifierBits(report.modifiers), nextKeys, nextCount);
}

HidDiff HidKeyboard::diffKeys(uint8_t nextMods, const uint8_t* nextKeys, uint8_t nextCount) {
    HidDiff d;
    const bool changedMods = nextMods != mods_;
    const bool addedMods = (nextMods & static_cast<uint8_t>(~mods_)) != 0;

    // Modifier-down before any key-down, and before this report's releases so
    // every other event already carries the new mask.
    if (changedMods && addedMods) emit(d, modifierEvent(nextMods, true));

    // Key-up before modifier-up. A BLE key release carries the report's mods,
    // so keyChar() would still see Shift if the same report also lifts it.
    for (uint8_t i = 0; i < count_; ++i) {
        if (!contains(nextKeys, nextCount, keys_[i]))
            emit(d, hidKeyEvent(keys_[i], nextMods, false));
    }
    for (uint8_t i = 0; i < nextCount; ++i) {
        if (!contains(keys_, count_, nextKeys[i]))
            emit(d, hidKeyEvent(nextKeys[i], nextMods, true));
    }

    // Only removals: let Shift/Control come up after the keys it modified.
    if (changedMods && !addedMods) emit(d, modifierEvent(nextMods, false));

    mods_ = nextMods;
    count_ = nextCount;
    for (int i = 0; i < 6; ++i) keys_[i] = i < nextCount ? nextKeys[i] : 0;
    return d;
}

HidDiff HidKeyboard::releaseAll() {
    HidDiff d;
    for (uint8_t i = 0; i < count_; ++i)
        emit(d, hidKeyEvent(keys_[i], mods_, false));
    if (mods_) emit(d, modifierEvent(0, false));
    mods_ = 0;
    count_ = 0;
    for (int i = 0; i < 6; ++i) keys_[i] = 0;
    return d;
}

}  // namespace kk

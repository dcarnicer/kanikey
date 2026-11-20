// From Tamagotcho (MIT, Copyright (c) 2026 Tamagotcho contributors).
#pragma once
// Keyboard input on boards with a keyboard (the Cardputer ADV; docs/decisions.md,
// 2026-10-21). The app tells a handful of keys apart and turns each into one of
// five actions, which main.cpp maps to the current screen's own (what the front
// tap, the front hold or the side button does there). It also has the text
// entry seam: a source-neutral KeyEvent (matrix, BLE, button) and keyChar(),
// so screens never read the matrix grid themselves. This header has which key a
// TCA8418 event is on the Cardputer ADV's wiring, the key -> action table and
// the arrows' auto-repeat. The BLE HID report mapping is wk/HidKeyboard.h. Pure
// C++, no Arduino (native-tested in test_wk); the TCA8418 itself is read in
// board/BoardCardputerAdv.cpp.
#include <cstdint>

namespace tgc {

// The keys the app tells apart. The arrows are the ; . , / keys (their Fn
// layer prints the arrows; with no text entry anywhere the plain keys are
// arrows too, so Fn does not matter), Esc is the ` key (Esc in its Fn layer),
// Backspace is "del", Tab is "tab". Every other key -- letters, digits, Fn,
// Shift, Ctrl, Opt, Alt, Space -- is Other: it wakes the screen and counts as
// activity, and does nothing else.
enum class Key : uint8_t { None, Up, Down, Left, Right, Enter, Esc, Backspace, Tab, Other };

// What a key asks for, the same on every screen.
enum class KeyAction : uint8_t {
    None,
    Next,     // down, right: what the front tap does (the next option, row, day)
    Prev,     // up, left: the previous option or day, a list's focus up
    Confirm,  // Enter: what the front hold does, at once
    Back,     // Esc, Backspace: the screen's way back or cancel
    Menu,     // Tab: the settings home
};

constexpr KeyAction keyAction(Key k) {
    return k == Key::Down || k == Key::Right  ? KeyAction::Next
           : k == Key::Up || k == Key::Left   ? KeyAction::Prev
           : k == Key::Enter                  ? KeyAction::Confirm
           : k == Key::Esc || k == Key::Backspace ? KeyAction::Back
           : k == Key::Tab                    ? KeyAction::Menu
                                              : KeyAction::None;
}

// The keys that may repeat while held: the arrows and Backspace. App::loop()
// feeds every key event into KeyRepeat and repeats a held Backspace in the
// review and lesson answer fields. Enter and Esc never repeat.
constexpr bool keyRepeats(Key k) {
    return k == Key::Up || k == Key::Down || k == Key::Left || k == Key::Right ||
           k == Key::Backspace;
}

// Where a key event came from. Matrix is the Cardputer ADV's TCA8418; Ble is
// a Bluetooth HID keyboard; Button is a board key or a PaperDIY button. The
// app treats every source the same once the event is in the queue.
enum class KeySource : uint8_t { Matrix, Ble, Button };

// KeyEvent::mods bits: the modifiers held when the event was made. Matrix
// events leave this 0 (their modifier keys arrive as their own events and the
// app tracks gShift/gCtrl/...); BLE events replace it from every HID report.
constexpr uint8_t kModShift = 1u << 0;
constexpr uint8_t kModCtrl = 1u << 1;
constexpr uint8_t kModAlt = 1u << 2;
constexpr uint8_t kModGui = 1u << 3;

// A key going down or up. Key::None going up stands for every key (the board
// lost events, e.g. its FIFO overflowed or a BLE keyboard disconnected):
// nothing is held any more.
//
// `code` is the TCA8418 key number (1-80) and `x`/`y` the printed grid
// position; they stay matrix-only (0 for Ble/Button). `ch` is the unshifted US
// character of a Ble/Button key (0 when none), and `mods` the kMod* bits held
// at the event. Screens ask keyChar() instead of reading x/y or ch themselves.
struct KeyEvent {
    Key key = Key::None;
    bool down = false;
    uint8_t code = 0;
    uint8_t x = 0;
    uint8_t y = 0;
    char ch = 0;
    uint8_t mods = 0;
    KeySource source = KeySource::Matrix;
};

// ---------------------------------------------------------------- Cardputer
// The keyboard as printed: 4 rows of 14 keys, row 0 at the top
// (` 1 2 3 4 5 6 7 8 9 0 - = del), row 1 (tab q ... \), row 2
// (fn shift a ... ' enter), row 3 (ctrl opt alt z ... , . / space).
constexpr int kCardputerCols = 14;
constexpr int kCardputerRows = 4;

constexpr Key cardputerKeyAt(int x, int y) {
    if (x < 0 || x >= kCardputerCols || y < 0 || y >= kCardputerRows) return Key::None;
    if (y == 0) return x == 0 ? Key::Esc : x == 13 ? Key::Backspace : Key::Other;
    if (y == 1) return x == 0 ? Key::Tab : Key::Other;
    if (y == 2) return x == 11 ? Key::Up : x == 13 ? Key::Enter : Key::Other;
    return x == 10 ? Key::Left : x == 11 ? Key::Down : x == 12 ? Key::Right : Key::Other;
}

// A TCA8418 KEY_EVENT_A byte read on the Cardputer ADV: bit 7 set = pressed,
// bits 0-6 the key number, 1 + row * 10 + column of the chip's matrix (7 rows
// x 8 columns there; 0 = the FIFO is empty). The matrix is wired to the grid
// above as M5Stack's M5Cardputer library maps it (MIT;
// src/utility/Keyboard/KeyboardReader/TCA8418.cpp, get_key_event_raw() and
// remap(), github.com/m5stack/M5Cardputer at f1392858): chip row r and column
// c are grid column 2r + (c > 3 ? 1 : 0) and grid row c % 4. False for an
// empty FIFO and for a number outside the matrix (a GPI event).
inline bool cardputerAdvKeyPos(uint8_t raw, int* x, int* y) {
    const int code = raw & 0x7F;
    if (code == 0 || code > 80) return false;
    const int r = (code - 1) / 10;
    const int c = (code - 1) % 10;
    if (r >= 7 || c >= 8) return false;
    *x = 2 * r + (c > 3 ? 1 : 0);
    *y = c % 4;
    return true;
}

// The event for `raw` (see above). False when it is no key of the grid.
inline bool cardputerAdvEvent(uint8_t raw, KeyEvent* out) {
    int x = 0, y = 0;
    if (!cardputerAdvKeyPos(raw, &x, &y)) return false;
    out->key = cardputerKeyAt(x, y);
    out->down = (raw & 0x80) != 0;
    out->code = raw & 0x7F;
    out->x = static_cast<uint8_t>(x);
    out->y = static_cast<uint8_t>(y);
    out->ch = 0;
    out->mods = 0;
    out->source = KeySource::Matrix;
    return true;
}

// The printed character of a grid key on the main or Shift layer. 0 for the
// modifier keys (Fn, Shift, Ctrl, Opt, Alt), Tab, Enter, Backspace and the
// other non-printing keys. The Fn layer is navigation (arrows, Esc, F1-F12),
// not text; the caller handles it before asking for a character.
constexpr char cardputerCharAt(int x, int y, bool shift) {
    if (x < 0 || x >= kCardputerCols || y < 0 || y >= kCardputerRows) return 0;
    constexpr char kNormal[kCardputerRows][kCardputerCols] = {
        {'`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0},
        {0, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\\'},
        {0, 0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', 0},
        {0, 0, 0, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', ' '},
    };
    constexpr char kShift[kCardputerRows][kCardputerCols] = {
        {'~', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0},
        {0, 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '|'},
        {0, 0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', 0},
        {0, 0, 0, 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', ' '},
    };
    return shift ? kShift[y][x] : kNormal[y][x];
}

// The Shift layer of one unshifted US character. Letters, the digit row and
// the punctuation keys the Cardputer and a US HID keyboard print. Unknown
// characters are returned unchanged.
constexpr char shiftedChar(char c) {
    if (c >= 'a' && c <= 'z') return static_cast<char>(c - 'a' + 'A');
    switch (c) {
        case '1': return '!';
        case '2': return '@';
        case '3': return '#';
        case '4': return '$';
        case '5': return '%';
        case '6': return '^';
        case '7': return '&';
        case '8': return '*';
        case '9': return '(';
        case '0': return ')';
        case '`': return '~';
        case '-': return '_';
        case '=': return '+';
        case '[': return '{';
        case ']': return '}';
        case '\\': return '|';
        case ';': return ':';
        case '\'': return '"';
        case ',': return '<';
        case '.': return '>';
        case '/': return '?';
        default: return c;
    }
}

// The character a key event prints, with Shift applied exactly once. Matrix
// events use the printed grid and the caller's live Shift state, as before.
// Ble/Button events carry their own `ch` and modifier mask, so the passed
// shiftHeld is ignored: a HID report is stateful and its Shift bit is the one
// that was true when the key went down. 0 when the event prints nothing.
constexpr char keyChar(const KeyEvent& e, bool shiftHeld) {
    if (e.source == KeySource::Matrix) return cardputerCharAt(e.x, e.y, shiftHeld);
    return (e.mods & kModShift) ? shiftedChar(e.ch) : e.ch;
}

// The modifier keys, by grid position. The app tracks their down/up state;
// the TCA8418 reports them as ordinary keys.
enum class Modifier : uint8_t { None, Shift, Fn, Ctrl, Opt, Alt };

constexpr Modifier cardputerModifierAt(int x, int y) {
    if (x == 1 && y == 2) return Modifier::Shift;
    if (x == 0 && y == 2) return Modifier::Fn;
    if (x == 0 && y == 3) return Modifier::Ctrl;
    if (x == 1 && y == 3) return Modifier::Opt;
    if (x == 2 && y == 3) return Modifier::Alt;
    return Modifier::None;
}

// ---------------------------------------------------------------- repeat
// Auto-repeat: a matrix key that keyRepeats() accepts acts again
// kKeyRepeatDelayMs after its press, then every kKeyRepeatRateMs, until it
// comes up or another key goes down (only the last key pressed repeats, as on
// a PC). App::loop() repeats a held Backspace in the review and lesson answer
// fields; Enter and Esc never repeat, and a BLE keyboard repeats itself.
constexpr uint32_t kKeyRepeatDelayMs = 400;
constexpr uint32_t kKeyRepeatRateMs = 120;

class KeyRepeat {
public:
    // A key went down at `now` (the caller acts on the press itself).
    void down(Key k, uint32_t now) {
        held_ = keyRepeats(k) ? k : Key::None;
        next_ = now + kKeyRepeatDelayMs;
    }
    // A matrix event went down at `now`. BLE keyboards send their own repeat
    // reports, so a non-matrix press never becomes a local repeat and also
    // stops the matrix key that was repeating (the last key pressed owns the
    // repeat, and a BLE key repeats itself).
    void down(const KeyEvent& e, uint32_t now) {
        if (e.source != KeySource::Matrix) {
            held_ = Key::None;
            return;
        }
        down(e.key, now);
    }
    // A key came up; Key::None: every key.
    void up(Key k) {
        if (k == Key::None || k == held_) held_ = Key::None;
    }
    // A matrix key came up. A BLE release must not cancel a matrix repeat.
    void up(const KeyEvent& e) {
        if (e.source == KeySource::Matrix) up(e.key);
    }
    void cancel() { held_ = Key::None; }
    // The held arrow when a repeat is due at `now`, else Key::None. A late call
    // gets one repeat and the next one a full period later, never a burst.
    Key poll(uint32_t now) {
        if (held_ == Key::None || static_cast<int32_t>(now - next_) < 0) return Key::None;
        next_ = now + kKeyRepeatRateMs;
        return held_;
    }
    Key held() const { return held_; }

private:
    Key held_ = Key::None;
    uint32_t next_ = 0;
};

}  // namespace tgc

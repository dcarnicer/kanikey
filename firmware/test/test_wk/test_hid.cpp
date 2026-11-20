// BT-02: HID boot-keyboard mapping and the source-neutral key seam.
#include <unity.h>

#include <initializer_list>

#include "ui/KeyInput.h"
#include "wk/HidKeyboard.h"

using namespace kk;

namespace {

HidBootReport report(uint8_t modifiers, std::initializer_list<uint8_t> keys) {
    HidBootReport r;
    r.modifiers = modifiers;
    int i = 0;
    for (uint8_t key : keys) {
        if (i >= 6) break;
        r.keys[i++] = key;
    }
    return r;
}

void assertNoNone(const HidDiff& d) {
    for (uint8_t i = 0; i < d.count; ++i)
        TEST_ASSERT_TRUE(d.events[i].key != tgc::Key::None);
}

// ---------------------------------------------------------------- usages

static void test_hid_usage_letters_and_digits() {
    TEST_ASSERT_EQUAL(tgc::Key::Other, hidUsageToKey(0x04));
    TEST_ASSERT_EQUAL_CHAR('a', hidUsageChar(0x04));
    TEST_ASSERT_EQUAL(tgc::Key::Other, hidUsageToKey(0x1D));
    TEST_ASSERT_EQUAL_CHAR('z', hidUsageChar(0x1D));

    TEST_ASSERT_EQUAL_CHAR('1', hidUsageChar(0x1E));
    TEST_ASSERT_EQUAL_CHAR('9', hidUsageChar(0x26));
    TEST_ASSERT_EQUAL_CHAR('0', hidUsageChar(0x27));
    TEST_ASSERT_EQUAL(tgc::Key::Other, hidUsageToKey(0x27));
}

static void test_hid_usage_editing_keys() {
    TEST_ASSERT_EQUAL(tgc::Key::Enter, hidUsageToKey(0x28));
    TEST_ASSERT_EQUAL(tgc::Key::Esc, hidUsageToKey(0x29));
    TEST_ASSERT_EQUAL(tgc::Key::Backspace, hidUsageToKey(0x2A));
    TEST_ASSERT_EQUAL(tgc::Key::Tab, hidUsageToKey(0x2B));
    TEST_ASSERT_EQUAL_CHAR(0, hidUsageChar(0x28));
    TEST_ASSERT_EQUAL_CHAR(0, hidUsageChar(0x2A));
}

static void test_hid_usage_arrows() {
    TEST_ASSERT_EQUAL(tgc::Key::Right, hidUsageToKey(0x4F));
    TEST_ASSERT_EQUAL(tgc::Key::Left, hidUsageToKey(0x50));
    TEST_ASSERT_EQUAL(tgc::Key::Down, hidUsageToKey(0x51));
    TEST_ASSERT_EQUAL(tgc::Key::Up, hidUsageToKey(0x52));
    TEST_ASSERT_EQUAL_CHAR(0, hidUsageChar(0x4F));
}

static void test_hid_usage_punctuation_and_space() {
    TEST_ASSERT_EQUAL_CHAR(' ', hidUsageChar(0x2C));
    TEST_ASSERT_EQUAL_CHAR('-', hidUsageChar(0x2D));
    TEST_ASSERT_EQUAL_CHAR('=', hidUsageChar(0x2E));
    TEST_ASSERT_EQUAL_CHAR('[', hidUsageChar(0x2F));
    TEST_ASSERT_EQUAL_CHAR(']', hidUsageChar(0x30));
    TEST_ASSERT_EQUAL_CHAR('\\', hidUsageChar(0x31));
    TEST_ASSERT_EQUAL_CHAR(';', hidUsageChar(0x33));
    TEST_ASSERT_EQUAL_CHAR('\'', hidUsageChar(0x34));
    TEST_ASSERT_EQUAL_CHAR('`', hidUsageChar(0x35));
    TEST_ASSERT_EQUAL_CHAR(',', hidUsageChar(0x36));
    TEST_ASSERT_EQUAL_CHAR('.', hidUsageChar(0x37));
    TEST_ASSERT_EQUAL_CHAR('/', hidUsageChar(0x38));
    TEST_ASSERT_EQUAL(tgc::Key::Other, hidUsageToKey(0x2C));
}

static void test_hid_usage_unsupported_is_none() {
    TEST_ASSERT_FALSE(hidUsageSupported(0x00));  // no key
    TEST_ASSERT_FALSE(hidUsageSupported(0x39));  // Caps Lock
    TEST_ASSERT_EQUAL(tgc::Key::None, hidUsageToKey(0x39));
    TEST_ASSERT_EQUAL_CHAR(0, hidUsageChar(0x39));
    TEST_ASSERT_FALSE(hidUsageSupported(0x70));  // undefined in this page
    TEST_ASSERT_TRUE(hidUsageIsError(0x01));
    TEST_ASSERT_TRUE(hidUsageIsError(0x02));
    TEST_ASSERT_TRUE(hidUsageIsError(0x03));
    TEST_ASSERT_FALSE(hidUsageIsError(0x04));
}

static void test_hid_modifier_bits() {
    TEST_ASSERT_EQUAL_UINT8(tgc::kModCtrl, hidModifierBits(0x01));   // left Ctrl
    TEST_ASSERT_EQUAL_UINT8(tgc::kModCtrl, hidModifierBits(0x10));   // right Ctrl
    TEST_ASSERT_EQUAL_UINT8(tgc::kModShift, hidModifierBits(0x02));  // left Shift
    TEST_ASSERT_EQUAL_UINT8(tgc::kModShift, hidModifierBits(0x20));  // right Shift
    TEST_ASSERT_EQUAL_UINT8(tgc::kModAlt, hidModifierBits(0x04));
    TEST_ASSERT_EQUAL_UINT8(tgc::kModAlt, hidModifierBits(0x40));
    TEST_ASSERT_EQUAL_UINT8(tgc::kModGui, hidModifierBits(0x08));
    TEST_ASSERT_EQUAL_UINT8(tgc::kModGui, hidModifierBits(0x80));
    TEST_ASSERT_EQUAL_UINT8(tgc::kModShift | tgc::kModCtrl | tgc::kModAlt | tgc::kModGui,
                            hidModifierBits(0xFF));
    TEST_ASSERT_EQUAL_UINT8(0, hidModifierBits(0x00));
}

// ---------------------------------------------------------------- keyChar

static void test_key_char_shift_for_ble() {
    tgc::KeyEvent e;
    e.source = tgc::KeySource::Ble;
    e.ch = 'a';
    e.mods = tgc::kModShift;
    TEST_ASSERT_EQUAL_CHAR('A', tgc::keyChar(e, false));
    e.mods = 0;
    // The BLE event's own mask wins: shiftHeld is only the matrix layer state.
    TEST_ASSERT_EQUAL_CHAR('a', tgc::keyChar(e, true));

    e.ch = '1';
    e.mods = tgc::kModShift;
    TEST_ASSERT_EQUAL_CHAR('!', tgc::keyChar(e, false));
    e.ch = ';';
    TEST_ASSERT_EQUAL_CHAR(':', tgc::keyChar(e, false));
    e.ch = ',';
    TEST_ASSERT_EQUAL_CHAR('<', tgc::keyChar(e, false));
    e.ch = ' ';
    TEST_ASSERT_EQUAL_CHAR(' ', tgc::keyChar(e, false));
    e.ch = 0;
    TEST_ASSERT_EQUAL_CHAR(0, tgc::keyChar(e, false));

    // Ctrl/Alt/Gui do not change the printed character; only Shift does.
    e.ch = 'c';
    e.mods = tgc::kModCtrl | tgc::kModAlt | tgc::kModGui;
    TEST_ASSERT_EQUAL_CHAR('c', tgc::keyChar(e, false));

    e.source = tgc::KeySource::Button;
    e.mods = tgc::kModShift;
    e.ch = 'z';
    TEST_ASSERT_EQUAL_CHAR('Z', tgc::keyChar(e, false));
}

static void test_key_char_matrix_identity() {
    for (int y = 0; y < tgc::kCardputerRows; ++y) {
        for (int x = 0; x < tgc::kCardputerCols; ++x) {
            tgc::KeyEvent e;
            e.source = tgc::KeySource::Matrix;
            e.x = static_cast<uint8_t>(x);
            e.y = static_cast<uint8_t>(y);
            // Matrix events must ignore ch/mods and use the grid exactly as
            // before, with Shift applied once.
            e.ch = '?';
            e.mods = 0xFF;
            TEST_ASSERT_EQUAL_CHAR(tgc::cardputerCharAt(x, y, false),
                                   tgc::keyChar(e, false));
            TEST_ASSERT_EQUAL_CHAR(tgc::cardputerCharAt(x, y, true),
                                   tgc::keyChar(e, true));
        }
    }
    tgc::KeyEvent out;
    out.source = tgc::KeySource::Matrix;
    out.x = 200;
    out.y = 200;
    TEST_ASSERT_EQUAL_CHAR(0, tgc::keyChar(out, true));
}

// ---------------------------------------------------------------- reports

static void test_hid_boot_report_parse() {
    const uint8_t raw[8] = {0x02, 0x00, 0x04, 0x05, 0, 0, 0, 0};
    HidBootReport r;
    TEST_ASSERT_TRUE(hidBootReportParse(raw, sizeof raw, &r));
    TEST_ASSERT_EQUAL_UINT8(0x02, r.modifiers);
    TEST_ASSERT_EQUAL_UINT8(0x04, r.keys[0]);
    TEST_ASSERT_EQUAL_UINT8(0x05, r.keys[1]);
    TEST_ASSERT_EQUAL_UINT8(0, r.keys[2]);

    const uint8_t longer[9] = {0x02, 0x00, 0x04, 0x05, 0, 0, 0, 0, 0xFF};
    TEST_ASSERT_TRUE(hidBootReportParse(longer, sizeof longer, &r));
    TEST_ASSERT_EQUAL_UINT8(0x05, r.keys[1]);

    TEST_ASSERT_FALSE(hidBootReportParse(raw, 7, &r));
    TEST_ASSERT_FALSE(hidBootReportParse(nullptr, 8, &r));
    TEST_ASSERT_FALSE(hidBootReportParse(raw, sizeof raw, nullptr));
}

static void test_hid_diff_press_release() {
    HidKeyboard kbd;
    HidDiff d = kbd.diff(report(0, {0x04}));
    TEST_ASSERT_EQUAL_UINT8(1, d.count);
    TEST_ASSERT_FALSE(d.lost);
    TEST_ASSERT_TRUE(d.events[0].down);
    TEST_ASSERT_EQUAL(tgc::Key::Other, d.events[0].key);
    TEST_ASSERT_EQUAL_CHAR('a', d.events[0].ch);
    TEST_ASSERT_EQUAL_UINT8(0, d.events[0].mods);
    TEST_ASSERT_EQUAL(tgc::KeySource::Ble, d.events[0].source);
    TEST_ASSERT_EQUAL_UINT8(0, d.events[0].x);
    TEST_ASSERT_EQUAL_UINT8(0, d.events[0].y);

    // The same report again is not a new press (BLE keyboards repeat by
    // sending a press report only when they mean it).
    d = kbd.diff(report(0, {0x04}));
    TEST_ASSERT_EQUAL_UINT8(0, d.count);

    // Another key while 'a' is still down: only the new press.
    d = kbd.diff(report(0, {0x04, 0x05}));
    TEST_ASSERT_EQUAL_UINT8(1, d.count);
    TEST_ASSERT_TRUE(d.events[0].down);
    TEST_ASSERT_EQUAL_CHAR('b', d.events[0].ch);

    // 'a' removed: one individual release, not Key::None.
    d = kbd.diff(report(0, {0x05}));
    TEST_ASSERT_EQUAL_UINT8(1, d.count);
    TEST_ASSERT_FALSE(d.events[0].down);
    TEST_ASSERT_EQUAL_CHAR('a', d.events[0].ch);
    assertNoNone(d);

    // All up: the last held key is released individually.
    d = kbd.diff(report(0, {}));
    TEST_ASSERT_EQUAL_UINT8(1, d.count);
    TEST_ASSERT_FALSE(d.events[0].down);
    TEST_ASSERT_EQUAL_CHAR('b', d.events[0].ch);
    assertNoNone(d);
    TEST_ASSERT_FALSE(kbd.hasHeldKeys());
    TEST_ASSERT_FALSE(kbd.hasHeldModifiers());

    // An empty report repeated everywhere is silent.
    d = kbd.diff(report(0, {}));
    TEST_ASSERT_EQUAL_UINT8(0, d.count);
    TEST_ASSERT_FALSE(d.lost);
}

static void test_hid_diff_shift_order() {
    HidKeyboard kbd;
    HidDiff d = kbd.diff(report(0x02, {0x04}));
    TEST_ASSERT_EQUAL_UINT8(2, d.count);
    // Modifier-down first, so gShift is right before the letter.
    TEST_ASSERT_TRUE(d.events[0].down);
    TEST_ASSERT_EQUAL(tgc::Key::Other, d.events[0].key);
    TEST_ASSERT_EQUAL_UINT8(tgc::kModShift, d.events[0].mods);
    TEST_ASSERT_TRUE(d.events[1].down);
    TEST_ASSERT_EQUAL_CHAR('a', d.events[1].ch);
    TEST_ASSERT_EQUAL_UINT8(tgc::kModShift, d.events[1].mods);
    TEST_ASSERT_EQUAL_CHAR('A', tgc::keyChar(d.events[1], false));

    // Shift coming down while the key is held is a modifier-only event.
    HidKeyboard later;
    later.diff(report(0, {0x04}));
    d = later.diff(report(0x02, {0x04}));
    TEST_ASSERT_EQUAL_UINT8(1, d.count);
    TEST_ASSERT_TRUE(d.events[0].down);
    TEST_ASSERT_EQUAL(tgc::Key::Other, d.events[0].key);
    TEST_ASSERT_EQUAL_UINT8(tgc::kModShift, d.events[0].mods);

    // Shift up while the key is held is also modifier-only.
    d = later.diff(report(0, {0x04}));
    TEST_ASSERT_EQUAL_UINT8(1, d.count);
    TEST_ASSERT_FALSE(d.events[0].down);
    TEST_ASSERT_EQUAL(tgc::Key::Other, d.events[0].key);
    TEST_ASSERT_EQUAL_UINT8(0, d.events[0].mods);

    // All up with Shift: key-up first, then modifier-up.
    d = later.diff(report(0, {}));
    TEST_ASSERT_EQUAL_UINT8(1, d.count);
    TEST_ASSERT_FALSE(d.events[0].down);

    HidKeyboard both;
    both.diff(report(0x02, {0x04}));
    d = both.diff(report(0, {}));
    TEST_ASSERT_EQUAL_UINT8(2, d.count);
    TEST_ASSERT_EQUAL_CHAR('a', d.events[0].ch);
    TEST_ASSERT_FALSE(d.events[0].down);
    TEST_ASSERT_EQUAL_UINT8(0, d.events[0].mods);  // the same report lifted Shift
    TEST_ASSERT_EQUAL(tgc::Key::Other, d.events[1].key);
    TEST_ASSERT_FALSE(d.events[1].down);
    TEST_ASSERT_EQUAL_UINT8(0, d.events[1].mods);
    assertNoNone(d);
}

static void test_hid_diff_shift_ctrl_alt_gui() {
    HidKeyboard kbd;
    const uint8_t all = 0x0F;  // left Ctrl, Shift, Alt, Gui
    HidDiff d = kbd.diff(report(all, {0x04}));
    TEST_ASSERT_EQUAL_UINT8(2, d.count);
    TEST_ASSERT_EQUAL_UINT8(tgc::kModCtrl | tgc::kModShift | tgc::kModAlt | tgc::kModGui,
                            d.events[0].mods);
    TEST_ASSERT_EQUAL_UINT8(tgc::kModCtrl | tgc::kModShift | tgc::kModAlt | tgc::kModGui,
                            d.events[1].mods);
    TEST_ASSERT_EQUAL_CHAR('A', tgc::keyChar(d.events[1], false));
    TEST_ASSERT_TRUE(kbd.hasHeldKeys());
    TEST_ASSERT_TRUE(kbd.hasHeldModifiers());
}

static void test_hid_diff_rollover_releases_and_flags_loss() {
    HidKeyboard kbd;
    kbd.diff(report(0x02, {0x04}));
    HidDiff d = kbd.diff(report(0x02, {0x01}));
    TEST_ASSERT_TRUE(d.lost);
    TEST_ASSERT_EQUAL_UINT8(2, d.count);  // 'a' up, then Shift up
    TEST_ASSERT_FALSE(d.events[0].down);
    TEST_ASSERT_EQUAL_CHAR('a', d.events[0].ch);
    TEST_ASSERT_FALSE(d.events[1].down);
    TEST_ASSERT_EQUAL(tgc::Key::Other, d.events[1].key);
    TEST_ASSERT_EQUAL_UINT8(0, d.events[1].mods);
    assertNoNone(d);
    TEST_ASSERT_FALSE(kbd.hasHeldKeys());
    TEST_ASSERT_FALSE(kbd.hasHeldModifiers());

    // Being clear again is not a second loss.
    d = kbd.diff(report(0, {}));
    TEST_ASSERT_FALSE(d.lost);
    TEST_ASSERT_EQUAL_UINT8(0, d.count);

    // The next real key presses normally.
    d = kbd.diff(report(0, {0x05}));
    TEST_ASSERT_FALSE(d.lost);
    TEST_ASSERT_EQUAL_UINT8(1, d.count);
    TEST_ASSERT_TRUE(d.events[0].down);
}

static void test_hid_release_all_on_disconnect() {
    HidKeyboard kbd;
    kbd.diff(report(0x02, {0x04}));
    HidDiff d = kbd.releaseAll();
    TEST_ASSERT_EQUAL_UINT8(2, d.count);
    TEST_ASSERT_FALSE(d.events[0].down);
    TEST_ASSERT_EQUAL_CHAR('a', d.events[0].ch);
    TEST_ASSERT_EQUAL_UINT8(tgc::kModShift, d.events[0].mods);
    TEST_ASSERT_FALSE(d.events[1].down);
    TEST_ASSERT_EQUAL(tgc::Key::Other, d.events[1].key);
    TEST_ASSERT_EQUAL_UINT8(0, d.events[1].mods);
    assertNoNone(d);
    TEST_ASSERT_FALSE(kbd.hasHeldKeys());
    TEST_ASSERT_FALSE(kbd.hasHeldModifiers());

    d = kbd.releaseAll();
    TEST_ASSERT_EQUAL_UINT8(0, d.count);
    TEST_ASSERT_FALSE(d.lost);

    // reset() also forgets without emitting.
    kbd.diff(report(0, {0x04}));
    kbd.reset();
    TEST_ASSERT_FALSE(kbd.hasHeldKeys());
    d = kbd.diff(report(0, {0x04}));
    TEST_ASSERT_EQUAL_UINT8(1, d.count);
    TEST_ASSERT_TRUE(d.events[0].down);
}

static void test_hid_diff_ignores_unsupported_and_duplicates() {
    HidKeyboard kbd;
    HidDiff d = kbd.diff(report(0, {0x39}));  // Caps Lock
    TEST_ASSERT_EQUAL_UINT8(0, d.count);
    TEST_ASSERT_FALSE(d.lost);
    TEST_ASSERT_FALSE(kbd.hasHeldKeys());

    d = kbd.diff(report(0, {0x39, 0x04}));
    TEST_ASSERT_EQUAL_UINT8(1, d.count);
    TEST_ASSERT_TRUE(d.events[0].down);
    TEST_ASSERT_EQUAL_CHAR('a', d.events[0].ch);

    // A duplicate in the six slots is one key, not a second press.
    d = kbd.diff(report(0, {0x04, 0x04}));
    TEST_ASSERT_EQUAL_UINT8(0, d.count);
    d = kbd.diff(report(0, {0x04}));
    TEST_ASSERT_EQUAL_UINT8(0, d.count);
    d = kbd.diff(report(0, {}));
    TEST_ASSERT_EQUAL_UINT8(1, d.count);
    TEST_ASSERT_FALSE(d.events[0].down);
}

// ---------------------------------------------------------------- repeat seam

static void test_key_repeat_skips_ble() {
    tgc::KeyRepeat repeat;
    tgc::KeyEvent matrix;
    matrix.source = tgc::KeySource::Matrix;
    matrix.key = tgc::Key::Backspace;
    matrix.down = true;
    repeat.down(matrix, 1000);
    TEST_ASSERT_TRUE(repeat.poll(1400) == tgc::Key::Backspace);

    tgc::KeyEvent ble;
    ble.source = tgc::KeySource::Ble;
    ble.key = tgc::Key::Backspace;
    ble.down = true;
    // A BLE press stops the matrix repeat and never starts a local one: the
    // keyboard sends its own repeat reports.
    repeat.down(ble, 2000);
    TEST_ASSERT_TRUE(repeat.poll(2400) == tgc::Key::None);

    repeat.down(matrix, 3000);
    repeat.down(ble, 3100);
    repeat.up(matrix);
    TEST_ASSERT_TRUE(repeat.poll(4000) == tgc::Key::None);

    // A BLE release must not cancel a matrix repeat that is still held.
    repeat.down(matrix, 5000);
    tgc::KeyEvent bleUp = ble;
    bleUp.down = false;
    repeat.up(bleUp);
    TEST_ASSERT_TRUE(repeat.poll(5400) == tgc::Key::Backspace);
    repeat.up(matrix);
    TEST_ASSERT_TRUE(repeat.poll(6000) == tgc::Key::None);
}

}  // namespace

void run_hid_tests() {
    RUN_TEST(test_hid_usage_letters_and_digits);
    RUN_TEST(test_hid_usage_editing_keys);
    RUN_TEST(test_hid_usage_arrows);
    RUN_TEST(test_hid_usage_punctuation_and_space);
    RUN_TEST(test_hid_usage_unsupported_is_none);
    RUN_TEST(test_hid_modifier_bits);
    RUN_TEST(test_key_char_shift_for_ble);
    RUN_TEST(test_key_char_matrix_identity);
    RUN_TEST(test_hid_boot_report_parse);
    RUN_TEST(test_hid_diff_press_release);
    RUN_TEST(test_hid_diff_shift_order);
    RUN_TEST(test_hid_diff_shift_ctrl_alt_gui);
    RUN_TEST(test_hid_diff_rollover_releases_and_flags_loss);
    RUN_TEST(test_hid_release_all_on_disconnect);
    RUN_TEST(test_hid_diff_ignores_unsupported_and_duplicates);
    RUN_TEST(test_key_repeat_skips_ble);
}

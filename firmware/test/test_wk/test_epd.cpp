// PD-02: the PaperDIY e-paper present/refresh policy and the 16-level gray
// palette. Pure host tests; the timings are proposals (spec §2.3) and are not
// hardware numbers.
#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "ui/EpdPalette.h"
#include "wk/EpdRefreshPolicy.h"

using namespace kk::epd;

namespace {

const Config kCfg;  // settle 150, floor 500, defer 1000, full every 10

// Push a first frame at `t` so the rest of a test starts from a presented panel.
void presentFirstFrame(Presenter* p, uint32_t t) {
    markDirty(p, t);
    const Decision d = decide(*p, t, false, kCfg);
    TEST_ASSERT_TRUE(d.action == Action::Present);
    recordPresent(p, d, t);
}

}  // namespace

// The first frame goes out at once, in quality, as a full refresh.
static void test_epd_first_frame_is_immediate_and_full() {
    Presenter p;
    markDirty(&p, 1000);
    const Decision d = decide(p, 1000, false, kCfg);
    TEST_ASSERT_TRUE(d.action == Action::Present);
    TEST_ASSERT_TRUE(d.full);
    TEST_ASSERT_TRUE(d.mode == Mode::Quality);
}

// A clock or battery tick that changes nothing never marks the canvas dirty,
// so no amount of time presents again.
static void test_epd_no_change_never_presents() {
    Presenter p;
    presentFirstFrame(&p, 0);
    TEST_ASSERT_TRUE(decide(p, 1000, false, kCfg).action == Action::Wait);
    TEST_ASSERT_TRUE(decide(p, 60000, false, kCfg).action == Action::Wait);
    TEST_ASSERT_TRUE(decide(p, 3600000, false, kCfg).action == Action::Wait);
}

// A change waits for 150 ms of stillness and for the 500 ms floor since the
// last push, whichever is later; then it is a partial text refresh.
static void test_epd_settle_then_floor() {
    Presenter p;
    presentFirstFrame(&p, 0);
    markDirty(&p, 600);
    TEST_ASSERT_TRUE(decide(p, 700, false, kCfg).action == Action::Wait);  // not settled
    const Decision d = decide(p, 750, false, kCfg);
    TEST_ASSERT_TRUE(d.action == Action::Present);
    TEST_ASSERT_FALSE(d.full);
    TEST_ASSERT_TRUE(d.mode == Mode::Text);
}

// A change that has settled still waits for the floor after the last push.
static void test_epd_floor_holds_back_a_settled_change() {
    Presenter p;
    presentFirstFrame(&p, 0);
    markDirty(&p, 10);
    TEST_ASSERT_TRUE(decide(p, 160, false, kCfg).action == Action::Wait);  // settled, under floor
    TEST_ASSERT_TRUE(decide(p, 499, false, kCfg).action == Action::Wait);
    TEST_ASSERT_TRUE(decide(p, 500, false, kCfg).action == Action::Present);
}

// Ten keystrokes 100 ms apart are one presented frame, not ten. A steady
// stream still presents once the 1000 ms defer is reached.
static void test_epd_burst_is_coalesced_and_deferred() {
    Presenter p;
    presentFirstFrame(&p, 0);
    for (uint32_t t = 600; t <= 1500; t += 100) {
        markDirty(&p, t);
        TEST_ASSERT_TRUE(decide(p, t, false, kCfg).action == Action::Wait);
    }
    TEST_ASSERT_TRUE(decide(p, 1599, false, kCfg).action == Action::Wait);
    TEST_ASSERT_TRUE(decide(p, 1600, false, kCfg).action == Action::Present);  // 600 + 1000
    recordPresent(&p, decide(p, 1600, false, kCfg), 1600);
    TEST_ASSERT_FALSE(p.pending);
    TEST_ASSERT_TRUE(decide(p, 3000, false, kCfg).action == Action::Wait);  // nothing new
}

// A busy panel is never pushed to, even when every timer says it is due.
static void test_epd_busy_panel_waits() {
    Presenter p;
    presentFirstFrame(&p, 0);
    markDirty(&p, 10000);
    TEST_ASSERT_TRUE(decide(p, 20000, true, kCfg).action == Action::Wait);
    TEST_ASSERT_TRUE(decide(p, 20000, false, kCfg).action == Action::Present);
}

// Every fullEvery partials, the next frame is a full quality refresh; the
// count restarts after it. The ghosting limit is 10 by default.
static void test_epd_full_refresh_every_n_partials() {
    Presenter p;
    presentFirstFrame(&p, 0);
    uint32_t t = 0;
    // Each change is made at `t` and decided 200 ms later, once it has settled.
    for (int i = 0; i < 10; ++i) {
        t += 1000;
        markDirty(&p, t);
        const Decision d = decide(p, t + 200, false, kCfg);
        TEST_ASSERT_TRUE(d.action == Action::Present);
        TEST_ASSERT_FALSE(d.full);
        recordPresent(&p, d, t + 200);
    }
    TEST_ASSERT_EQUAL_UINT32(10, p.partials);
    t += 1000;
    markDirty(&p, t);
    const Decision full = decide(p, t + 200, false, kCfg);
    TEST_ASSERT_TRUE(full.action == Action::Present);
    TEST_ASSERT_TRUE(full.full);
    TEST_ASSERT_TRUE(full.mode == Mode::Quality);
    recordPresent(&p, full, t + 200);
    TEST_ASSERT_EQUAL_UINT32(0, p.partials);
}

// A forced push (a blocking frame, or the first frame) ignores the timers and
// a missing change, but still waits for a busy panel.
static void test_epd_forced_push_skips_timers_not_busy() {
    Presenter p;
    presentFirstFrame(&p, 0);
    const Decision d = decide(p, 10, false, kCfg, /*force=*/true);
    TEST_ASSERT_TRUE(d.action == Action::Present);
    TEST_ASSERT_TRUE(d.mode == Mode::Text);
    TEST_ASSERT_TRUE(decide(p, 10, true, kCfg, /*force=*/true).action == Action::Wait);
}

// millis() wraps; the floor and settle still measure the elapsed time.
static void test_epd_timers_survive_millis_wrap() {
    Presenter p;
    p.presentedOnce = true;
    p.pending = true;
    p.pendingSinceMs = 0xFFFFFF00u;
    p.lastDirtyMs = 0xFFFFFF00u;
    p.lastPresentMs = 0xFFFFFF00u;
    TEST_ASSERT_TRUE(decide(p, 0xFFFFFF00u + 400u, false, kCfg).action == Action::Wait);
    TEST_ASSERT_TRUE(decide(p, 0xFFFFFF00u + 600u, false, kCfg).action == Action::Present);
}

static void test_epd_config_defaults() {
    TEST_ASSERT_EQUAL_UINT32(150, kCfg.settleMs);
    TEST_ASSERT_EQUAL_UINT32(500, kCfg.floorMs);
    TEST_ASSERT_EQUAL_UINT32(1000, kCfg.maxDeferMs);
    TEST_ASSERT_EQUAL_UINT32(10, kCfg.fullEvery);
}

// --- Palette -------------------------------------------------------------

namespace pal = kk::ui::epd;

static int panelSteps(uint8_t a, uint8_t b) {
    return std::abs((a >> 4) - (b >> 4));
}

// The panel keeps the top four bits, so only multiples of 0x10 are distinct
// panel grays. Every role must be one of them.
static void test_epd_palette_levels_are_panel_grays() {
    const uint8_t levels[] = {
        pal::kLevelBlack, pal::kLevelGray1, pal::kLevelGray2, pal::kLevelGray3,
        pal::kLevelGray4, pal::kLevelGray5, pal::kLevelGray6, pal::kLevelWhite,
        pal::kRoleBg,     pal::kRoleSurface, pal::kRoleSurfaceHi, pal::kRoleLine,
        pal::kRoleText,   pal::kRoleDim,     pal::kRoleFaint,     pal::kRoleBrand,
        pal::kRoleOk,     pal::kRoleOkBg,    pal::kRoleAccent,    pal::kRoleAccentHi,
        pal::kRoleReading, pal::kRoleErr,    pal::kRoleErrBg,     pal::kRoleWarn,
    };
    for (uint8_t level : levels) TEST_ASSERT_EQUAL_UINT8(0, level & 0x0F);
}

// The canvas stores a 565 colour as its luma. Expanded back to 8 bits, the
// colour's luma must fall on the same panel gray as the level it was built from.
static void test_epd_gray565_lands_on_its_panel_gray() {
    const uint8_t levels[] = {0x00, 0x20, 0x30, 0x50, 0x80, 0xB0, 0xD0, 0xF0};
    for (uint8_t level : levels) {
        const uint16_t c = pal::gray565(level);
        const uint32_t r5 = (c >> 11) & 31, g6 = (c >> 5) & 63, b5 = c & 31;
        const uint32_t r8 = (r5 << 3) | (r5 >> 2);
        const uint32_t g8 = (g6 << 2) | (g6 >> 4);
        const uint32_t b8 = (b5 << 3) | (b5 >> 2);
        const uint32_t luma = (r8 * 77 + g8 * 150 + b8 * 29) >> 8;
        TEST_ASSERT_EQUAL_UINT32(level >> 4, luma >> 4);
    }
}

// Legibility: text and hints keep a minimum gap from the background, and the
// words the screens use are readable over the highlight and error bands.
static void test_epd_palette_text_contrast() {
    TEST_ASSERT_TRUE(panelSteps(pal::kRoleText, pal::kRoleBg) >= 12);
    TEST_ASSERT_TRUE(panelSteps(pal::kRoleDim, pal::kRoleBg) >= 8);
    TEST_ASSERT_TRUE(panelSteps(pal::kRoleFaint, pal::kRoleBg) >= 8);
    TEST_ASSERT_TRUE(panelSteps(pal::kRoleText, pal::kRoleAccent) >= 10);
    TEST_ASSERT_TRUE(panelSteps(pal::kRoleText, pal::kRoleErrBg) >= 10);
    TEST_ASSERT_TRUE(panelSteps(pal::kRoleOk, pal::kRoleOkBg) >= 8);
}

// The status roles stay apart in gray, so ok, warning and error do not merge
// into one shade. Their words still carry the meaning.
static void test_epd_palette_status_roles_are_distinct() {
    TEST_ASSERT_TRUE(panelSteps(pal::kRoleOk, pal::kRoleWarn) >= 1);
    TEST_ASSERT_TRUE(panelSteps(pal::kRoleErr, pal::kRoleOk) >= 1);
    TEST_ASSERT_TRUE(panelSteps(pal::kRoleErr, pal::kRoleWarn) >= 2);
}

void run_epd_tests() {
    RUN_TEST(test_epd_first_frame_is_immediate_and_full);
    RUN_TEST(test_epd_no_change_never_presents);
    RUN_TEST(test_epd_settle_then_floor);
    RUN_TEST(test_epd_floor_holds_back_a_settled_change);
    RUN_TEST(test_epd_burst_is_coalesced_and_deferred);
    RUN_TEST(test_epd_busy_panel_waits);
    RUN_TEST(test_epd_full_refresh_every_n_partials);
    RUN_TEST(test_epd_forced_push_skips_timers_not_busy);
    RUN_TEST(test_epd_timers_survive_millis_wrap);
    RUN_TEST(test_epd_config_defaults);
    RUN_TEST(test_epd_palette_levels_are_panel_grays);
    RUN_TEST(test_epd_gray565_lands_on_its_panel_gray);
    RUN_TEST(test_epd_palette_text_contrast);
    RUN_TEST(test_epd_palette_status_roles_are_distinct);
}

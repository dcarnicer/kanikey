// ARCH-04: native tests for the pure screen state machine.
#include <unity.h>

#include <cstring>

#include "wk/ScreenModel.h"

using namespace kk;

// ------------------------------------------------------- modal stack

static void test_screen_modal_push_pop() {
    ScreenState state;
    state.screen = Screen::Review;

    TEST_ASSERT_EQUAL(Screen::Review, screenBase(state));
    TEST_ASSERT_EQUAL(Modal::None, screenTop(state));
    TEST_ASSERT_FALSE(screenPop(&state));

    TEST_ASSERT_TRUE(screenPush(&state, Modal::Pause));
    TEST_ASSERT_EQUAL(Modal::Pause, screenTop(state));
    TEST_ASSERT_TRUE(screenPush(&state, Modal::Settings));
    TEST_ASSERT_EQUAL(Modal::Settings, screenTop(state));

    // Pushing the modal that is already on top is refused and changes nothing.
    TEST_ASSERT_FALSE(screenPush(&state, Modal::Settings));
    TEST_ASSERT_EQUAL_UINT8(2, state.depth);

    TEST_ASSERT_TRUE(screenPop(&state));
    TEST_ASSERT_EQUAL(Modal::Pause, screenTop(state));
    TEST_ASSERT_TRUE(screenPop(&state));
    TEST_ASSERT_EQUAL(Modal::None, screenTop(state));
    TEST_ASSERT_FALSE(screenPop(&state));
    TEST_ASSERT_EQUAL_UINT8(0, state.depth);
    TEST_ASSERT_EQUAL(Screen::Review, screenBase(state));

    // A reset returns to Boot and drops every modal.
    TEST_ASSERT_TRUE(screenPush(&state, Modal::Pause));
    screenReset(&state);
    TEST_ASSERT_EQUAL(Screen::Boot, screenBase(state));
    TEST_ASSERT_EQUAL(Modal::None, screenTop(state));
    TEST_ASSERT_EQUAL_UINT8(0, state.depth);
}

static void test_screen_modal_full() {
    ScreenState state;
    state.screen = Screen::Home;
    TEST_ASSERT_TRUE(screenPush(&state, Modal::Pause));
    TEST_ASSERT_TRUE(screenPush(&state, Modal::Settings));
    TEST_ASSERT_TRUE(screenPush(&state, Modal::About));
    TEST_ASSERT_TRUE(screenPush(&state, Modal::SignOut));
    TEST_ASSERT_EQUAL_UINT8(4, state.depth);

    const ScreenState before = state;
    TEST_ASSERT_FALSE(screenPush(&state, Modal::LowBattery));
    TEST_ASSERT_EQUAL_UINT8(4, state.depth);
    TEST_ASSERT_EQUAL(Modal::SignOut, screenTop(state));
    TEST_ASSERT_EQUAL_MEMORY(&before, &state, sizeof state);
}

static void test_screen_pause_settings_about_chain() {
    ScreenState state;
    state.screen = Screen::Review;

    TEST_ASSERT_TRUE(screenPush(&state, Modal::Pause));
    TEST_ASSERT_TRUE(screenPush(&state, Modal::Settings));
    TEST_ASSERT_TRUE(screenPush(&state, Modal::About));
    TEST_ASSERT_EQUAL(Modal::About, screenTop(state));
    TEST_ASSERT_EQUAL_UINT8(3, state.depth);

    TEST_ASSERT_TRUE(screenPop(&state));
    TEST_ASSERT_EQUAL(Modal::Settings, screenTop(state));
    TEST_ASSERT_TRUE(screenPop(&state));
    TEST_ASSERT_EQUAL(Modal::Pause, screenTop(state));
    TEST_ASSERT_TRUE(screenPop(&state));
    TEST_ASSERT_EQUAL(Modal::None, screenTop(state));
    TEST_ASSERT_EQUAL(Screen::Review, screenBase(state));
}

// ------------------------------------------------------- sleep and tables

static void test_screen_sleep_predicate() {
    ScreenState state;

    // The old sleep predicate, screen by screen.
    const Screen sleeping[] = {Screen::Boot, Screen::Home, Screen::Sync,
                               Screen::Setup, Screen::Debug, Screen::RadicalTest};
    for (Screen screen : sleeping) {
        state.screen = screen;
        TEST_ASSERT_TRUE(screenCanSleep(state, false, false, false));
    }

    const Screen awake[] = {Screen::Review,    Screen::ReviewSummary,
                            Screen::LessonEmpty, Screen::LessonList,
                            Screen::LessonCard,  Screen::LessonQuiz,
                            Screen::Outbox,      Screen::Firmware};
    for (Screen screen : awake) {
        state.screen = screen;
        TEST_ASSERT_FALSE(screenCanSleep(state, false, false, false));
    }

    // Every modal keeps the device awake, whatever the base screen.
    state.screen = Screen::Home;
    for (uint8_t raw = static_cast<uint8_t>(Modal::Pause);
         raw <= static_cast<uint8_t>(Modal::FirmwareConfirm); ++raw) {
        ScreenState modalState = state;
        TEST_ASSERT_TRUE(screenPush(&modalState, static_cast<Modal>(raw)));
        TEST_ASSERT_FALSE(screenCanSleep(modalState, false, false, false));
    }

    // Portal, asset server, firmware work and the screens still outside the
    // model all block sleep, matching the guards the callers keep.
    state.screen = Screen::Home;
    TEST_ASSERT_FALSE(screenCanSleep(state, true, false, false));
    TEST_ASSERT_FALSE(screenCanSleep(state, false, true, false));
    TEST_ASSERT_FALSE(screenCanSleep(state, false, false, true));
    TEST_ASSERT_FALSE(screenCanSleep(state, false, false, false, true));
}

static void test_screen_session_active() {
    struct Case {
        Screen screen;
        bool session;
    };
    static const Case cases[] = {
        {Screen::Boot, false},          {Screen::Home, false},
        {Screen::Review, true},         {Screen::ReviewSummary, true},
        {Screen::LessonEmpty, true},    {Screen::LessonList, true},
        {Screen::LessonCard, true},     {Screen::LessonQuiz, true},
        {Screen::Outbox, false},        {Screen::Firmware, false},
        {Screen::Sync, false},          {Screen::Setup, false},
        {Screen::Debug, false},         {Screen::RadicalTest, false},
    };
    for (const Case& c : cases)
        TEST_ASSERT_EQUAL(c.session, screenDef(c.screen).sessionActive);
}

static void test_screen_table_complete() {
    struct Expected {
        Screen screen;
        const char* name;
        bool keepsAwake;
        bool session;
    };
    static const Expected expected[] = {
        {Screen::Boot, "Boot", false, false},
        {Screen::Home, "Home", false, false},
        {Screen::Review, "Review", true, true},
        {Screen::ReviewSummary, "ReviewSummary", true, true},
        {Screen::LessonEmpty, "LessonEmpty", true, true},
        {Screen::LessonList, "LessonList", true, true},
        {Screen::LessonCard, "LessonCard", true, true},
        {Screen::LessonQuiz, "LessonQuiz", true, true},
        {Screen::Outbox, "Outbox", true, false},
        {Screen::Firmware, "Firmware", true, false},
        {Screen::Sync, "Sync", false, false},
        {Screen::Setup, "Setup", false, false},
        {Screen::Debug, "Debug", false, false},
        {Screen::RadicalTest, "RadicalTest", false, false},
    };

    for (const Expected& e : expected) {
        const ScreenDef& def = screenDef(e.screen);
        TEST_ASSERT_EQUAL(e.screen, def.screen);
        TEST_ASSERT_NOT_NULL(def.name);
        TEST_ASSERT_TRUE(def.name[0] != '\0');
        TEST_ASSERT_EQUAL_STRING(e.name, def.name);
        TEST_ASSERT_EQUAL(e.keepsAwake, def.keepsAwake);
        TEST_ASSERT_EQUAL(e.session, def.sessionActive);
    }

    // Every name is unique, so a log line or a help title cannot be ambiguous.
    for (size_t i = 0; i < sizeof expected / sizeof expected[0]; ++i)
        for (size_t j = i + 1; j < sizeof expected / sizeof expected[0]; ++j)
            TEST_ASSERT_TRUE(std::strcmp(screenDef(expected[i].screen).name,
                                          screenDef(expected[j].screen).name) != 0);
}

void run_screen_tests() {
    RUN_TEST(test_screen_modal_push_pop);
    RUN_TEST(test_screen_modal_full);
    RUN_TEST(test_screen_pause_settings_about_chain);
    RUN_TEST(test_screen_sleep_predicate);
    RUN_TEST(test_screen_session_active);
    RUN_TEST(test_screen_table_complete);
}

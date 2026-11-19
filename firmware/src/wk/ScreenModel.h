#pragma once
// ARCH-04: the screen state machine. A single base screen plus a small modal
// stack replaces the per-screen booleans that used to decide drawing, key
// dispatch and sleep. Pure C++: no Arduino headers, compiled by the native
// test environment too.
#include <cstdint>

namespace kk {

enum class Screen : uint8_t {
    Boot,
    Home,
    Review,
    ReviewSummary,
    LessonEmpty,
    LessonList,
    LessonCard,
    LessonQuiz,
    Outbox,
    Firmware,
    Sync,
    Setup,
    Debug,
    RadicalTest,
};

enum class Modal : uint8_t {
    None,
    Pause,
    Settings,
    About,
    SignOut,
    OutboxConfirm,
    LowBattery,
    FirmwareConfirm,
};

// keepsAwake: today's sleep predicate, side by side with the screen.
// sessionActive: true for a review or lesson session, including the summary
// and the six lesson states; false for utility screens such as Outbox.
struct ScreenDef {
    Screen screen;
    const char* name;
    bool keepsAwake;
    bool sessionActive;
};

const ScreenDef& screenDef(Screen screen);

struct ScreenState {
    Screen screen = Screen::Boot;
    Modal stack[4] = {Modal::None, Modal::None, Modal::None, Modal::None};
    uint8_t depth = 0;
};

// LIFO modal stack. Push refuses when the stack is full or the modal is
// already on top; a refused push leaves the state untouched. Pop on an empty
// stack returns false.
bool screenPush(ScreenState* state, Modal modal);
bool screenPop(ScreenState* state);

Modal screenTop(const ScreenState& state);
Screen screenBase(const ScreenState& state);

// True only when every visible state allows deep sleep. Keeping this in the
// pure model means App.cpp and the tests share the same predicate.
//
// portalActive, assetServerActive and firmwareBusy are the blocking operations
// that are not screens. auxiliaryScreen covers the screens that still live
// outside the enumerated model in this task (stats, study, critical list,
// Wi-Fi setup and the notice frame); without it they would silently start
// sleeping, which the old predicate did not allow.
bool screenCanSleep(const ScreenState& state, bool portalActive, bool assetServerActive,
                    bool firmwareBusy, bool auxiliaryScreen = false);

void screenReset(ScreenState* state);

}  // namespace kk

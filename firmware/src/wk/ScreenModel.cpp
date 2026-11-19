#include "wk/ScreenModel.h"

#include <cstddef>

namespace kk {
namespace {

// ARCH-04: the table matches today's sleep predicate exactly. Review and the
// lesson flow are sessions; Outbox and Firmware keep the panel awake but are
// not sessions. Every modal blocks sleep through screenCanSleep().
constexpr ScreenDef kScreenDefs[] = {
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

constexpr size_t kScreenDefCount = sizeof kScreenDefs / sizeof kScreenDefs[0];
static_assert(kScreenDefCount == static_cast<size_t>(Screen::RadicalTest) + 1,
              "screen table must cover every Screen value");

}  // namespace

const ScreenDef& screenDef(Screen screen) {
    const size_t index = static_cast<size_t>(screen);
    if (index >= kScreenDefCount) return kScreenDefs[0];
    return kScreenDefs[index];
}

bool screenPush(ScreenState* state, Modal modal) {
    if (!state || modal == Modal::None) return false;
    if (state->depth >= sizeof state->stack / sizeof state->stack[0]) return false;
    if (state->depth && state->stack[state->depth - 1] == modal) return false;
    state->stack[state->depth++] = modal;
    return true;
}

bool screenPop(ScreenState* state) {
    if (!state || state->depth == 0) return false;
    --state->depth;
    state->stack[state->depth] = Modal::None;
    return true;
}

Modal screenTop(const ScreenState& state) {
    if (state.depth == 0) return Modal::None;
    return state.stack[state.depth - 1];
}

Screen screenBase(const ScreenState& state) { return state.screen; }

bool screenCanSleep(const ScreenState& state, bool portalActive, bool assetServerActive,
                    bool firmwareBusy, bool auxiliaryScreen) {
    if (portalActive || assetServerActive || firmwareBusy || auxiliaryScreen) return false;
    if (state.depth > 0) return false;
    return !screenDef(screenBase(state)).keepsAwake;
}

void screenReset(ScreenState* state) {
    if (!state) return;
    state->screen = Screen::Boot;
    for (Modal& modal : state->stack) modal = Modal::None;
    state->depth = 0;
}

}  // namespace kk

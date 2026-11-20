#pragma once
// Shared UI primitives: canvas ownership, the frame target, key names, the
// status log and the blocking loading/notice frames. Not a controller.
#include <cstddef>
#include <cstdint>

#include "app/AppState.h"
#include "app/ButtonHint.h"

namespace kk::app {

lgfx::LovyanGFX& target();
// Cardputer: present() pushes the canvas at once. PaperDIY (PD-02): present()
// marks the canvas dirty and presentTick() pushes it when the e-paper policy
// says so; presentNow() pushes at once, for blocking frames and the first frame.
void present();
void presentNow();
// PD-04: the sleeping frame before a deep sleep: a full-quality refresh now,
// whatever the PD-02 policy would pick, because it is the frame that stays up.
void presentFullNow();
void presentTick(uint32_t now);
bool ensureCanvas();
void releaseCanvas();
void requestBefore();
void requestAfter();
// Retries a canvas that could not be restored after a request (heap too
// fragmented then); call once per loop.
void canvasRecoveryTick(uint32_t now);
// AUD-03 heap lending hooks for the audio player (see AppUi.cpp).
bool lendCanvasForAudio();
void canvasBackFromAudio();

const char* keyName(tgc::Key k);
void pushLog(const char* text);
void updateModifier(const tgc::KeyEvent& e);
bool shift();

void drawLoading(const char* title, const char* subtitle);
void drawNotice();
void noticeKey(const tgc::KeyEvent& e);
// PD-03: refuses a keyboard-only action on a two-button board. `line` names it
// ("Reviews need a keyboard"); any button closes the notice and nothing else
// changes.
void needsKeyboardNotice(const char* line);

void localHm(uint32_t epoch, char* out, size_t n);
void localDate(uint32_t epoch, char* out, size_t n);

}  // namespace kk::app

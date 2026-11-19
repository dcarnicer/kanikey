#pragma once
// Shared UI primitives: canvas ownership, the frame target, key names, the
// status log and the blocking loading/notice frames. Not a controller.
#include <cstddef>
#include <cstdint>

#include "app/AppState.h"

namespace kk::app {

lgfx::LovyanGFX& target();
void present();
bool ensureCanvas();
void releaseCanvas();
void requestBefore();
void requestAfter();

const char* keyName(tgc::Key k);
void pushLog(const char* text);
void updateModifier(uint8_t x, uint8_t y, bool down);
bool shift();

void drawLoading(const char* title, const char* subtitle);
void drawNotice();
void noticeKey(const tgc::KeyEvent& e);

void localHm(uint32_t epoch, char* out, size_t n);
void localDate(uint32_t epoch, char* out, size_t n);

}  // namespace kk::app

#include "platform/SpeakerPower.h"

#include <Arduino.h>
#include <M5Unified.h>

namespace kk::speaker {

namespace {

// Last millis() at which a sound was playing or started. The idle delay counts
// from here.
uint32_t gLastBusy = 0;

}  // namespace

void wake() {
    gLastBusy = millis();
    // isRunning() is the playback task's state: M5Speaker's isEnabled() only
    // reports that a data pin is set.
    if (M5.Speaker.isRunning()) return;
    if (!M5.Speaker.begin()) {
        Serial.println("[audio] speaker failed to start");
        return;
    }
    Serial.println("[audio] speaker on");
}

void tick(uint32_t now, bool soundPending) {
    if (soundPending || M5.Speaker.isPlaying()) {
        gLastBusy = now;
        return;
    }
    if (!M5.Speaker.isRunning()) return;
    // Unsigned subtraction: a millis() wrap does not end the speaker early.
    if (now - gLastBusy < kIdleMs) return;
    M5.Speaker.end();
    Serial.println("[audio] speaker off");
}

}  // namespace kk::speaker

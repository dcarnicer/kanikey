#pragma once
// Dev-only latency trace for the Enter -> feedback path (REV-10 follow-up).
// While a trace is armed, each step prints one `[perf]` line with the
// milliseconds since the Enter key that opened the feedback frame, and the
// step's own delta. Release builds compile every call to nothing.
#ifdef KANIKEY_DEV
#include <Arduino.h>

namespace kk::app {

inline bool gFeedbackTraceOn = false;
inline uint32_t gFeedbackTraceT0 = 0;
inline uint32_t gFeedbackTraceLast = 0;

// Arms the trace at the answer check of an Enter that opens feedback.
inline void feedbackTraceBegin() {
    gFeedbackTraceOn = true;
    gFeedbackTraceT0 = gFeedbackTraceLast = millis();
}

inline void feedbackTraceStep(const char* step) {
    if (!gFeedbackTraceOn) return;
    const uint32_t now = millis();
    Serial.printf("[perf] %-24s %5u ms since Enter (+%u ms)\n", step,
                  static_cast<unsigned>(now - gFeedbackTraceT0),
                  static_cast<unsigned>(now - gFeedbackTraceLast));
    gFeedbackTraceLast = now;
}

// Called once the deferred writes of that frame have run.
inline void feedbackTraceEnd() {
    if (!gFeedbackTraceOn) return;
    feedbackTraceStep("writes done");
    gFeedbackTraceOn = false;
}

}  // namespace kk::app

#else

namespace kk::app {
inline void feedbackTraceBegin() {}
inline void feedbackTraceStep(const char*) {}
inline void feedbackTraceEnd() {}
}  // namespace kk::app

#endif

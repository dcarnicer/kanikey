// KaniKey — Arduino entry points.
//
// The application state, services, screen routing and input handling live in
// app/App.cpp; board and platform code live in board/ and platform/.

#include <Arduino.h>

#include "app/App.h"

// The loop task stack. The lesson fetch (streaming JSON + study-material DOM +
// TLS) overflowed the framework's 8 KB stack in the dev image. Measured on the
// device under Arduino 3 (2026-11-20): high-water 19,356 B free of 24,576 after
// boot and sync, about 5.2 KB used (earlier estimate for the peak: 8.8 KB).
// MEM-12 moved the 4 KB OTA copy blocks to the heap and removed the 2.5 KB
// std::mt19937 local from the review batch. 16 KB keeps at least 7 KB of margin
// over the earlier peak estimate; `h` after a lesson fetch and an OTA check is
// the proof (docs/spec/12-memory.md §5).
SET_LOOP_TASK_STACK_SIZE(16384);

void setup() { kk::app::setup(); }

void loop() { kk::app::loop(); }

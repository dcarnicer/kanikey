// KaniKey — Arduino entry points.
//
// The application state, services, screen routing and input handling live in
// app/App.cpp; board and platform code live in board/ and platform/.

#include <Arduino.h>

#include "app/App.h"

// The lesson fetch (streaming JSON + study-material DOM + TLS) overflowed the
// framework's 8 KB loop stack in the dev image; the OTA install adds TLS and
// the Update sink on top. 24 KB leaves about 17 KB of high-water headroom and
// is still small next to the internal heap; the lazy review batch and the
// smaller USB RX ring pay for it in the release build.
SET_LOOP_TASK_STACK_SIZE(24576);

void setup() { kk::app::setup(); }

void loop() { kk::app::loop(); }

// KaniKey — Arduino entry points.
//
// The application state, services, screen routing and input handling live in
// app/App.cpp; board and platform code live in board/ and platform/.

#include <Arduino.h>

#include "app/App.h"

void setup() { kk::app::setup(); }

void loop() { kk::app::loop(); }

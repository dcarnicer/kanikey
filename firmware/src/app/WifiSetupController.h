#pragma once
// SETUP-09: choose a 2.4 GHz network and type its password on the Cardputer.
// The phone portal stays the fallback for the token and for any network this
// flow refuses.
#include "app/AppState.h"
#include "ui/KeyInput.h"

namespace kk::app {

// Opens the scan list. The scan is blocking and draws a loading frame, like the
// setup portal's phone scan.
void openWifiSetup(WifiSetupOrigin origin);
void wifiSetupKey(const tgc::KeyEvent& e);
void drawWifiSetup();

}  // namespace kk::app

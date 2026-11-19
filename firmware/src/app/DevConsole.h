#pragma once
// The serial console and the KANIKEY_DEV asset server.
namespace kk::app {

void handleSerial();
void serviceAssetServer();

#ifdef KANIKEY_DEV
void showDebug();
void showRadicalTest();
#endif

}  // namespace kk::app

#pragma once
// Development Wi-Fi helper for the first hardware milestone.
//
// KaniKey will have its own setup portal (docs/design.md §5). Until then this
// reads the credentials Tangocho already saved in NVS namespace "net" and
// joins that network on demand. The password never reaches a log.
#include <Arduino.h>

namespace kk {

class DevWifi {
public:
    static bool hasSaved();
    static String savedSsid();  // "" when none is saved
    // Development helper: store the network in NVS namespace "net" (the same
    // namespace Tangocho uses). The password is never logged.
    static bool saveCredentials(const String& ssid, const String& pass);
    // Joins the saved network, blocking until connected or `timeoutMs`.
    static bool connect(uint32_t timeoutMs = 20000);
    static void disconnect();
    static String ip();
    static int rssi();
    static bool connected();
};

}  // namespace kk

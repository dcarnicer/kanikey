#pragma once
// Wi-Fi credentials and station helpers: the saved network in NVS namespace
// "net", blocking scans for the on-device setup list, and a non-saving join
// used before new credentials are committed. The password never reaches a log.
#include <Arduino.h>

#include "wk/WifiSetup.h"

namespace kk {

class DevWifi {
public:
    static bool hasSaved();
    static String savedSsid();  // "" when none is saved
    // Development helper: store the network in NVS namespace "net" (the same
    // namespace Tangocho uses). The password is never logged.
    static bool saveCredentials(const String& ssid, const String& pass);
    // Joins the saved network, blocking until connected or `timeoutMs`.
    static bool connect(uint32_t timeoutMs = 30000);
    // Joins `ssid` with `pass` without writing NVS. Fills the final link status
    // and whether the attempt timed out; the password is never logged. The
    // caller saves the credentials only after a successful join.
    static bool connectTo(const String& ssid, const String& pass, uint32_t timeoutMs,
                          kk::WifiLinkStatus* lastStatus = nullptr,
                          bool* timedOut = nullptr);
    // Blocking scan of the visible 2.4 GHz APs. The caller can still pass the
    // result through wk/WifiSetup's wifiBuildScanList() for the SSID dedupe
    // and signal sort. Returns how many entries were written.
    static size_t scan(kk::WifiNetwork* out, size_t max);
    static void disconnect();
    static String ip();
    static int rssi();
    static bool connected();
};

}  // namespace kk

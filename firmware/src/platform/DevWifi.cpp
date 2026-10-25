#include "platform/DevWifi.h"

#include <Preferences.h>
#include <WiFi.h>

#include <cstring>

namespace kk {

namespace {
constexpr const char* kNs = "net";
constexpr const char* kKeySsid = "ssid";
constexpr const char* kKeyPass = "pass";

bool load(String& ssid, String& pass) {
    Preferences p;
    if (!p.begin(kNs, false)) return false;
    if (!p.isKey(kKeySsid)) {
        p.end();
        return false;
    }
    ssid = p.getString(kKeySsid, "");
    pass = p.isKey(kKeyPass) ? p.getString(kKeyPass, "") : String();
    p.end();
    return !ssid.isEmpty();
}

void wipe(String& s) {
    if (s.length()) memset(const_cast<char*>(s.c_str()), 0, s.length());
}
}  // namespace

bool DevWifi::hasSaved() {
    String ssid, pass;
    const bool ok = load(ssid, pass);
    wipe(pass);
    return ok;
}

String DevWifi::savedSsid() {
    String ssid, pass;
    load(ssid, pass);
    wipe(pass);
    return ssid;
}

bool DevWifi::connect(uint32_t timeoutMs) {
    String ssid, pass;
    if (!load(ssid, pass)) {
        Serial.println("[wifi] no saved credentials in NVS namespace net");
        return false;
    }

    Serial.printf("[wifi] joining \"%s\"\n", ssid.c_str());
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);
    wipe(pass);

    const uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
        delay(100);
    }
    const uint32_t ms = millis() - t0;
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("[wifi] failed after %u ms, status %d\n", static_cast<unsigned>(ms),
                      static_cast<int>(WiFi.status()));
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return false;
    }
    Serial.printf("[wifi] connected in %u ms, ip %s, rssi %d dBm\n",
                  static_cast<unsigned>(ms), WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
}

void DevWifi::disconnect() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

String DevWifi::ip() { return WiFi.localIP().toString(); }
int DevWifi::rssi() { return WiFi.RSSI(); }
bool DevWifi::connected() { return WiFi.status() == WL_CONNECTED; }

}  // namespace kk

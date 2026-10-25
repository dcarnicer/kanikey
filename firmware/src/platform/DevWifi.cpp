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

bool DevWifi::saveCredentials(const String& ssid, const String& pass) {
    if (ssid.isEmpty() || ssid.length() > 32) return false;
    if (!pass.isEmpty()) {
        if (pass.length() < 8 || pass.length() > 63) return false;
        for (size_t i = 0; i < pass.length(); ++i) {
            const char c = pass[i];
            if (c < 0x20 || c > 0x7E) return false;
        }
    }
    Preferences p;
    if (!p.begin(kNs, false)) return false;
    const bool ok = p.putString(kKeySsid, ssid) == ssid.length() &&
                    p.putString(kKeyPass, pass) == pass.length();
    p.end();
    if (ok) Serial.printf("[wifi] saved \"%s\"\n", ssid.c_str());
    return ok;
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

    // The first join after a cold boot sometimes sits in WL_DISCONNECTED until
    // the driver's scan settles. Two attempts with a clean radio in between
    // match what a manual retry does; split the caller's budget between them.
    const uint32_t perAttempt = timeoutMs / 2;
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (attempt > 0) {
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            delay(500);
            WiFi.mode(WIFI_STA);
        }
        WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);
        const uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < perAttempt) delay(100);
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[wifi] connected in %u ms (attempt %d), ip %s, rssi %d dBm\n",
                          static_cast<unsigned>(millis() - t0), attempt + 1,
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
            wipe(pass);
            return true;
        }
        Serial.printf("[wifi] attempt %d failed after %u ms, status %d\n", attempt + 1,
                      static_cast<unsigned>(millis() - t0), static_cast<int>(WiFi.status()));
    }
    wipe(pass);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
}

void DevWifi::disconnect() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

String DevWifi::ip() { return WiFi.localIP().toString(); }
int DevWifi::rssi() { return WiFi.RSSI(); }
bool DevWifi::connected() { return WiFi.status() == WL_CONNECTED; }

}  // namespace kk

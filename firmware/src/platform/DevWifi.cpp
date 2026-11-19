#include "platform/DevWifi.h"

#include <Preferences.h>
#include <WiFi.h>

#include <cstdio>
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

kk::WifiLinkStatus linkStatus(wl_status_t status) {
    switch (status) {
        case WL_IDLE_STATUS: return kk::WifiLinkStatus::Idle;
        case WL_NO_SSID_AVAIL: return kk::WifiLinkStatus::NoSsid;
        case WL_SCAN_COMPLETED: return kk::WifiLinkStatus::ScanCompleted;
        case WL_CONNECTED: return kk::WifiLinkStatus::Connected;
        case WL_CONNECT_FAILED: return kk::WifiLinkStatus::ConnectFailed;
        case WL_CONNECTION_LOST: return kk::WifiLinkStatus::ConnectionLost;
        case WL_DISCONNECTED: return kk::WifiLinkStatus::Disconnected;
        default: return kk::WifiLinkStatus::Other;
    }
}

kk::WifiSecurity securityFromAuth(wifi_auth_mode_t auth) {
    switch (auth) {
        case WIFI_AUTH_OPEN: return kk::WifiSecurity::Open;
        case WIFI_AUTH_WEP: return kk::WifiSecurity::Wep;
        case WIFI_AUTH_WPA_PSK: return kk::WifiSecurity::Wpa;
        case WIFI_AUTH_WPA2_PSK: return kk::WifiSecurity::Wpa2;
        case WIFI_AUTH_WPA_WPA2_PSK: return kk::WifiSecurity::Wpa2;
        case WIFI_AUTH_WPA2_ENTERPRISE: return kk::WifiSecurity::Enterprise;
        case WIFI_AUTH_WPA3_PSK: return kk::WifiSecurity::Wpa3;
        case WIFI_AUTH_WPA2_WPA3_PSK: return kk::WifiSecurity::Wpa2Wpa3;
        case WIFI_AUTH_WAPI_PSK: return kk::WifiSecurity::Wapi;
        case WIFI_AUTH_WPA3_ENT_192: return kk::WifiSecurity::Enterprise;
        default: return kk::WifiSecurity::Unknown;
    }
}

bool joinNetwork(const String& ssid, const String& pass, uint32_t timeoutMs,
                 kk::WifiLinkStatus* lastStatus, bool* timedOut) {
    Serial.printf("[wifi] joining \"%s\"\n", ssid.c_str());
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_STA);

    // The first join after a cold boot sometimes sits in WL_DISCONNECTED until
    // the driver's scan settles. Two attempts with a clean radio in between
    // match what a manual retry does; split the caller's budget between them.
    const uint32_t perAttempt = timeoutMs / 2;
    wl_status_t last = WL_IDLE_STATUS;
    bool elapsed = false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (attempt > 0) {
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            delay(500);
            WiFi.mode(WIFI_STA);
        }
        WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);
        const uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < perAttempt) {
            delay(100);
            // NO_SSID_AVAIL does not improve with the remaining timeout: the
            // AP is not in range (or the name is wrong). A short grace lets
            // the driver finish its scan.
            if (WiFi.status() == WL_NO_SSID_AVAIL && millis() - t0 >= 2000) break;
        }
        last = WiFi.status();
        elapsed = last != WL_CONNECTED;
        if (last == WL_CONNECTED) {
            Serial.printf("[wifi] connected in %u ms (attempt %d), ip %s, rssi %d dBm\n",
                          static_cast<unsigned>(millis() - t0), attempt + 1,
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
            if (lastStatus) *lastStatus = kk::WifiLinkStatus::Connected;
            if (timedOut) *timedOut = false;
            return true;
        }
        Serial.printf("[wifi] attempt %d failed after %u ms, status %d\n", attempt + 1,
                      static_cast<unsigned>(millis() - t0), static_cast<int>(last));
        if (last == WL_NO_SSID_AVAIL) break;
    }
    if (lastStatus) *lastStatus = linkStatus(last);
    if (timedOut) *timedOut = elapsed;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
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
    const bool ok = joinNetwork(ssid, pass, timeoutMs, nullptr, nullptr);
    wipe(pass);
    return ok;
}

bool DevWifi::connectTo(const String& ssid, const String& pass, uint32_t timeoutMs,
                        kk::WifiLinkStatus* lastStatus, bool* timedOut) {
    return joinNetwork(ssid, pass, timeoutMs, lastStatus, timedOut);
}

size_t DevWifi::scan(kk::WifiNetwork* out, size_t max) {
    if (!out || max == 0) return 0;
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_STA);
    // Blocking scan. Only 2.4 GHz APs are kept so 5 GHz entries cannot fill
    // the caller's buffer or appear in the list; the pure builder filters again
    // for tests and for any caller that supplies its own list.
    const int found = WiFi.scanNetworks();
    size_t written = 0;
    for (int i = 0; i < found && written < max; ++i) {
        const String name = WiFi.SSID(i);
        if (name.isEmpty() || name.length() > static_cast<int>(kk::kWifiSsidMax)) continue;
        const int32_t channel = WiFi.channel(i);
        if (channel < 1 || channel > 14) continue;
        kk::WifiNetwork& entry = out[written];
        entry = kk::WifiNetwork{};
        snprintf(entry.ssid, sizeof entry.ssid, "%s", name.c_str());
        const int32_t rssi = WiFi.RSSI(i);
        entry.rssi = static_cast<int16_t>(rssi < -32768 ? -32768 : rssi > 32767 ? 32767 : rssi);
        entry.channel = static_cast<uint8_t>(channel);
        entry.security = securityFromAuth(WiFi.encryptionType(i));
        ++written;
    }
    WiFi.scanDelete();
    Serial.printf("[wifi] scan: %d visible, %u kept for the list\n", found,
                  static_cast<unsigned>(written));
    return written;
}

void DevWifi::disconnect() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

String DevWifi::ip() { return WiFi.localIP().toString(); }
int DevWifi::rssi() { return WiFi.RSSI(); }
bool DevWifi::connected() { return WiFi.status() == WL_CONNECTED; }

}  // namespace kk

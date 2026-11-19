#pragma once
// Pure helpers for the on-device Wi-Fi setup (SETUP-09): scan-list filtering,
// 2.4 GHz and security rules, SSID/password validation, join-failure reasons
// and password masking. No Arduino headers; natively compiled and tested.
#include <cstddef>
#include <cstdint>

namespace kk {

constexpr size_t kWifiSsidMax = 32;
constexpr size_t kWifiPasswordMin = 8;
constexpr size_t kWifiPasswordMax = 63;

enum class WifiSecurity : uint8_t {
    Open,
    Wep,
    Wpa,
    Wpa2,
    Wpa2Wpa3,
    Wpa3,
    Enterprise,
    Wapi,
    Unknown,
};

// Mirrors the wl_status_t values the ESP32 Arduino core reports, so the pure
// failure classifier does not need WiFi.h.
enum class WifiLinkStatus : uint8_t {
    Idle = 0,
    NoSsid = 1,
    ScanCompleted = 2,
    Connected = 3,
    ConnectFailed = 4,
    ConnectionLost = 5,
    Disconnected = 6,
    Other = 255,
};

enum class WifiPasswordStatus : uint8_t { Ok, Empty, TooShort, TooLong, BadChar };

enum class WifiJoinFailure : uint8_t {
    None,
    NotFound,
    WrongPassword,
    Timeout,
    Unsupported,
    SaveFailed,
};

// One scanned access point. `ssid` is always NUL-terminated; a 32-byte SSID
// fills all 32 bytes plus the terminator.
struct WifiNetwork {
    char ssid[kWifiSsidMax + 1] = {};
    int16_t rssi = 0;
    uint8_t channel = 0;
    WifiSecurity security = WifiSecurity::Unknown;
};

// 2.4 GHz channels are 1..14; 5 GHz starts at 32. Channel 0 is unknown.
bool wifiChannelIs24(uint8_t channel);

// The short label shown in the scan list ("WPA2", "WPA2/3", "open").
const char* wifiSecurityLabel(WifiSecurity security);

// Open networks need no password. Unknown comes from a manual (hidden) SSID
// and behaves like a secured network until the password is empty.
bool wifiSecurityNeedsPassword(WifiSecurity security);

// WEP, WAPI and enterprise need a different flow than the WPA2/WPA3 passphrase
// this screen types, so the on-device setup refuses them.
bool wifiSecuritySupported(WifiSecurity security);

// "||||" strongest down to "|", for the list's right-hand column.
const char* wifiSignalLabel(int16_t rssi);

// Filters `raw` to 2.4 GHz entries with a valid, non-empty SSID, keeps the
// strongest AP for each SSID, sorts by RSSI descending (SSID ascending on a
// tie) and writes at most `max` entries to `out`. Returns the count written.
// In-place use (out == raw) is allowed.
size_t wifiBuildScanList(const WifiNetwork* raw, size_t count, WifiNetwork* out,
                         size_t max);

// A 1..32 byte SSID with no control characters. Matches what DevWifi stores.
bool wifiSsidValid(const char* ssid);

// WPA2/WPA3 passphrase: 8..63 printable ASCII bytes. `allowEmpty` marks a
// manual SSID as an open network; a scanned open network skips this check.
WifiPasswordStatus wifiCheckPassword(const char* pass, bool allowEmpty);
const char* wifiPasswordError(WifiPasswordStatus status);

// Maps the final link status and whether the join timed out to the reason the
// password screen shows. Enterprise/WEP are unsupported before any join.
WifiJoinFailure wifiClassifyFailure(WifiLinkStatus status, bool timedOut,
                                    WifiSecurity security);
const char* wifiFailureLabel(WifiJoinFailure failure);

// Copies `text` to `out`, replacing every character with '*' unless `show`.
// Always NUL-terminates. Returns the number of visible characters copied.
size_t wifiMaskText(const char* text, bool show, char* out, size_t size);

}  // namespace kk

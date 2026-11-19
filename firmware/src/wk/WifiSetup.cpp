#include "wk/WifiSetup.h"

#include <cstring>

namespace kk {

namespace {

bool isPrintableAscii(unsigned char c) { return c >= 0x20 && c <= 0x7E; }

bool sameSsid(const WifiNetwork& a, const WifiNetwork& b) {
    return std::strcmp(a.ssid, b.ssid) == 0;
}

}  // namespace

bool wifiChannelIs24(uint8_t channel) { return channel >= 1 && channel <= 14; }

const char* wifiSecurityLabel(WifiSecurity security) {
    switch (security) {
        case WifiSecurity::Open: return "open";
        case WifiSecurity::Wep: return "WEP";
        case WifiSecurity::Wpa: return "WPA";
        case WifiSecurity::Wpa2: return "WPA2";
        case WifiSecurity::Wpa2Wpa3: return "WPA2/3";
        case WifiSecurity::Wpa3: return "WPA3";
        case WifiSecurity::Enterprise: return "802.1X";
        case WifiSecurity::Wapi: return "WAPI";
        case WifiSecurity::Unknown: return "?";
    }
    return "?";
}

bool wifiSecurityNeedsPassword(WifiSecurity security) {
    return security != WifiSecurity::Open;
}

bool wifiSecuritySupported(WifiSecurity security) {
    switch (security) {
        case WifiSecurity::Open:
        case WifiSecurity::Wpa:
        case WifiSecurity::Wpa2:
        case WifiSecurity::Wpa2Wpa3:
        case WifiSecurity::Wpa3:
        case WifiSecurity::Unknown:
            return true;
        case WifiSecurity::Wep:
        case WifiSecurity::Enterprise:
        case WifiSecurity::Wapi:
            return false;
    }
    return false;
}

const char* wifiSignalLabel(int16_t rssi) {
    if (rssi >= -55) return "||||";
    if (rssi >= -67) return "|||";
    if (rssi >= -78) return "||";
    return "|";
}

size_t wifiBuildScanList(const WifiNetwork* raw, size_t count, WifiNetwork* out,
                         size_t max) {
    if (!raw || !out || max == 0) return 0;
    size_t written = 0;
    for (size_t i = 0; i < count; ++i) {
        // Copy first: out may alias raw and an earlier write can overwrite the
        // entry this index still has to read.
        WifiNetwork candidate = raw[i];
        if (!wifiChannelIs24(candidate.channel)) continue;
        if (!wifiSsidValid(candidate.ssid)) continue;

        bool duplicate = false;
        for (size_t j = 0; j < written; ++j) {
            if (!sameSsid(out[j], candidate)) continue;
            duplicate = true;
            if (candidate.rssi > out[j].rssi) out[j] = candidate;
            break;
        }
        if (duplicate) continue;
        if (written < max) {
            out[written++] = candidate;
            continue;
        }
        // Full list: keep the strongest `max` entries instead of the first
        // ones, so a scan with many APs still offers the best signal.
        size_t weakest = 0;
        for (size_t j = 1; j < written; ++j) {
            const bool jWeaker =
                out[j].rssi < out[weakest].rssi ||
                (out[j].rssi == out[weakest].rssi &&
                 std::strcmp(out[j].ssid, out[weakest].ssid) > 0);
            if (jWeaker) weakest = j;
        }
        const bool candidateBetter =
            candidate.rssi > out[weakest].rssi ||
            (candidate.rssi == out[weakest].rssi &&
             std::strcmp(candidate.ssid, out[weakest].ssid) < 0);
        if (candidateBetter) out[weakest] = candidate;
    }

    // Strongest first; an SSID tie is alphabetical so the order is stable.
    for (size_t i = 1; i < written; ++i) {
        WifiNetwork item = out[i];
        size_t j = i;
        while (j > 0) {
            const WifiNetwork& prev = out[j - 1];
            const bool itemFirst =
                item.rssi > prev.rssi ||
                (item.rssi == prev.rssi && std::strcmp(item.ssid, prev.ssid) < 0);
            if (!itemFirst) break;
            out[j] = prev;
            --j;
        }
        out[j] = item;
    }
    return written;
}

bool wifiSsidValid(const char* ssid) {
    if (!ssid) return false;
    const size_t length = std::strlen(ssid);
    if (length == 0 || length > kWifiSsidMax) return false;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = static_cast<unsigned char>(ssid[i]);
        if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
}

WifiPasswordStatus wifiCheckPassword(const char* pass, bool allowEmpty) {
    if (!pass) pass = "";
    const size_t length = std::strlen(pass);
    if (length == 0) return allowEmpty ? WifiPasswordStatus::Ok : WifiPasswordStatus::Empty;
    if (length < kWifiPasswordMin) return WifiPasswordStatus::TooShort;
    if (length > kWifiPasswordMax) return WifiPasswordStatus::TooLong;
    for (size_t i = 0; i < length; ++i) {
        if (!isPrintableAscii(static_cast<unsigned char>(pass[i])))
            return WifiPasswordStatus::BadChar;
    }
    return WifiPasswordStatus::Ok;
}

const char* wifiPasswordError(WifiPasswordStatus status) {
    switch (status) {
        case WifiPasswordStatus::Ok: return "";
        case WifiPasswordStatus::Empty: return "Enter the password";
        case WifiPasswordStatus::TooShort: return "At least 8 characters";
        case WifiPasswordStatus::TooLong: return "At most 63 characters";
        case WifiPasswordStatus::BadChar: return "Use printable ASCII";
    }
    return "Check the password";
}

WifiJoinFailure wifiClassifyFailure(WifiLinkStatus status, bool timedOut,
                                    WifiSecurity security) {
    if (!wifiSecuritySupported(security)) return WifiJoinFailure::Unsupported;
    switch (status) {
        case WifiLinkStatus::NoSsid: return WifiJoinFailure::NotFound;
        case WifiLinkStatus::ConnectFailed: return WifiJoinFailure::WrongPassword;
        case WifiLinkStatus::ConnectionLost: return WifiJoinFailure::WrongPassword;
        case WifiLinkStatus::Disconnected:
            // A full timeout in WL_DISCONNECTED is the common wrong-password
            // state; without the timeout it is a transient link drop.
            return timedOut ? WifiJoinFailure::WrongPassword : WifiJoinFailure::Timeout;
        case WifiLinkStatus::Idle: return WifiJoinFailure::Timeout;
        case WifiLinkStatus::Other:
        case WifiLinkStatus::ScanCompleted:
        case WifiLinkStatus::Connected: return WifiJoinFailure::Timeout;
    }
    return WifiJoinFailure::Timeout;
}

const char* wifiFailureLabel(WifiJoinFailure failure) {
    switch (failure) {
        case WifiJoinFailure::None: return "";
        case WifiJoinFailure::NotFound: return "not found";
        case WifiJoinFailure::WrongPassword: return "wrong password";
        case WifiJoinFailure::Timeout: return "timeout";
        case WifiJoinFailure::Unsupported: return "unsupported";
        case WifiJoinFailure::SaveFailed: return "save failed";
    }
    return "";
}

size_t wifiMaskText(const char* text, bool show, char* out, size_t size) {
    if (!out || size == 0) return 0;
    const size_t length = text ? std::strlen(text) : 0;
    const size_t copied = length < size - 1 ? length : size - 1;
    for (size_t i = 0; i < copied; ++i) out[i] = show ? text[i] : '*';
    out[copied] = '\0';
    return copied;
}

}  // namespace kk

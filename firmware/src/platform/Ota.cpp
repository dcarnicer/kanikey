// Firmware updates over the air; see Ota.h and docs/ota.md.
#include "platform/Ota.h"

#include <Preferences.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>

#include "Version.h"
#include "platform/RootCerts.h"

#ifndef KANIKEY_ENV
#define KANIKEY_ENV "unknown"
#endif

namespace kk::ota {
namespace {

constexpr size_t kMaxRedirects = 4;
constexpr size_t kHeaderLineMax = 512;

struct Url {
    bool https = true;
    String host;
    uint16_t port = 443;
    String path;
};

bool parseUrl(std::string_view text, Url* out) {
    if (!out) return false;
    size_t schemeLen = 0;
    bool https = true;
    if (text.rfind("https://", 0) == 0) {
        schemeLen = 8;
        https = true;
    } else if (text.rfind("http://", 0) == 0) {
        schemeLen = 7;
        https = false;
    } else {
        return false;
    }
    text.remove_prefix(schemeLen);
    const size_t slash = text.find('/');
    std::string_view authority = slash == std::string_view::npos ? text : text.substr(0, slash);
    if (authority.empty() || authority.find('@') != std::string_view::npos) return false;
    std::string_view host = authority;
    uint16_t port = https ? 443 : 80;
    const size_t colon = authority.rfind(':');
    if (colon != std::string_view::npos) {
        host = authority.substr(0, colon);
        const std::string_view portText = authority.substr(colon + 1);
        if (portText.empty() || portText.size() > 5) return false;
        uint32_t value = 0;
        for (char c : portText) {
            if (c < '0' || c > '9') return false;
            value = value * 10 + static_cast<uint32_t>(c - '0');
        }
        if (value == 0 || value > 65535) return false;
        port = static_cast<uint16_t>(value);
    }
    if (host.empty()) return false;
    out->https = https;
    out->host = String(host.data(), host.size());
    out->port = port;
    out->path = slash == std::string_view::npos
                    ? String("/")
                    : String(text.substr(slash).data(), text.size() - slash);
    return true;
}

bool validHttpUrl(std::string_view text) {
    return text.rfind("https://", 0) == 0
#ifdef KANIKEY_DEV
           || text.rfind("http://", 0) == 0
#endif
        ;
}

std::string hexDigest(const unsigned char digest[32]) {
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (int i = 0; i < 32; ++i) {
        out.push_back(kHex[digest[i] >> 4]);
        out.push_back(kHex[digest[i] & 15]);
    }
    return out;
}

bool sameHex(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const char x = a[i] >= 'A' && a[i] <= 'F' ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
        const char y = b[i] >= 'A' && b[i] <= 'F' ? static_cast<char>(b[i] - 'A' + 'a') : b[i];
        if (x != y) return false;
    }
    return true;
}

bool parseContentLength(const String& line, long* out) {
    const int colon = line.indexOf(':');
    if (colon < 0) return false;
    String value = line.substring(colon + 1);
    value.trim();
    if (value.isEmpty()) return false;
    long n = 0;
    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value[i];
        if (c < '0' || c > '9') return false;
        if (n > 100000000L) return false;
        n = n * 10 + (c - '0');
    }
    *out = n;
    return true;
}

bool isOurSlot(const esp_partition_t* p) {
    if (!p || p->type != ESP_PARTITION_TYPE_APP) return false;
    return (p->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 && std::strcmp(p->label, "app0") == 0) ||
           (p->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1 && std::strcmp(p->label, "app1") == 0);
}

// One HTTP(S) request with redirects. The body is delivered to `onBody`, which
// returns false to stop. The connection is short-lived and closed before this
// returns.
FetchStatus request(const String& startUrl, long* contentLength, int* httpCode, int* tlsError,
                    uint32_t timeoutMs,
                    const std::function<bool(WiFiClient&, size_t)>& onBody) {
    *httpCode = 0;
    *tlsError = 0;
    if (!validHttpUrl(std::string_view(startUrl.c_str(), startUrl.length()))) {
        return FetchStatus::BadUrl;
    }
    String current = startUrl;
    for (size_t redirect = 0; redirect <= kMaxRedirects; ++redirect) {
        *httpCode = 0;
        *tlsError = 0;
        Url url;
        if (!parseUrl(std::string_view(current.c_str(), current.length()), &url)) {
            return FetchStatus::BadUrl;
        }
        if (!url.https) {
#ifdef KANIKEY_DEV
            // Local test server only.
#else
            return FetchStatus::BadUrl;
#endif
        }
        std::unique_ptr<WiFiClientSecure> secure;
        std::unique_ptr<WiFiClient> plain;
        WiFiClient* client = nullptr;
        if (url.https) {
            secure.reset(new WiFiClientSecure());
            secure->setCACert(tgc::kRootCaPem);
            secure->setTimeout(timeoutMs / 1000);
            client = secure.get();
        } else {
            plain.reset(new WiFiClient());
            plain->setTimeout(timeoutMs / 1000);
            client = plain.get();
        }
        if (!client->connect(url.host.c_str(), url.port)) {
            if (url.https) {
                char err[80] = "";
                *tlsError = secure->lastError(err, sizeof err);
            }
            return url.https && *tlsError ? FetchStatus::Tls : FetchStatus::Connect;
        }
        client->print("GET ");
        client->print(url.path);
        client->print(" HTTP/1.0\r\nHost: ");
        client->print(url.host);
        if ((url.https && url.port != 443) || (!url.https && url.port != 80)) {
            client->print(":");
            client->print(url.port);
        }
        client->print("\r\nUser-Agent: KaniKey/");
        client->print(kk::kFirmwareVersion);
        client->print(" (unofficial WaniKani client)\r\n"
                      "Accept: */*\r\nConnection: close\r\n\r\n");
        const uint32_t started = millis();
        String statusLine;
        while (statusLine.length() == 0 && millis() - started < timeoutMs) {
            if (!client->available() && !client->connected()) break;
            statusLine = client->readStringUntil('\n');
            statusLine.trim();
            if (statusLine.length() > 256) {
                client->stop();
                return FetchStatus::Http;
            }
        }
        int code = 0;
        if (statusLine.startsWith("HTTP/")) {
            const int sp = statusLine.indexOf(' ');
            if (sp > 0) code = statusLine.substring(sp + 1, sp + 4).toInt();
        }
        *httpCode = code;
        long length = -1;
        String location;
        while (millis() - started < timeoutMs) {
            if (!client->available() && !client->connected()) break;
            String line = client->readStringUntil('\n');
            line.trim();
            if (line.isEmpty()) break;
            if (line.length() > kHeaderLineMax) break;
            String lower = line;
            lower.toLowerCase();
            if (lower.startsWith("content-length:")) parseContentLength(line, &length);
            else if (lower.startsWith("location:")) {
                location = line.substring(9);
                location.trim();
            }
        }
        if (code >= 300 && code < 400 && !location.isEmpty() && redirect < kMaxRedirects) {
            current = resolveUrl(std::string_view(current.c_str(), current.length()),
                                 std::string_view(location.c_str(), location.length())).c_str();
            if (current.isEmpty()) return FetchStatus::BadUrl;
            client->stop();
            continue;
        }
        if (code != 200) {
            client->stop();
            if (contentLength) *contentLength = length;
            return FetchStatus::Http;
        }
        if (contentLength) *contentLength = length;
        const bool ok = onBody(*client, length < 0 ? 0 : static_cast<size_t>(length));
        client->stop();
        return ok ? FetchStatus::Ok : FetchStatus::SizeMismatch;
    }
    return FetchStatus::BadUrl;
}

}  // namespace

const char* fetchStatusString(FetchStatus status) {
    switch (status) {
        case FetchStatus::Ok: return "ok";
        case FetchStatus::Connect: return "connect failed";
        case FetchStatus::Tls: return "TLS failed";
        case FetchStatus::Http: return "HTTP error";
        case FetchStatus::TooLarge: return "too large";
        case FetchStatus::SizeMismatch: return "size mismatch";
        case FetchStatus::HashMismatch: return "hash mismatch";
        case FetchStatus::BadImage: return "bad image";
        case FetchStatus::WriteFailed: return "write failed";
        case FetchStatus::NoSlot: return "no update slot";
        case FetchStatus::BadUrl: return "bad URL";
        case FetchStatus::Cancelled: return "cancelled";
    }
    return "?";
}

const char* buildEnv() { return KANIKEY_ENV; }

bool layoutIsOurs() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    const esp_partition_t* otadata =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
    return otadata && isOurSlot(running) && isOurSlot(next) && running != next &&
           running->subtype != next->subtype && running->size == next->size;
}

uint32_t slotBytes() {
    return layoutIsOurs() ? esp_ota_get_next_update_partition(nullptr)->size : 0;
}

const char* runningSlot() {
    const esp_partition_t* p = esp_ota_get_running_partition();
    return p ? p->label : "";
}

const char* nextSlot() {
    const esp_partition_t* p = esp_ota_get_next_update_partition(nullptr);
    return p ? p->label : "";
}

bool pendingVerify() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    return running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
}

bool markValid() {
    if (!pendingVerify()) return false;
    const esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
    if (e != ESP_OK) Serial.printf("[ota] mark valid failed: %s\n", esp_err_to_name(e));
    return e == ESP_OK;
}

const char* lastInvalidSlot() {
    const esp_partition_t* p = esp_ota_get_last_invalid_partition();
    return p ? p->label : "";
}

void rollBackNow() {
    Serial.printf("[ota] %s failed at boot: back to the previous image\n", runningSlot());
    Serial.flush();
    esp_ota_mark_app_invalid_rollback_and_reboot();
}

FetchResult fetchText(std::string_view url, size_t maxBytes, std::string* out,
                      uint32_t timeoutMs) {
    FetchResult result;
    if (!out) {
        result.status = FetchStatus::BadUrl;
        return result;
    }
    out->clear();
    long contentLength = -1;
    size_t got = 0;
    bool tooLarge = false;
    const FetchStatus status = request(
        String(url.data(), url.size()), &contentLength, &result.httpCode, &result.tlsError,
        timeoutMs,
        [&](WiFiClient& client, size_t length) {
            if (length > maxBytes) {
                tooLarge = true;
                return false;
            }
            uint8_t buf[512];
            const uint32_t started = millis();
            uint32_t lastData = started;
            while (millis() - started < timeoutMs) {
                const int avail = client.available();
                if (avail > 0) {
                    const size_t take = std::min<size_t>(sizeof buf, static_cast<size_t>(avail));
                    const int n = client.read(buf, take);
                    if (n > 0) {
                        if (got + static_cast<size_t>(n) > maxBytes) {
                            tooLarge = true;
                            return false;
                        }
                        out->append(reinterpret_cast<const char*>(buf), static_cast<size_t>(n));
                        got += static_cast<size_t>(n);
                        lastData = millis();
                        continue;
                    }
                }
                if (!client.connected() && !client.available()) break;
                if (millis() - lastData > 3000) break;
                delay(2);
            }
            return true;
        });
    result.status = tooLarge ? FetchStatus::TooLarge : status;
    result.bytes = got;
    if (result.status == FetchStatus::Ok && contentLength >= 0 &&
        got != static_cast<size_t>(contentLength)) {
        result.status = FetchStatus::SizeMismatch;
    }
    return result;
}

FetchResult install(const AppImage& app, ProgressFn progress, void* progressCtx,
                    uint32_t idleTimeoutMs, uint32_t totalTimeoutMs) {
    FetchResult result;
    if (!layoutIsOurs()) {
        result.status = FetchStatus::NoSlot;
        return result;
    }
    if (app.bytes > slotBytes()) {
        result.status = FetchStatus::TooLarge;
        return result;
    }
    if (!validHttpUrl(std::string_view(app.url.c_str(), app.url.length()))) {
        result.status = FetchStatus::BadUrl;
        return result;
    }

    Serial.printf("[ota] installing %s (%u bytes) into %s\n", app.url.c_str(),
                  static_cast<unsigned>(app.bytes), nextSlot());

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts_ret(&sha, 0);
    bool updateBegun = false;
    size_t written = 0;
    unsigned char digest[32] = {};
    bool bodyDone = false;
    FetchStatus failure = FetchStatus::Ok;
    uint32_t lastProgress = 0;

    const FetchStatus status = request(
        String(app.url.c_str(), app.url.length()), nullptr, &result.httpCode, &result.tlsError,
        idleTimeoutMs,
        [&](WiFiClient& client, size_t length) {
            if (length != 0 && length != static_cast<size_t>(app.bytes)) {
                failure = FetchStatus::SizeMismatch;
                return false;
            }
            if (!Update.begin(app.bytes)) {
                const uint8_t e = Update.getError();
                failure = e == UPDATE_ERROR_SIZE           ? FetchStatus::TooLarge
                          : e == UPDATE_ERROR_NO_PARTITION ? FetchStatus::NoSlot
                                                           : FetchStatus::WriteFailed;
                Serial.printf("[ota] Update.begin: %s\n", Update.errorString());
                return false;
            }
            updateBegun = true;
            const uint32_t started = millis();
            uint32_t lastData = started;
            uint8_t buf[4096];
            while (written < app.bytes && millis() - started < totalTimeoutMs) {
                const int avail = client.available();
                if (avail > 0) {
                    const size_t take = std::min<size_t>(
                        sizeof buf, std::min<size_t>(static_cast<size_t>(avail), app.bytes - written));
                    const int n = client.read(buf, take);
                    if (n > 0) {
                        if (Update.write(buf, static_cast<size_t>(n)) !=
                            static_cast<size_t>(n)) {
                            failure = Update.getError() == UPDATE_ERROR_MAGIC_BYTE
                                          ? FetchStatus::BadImage
                                          : FetchStatus::WriteFailed;
                            Serial.printf("[ota] Update.write at %u: %s\n",
                                          static_cast<unsigned>(written), Update.errorString());
                            return false;
                        }
                        mbedtls_sha256_update_ret(&sha, buf, static_cast<size_t>(n));
                        written += static_cast<size_t>(n);
                        lastData = millis();
                        if (written - lastProgress >= 32768 || written == app.bytes) {
                            lastProgress = static_cast<uint32_t>(written);
                            if (progress && !progress(written, app.bytes, progressCtx)) {
                                failure = FetchStatus::Cancelled;
                                return false;
                            }
                        }
                        continue;
                    }
                }
                if (!client.connected() && !client.available()) break;
                if (millis() - lastData > idleTimeoutMs) break;
                delay(2);
            }
            if (written != app.bytes) {
                failure = FetchStatus::SizeMismatch;
                return false;
            }
            bodyDone = true;
            return true;
        });

    result.bytes = written;
    if (status != FetchStatus::Ok) {
        result.status = failure != FetchStatus::Ok ? failure : status;
    } else if (!bodyDone || written != app.bytes) {
        result.status = FetchStatus::SizeMismatch;
    } else {
        mbedtls_sha256_finish_ret(&sha, digest);
        const std::string hash = hexDigest(digest);
        if (!sameHex(hash, app.sha256)) {
            Serial.printf("[ota] sha256 %s, expected %s\n", hash.c_str(), app.sha256.c_str());
            result.status = FetchStatus::HashMismatch;
        } else {
            result.status = FetchStatus::Ok;
        }
    }
    mbedtls_sha256_free(&sha);
    if (result.status != FetchStatus::Ok && updateBegun) Update.abort();
    Serial.printf("[ota] install: %s (%u/%u bytes, http %d)\n",
                  fetchStatusString(result.status), static_cast<unsigned>(written),
                  static_cast<unsigned>(app.bytes), result.httpCode);
    return result;
}

bool activate() {
    if (Update.end()) return true;
    Serial.printf("[ota] Update.end: %s\n", Update.errorString());
    return false;
}

void abandon() { Update.abort(); }

String manifestUrl() {
#ifdef KANIKEY_DEV
    Preferences p;
    if (p.begin("kanikey", true)) {
        const String value = p.isKey("fwurl") ? p.getString("fwurl", "") : String();
        p.end();
        if (!value.isEmpty() && value.length() <= 200 &&
            validHttpUrl(std::string_view(value.c_str(), value.length())))
            return value;
    }
#endif
    return String(kDefaultManifestUrl);
}

bool setManifestUrl(const String& url) {
#ifdef KANIKEY_DEV
    Preferences p;
    if (!p.begin("kanikey", false)) return false;
    bool ok = true;
    if (url.isEmpty())
        p.remove("fwurl");
    else if (url.length() <= 200 && validHttpUrl(std::string_view(url.c_str(), url.length())))
        ok = p.putString("fwurl", url) == url.length();
    else
        ok = false;
    p.end();
    return ok;
#else
    (void)url;
    return false;
#endif
}

SavedState loadSavedState() {
    SavedState state;
    Preferences p;
    if (!p.begin("kanikey", true)) return state;
    if (p.isKey("fwbadge")) state.badgeVersion = p.getString("fwbadge", "").c_str();
    state.badgeBytes = p.isKey("fwbytes") ? p.getUInt("fwbytes", 0) : 0;
    if (p.isKey("fwfailed")) state.failedVersion = p.getString("fwfailed", "").c_str();
    if (p.isKey("fwnew")) state.installing = p.getString("fwnew", "").c_str();
    if (p.isKey("fwslot")) state.installSlot = p.getString("fwslot", "").c_str();
    state.lastCheckAt = p.isKey("fwcheck") ? p.getUInt("fwcheck", 0) : 0;
    p.end();
    return state;
}

void saveBadge(std::string_view version, uint32_t bytes) {
    Preferences p;
    if (!p.begin("kanikey", false)) return;
    if (version.empty()) {
        p.remove("fwbadge");
        p.remove("fwbytes");
    } else {
        p.putString("fwbadge", String(version.data(), version.size()));
        p.putUInt("fwbytes", bytes);
    }
    p.end();
}

void saveFailedVersion(std::string_view version) {
    Preferences p;
    if (!p.begin("kanikey", false)) return;
    if (version.empty())
        p.remove("fwfailed");
    else
        p.putString("fwfailed", String(version.data(), version.size()));
    p.end();
}

void saveInstallRecord(std::string_view version, std::string_view slot) {
    Preferences p;
    if (!p.begin("kanikey", false)) return;
    p.putString("fwnew", String(version.data(), version.size()));
    p.putString("fwslot", String(slot.data(), slot.size()));
    p.end();
}

void clearInstallRecord() {
    Preferences p;
    if (!p.begin("kanikey", false)) return;
    p.remove("fwnew");
    p.remove("fwslot");
    p.end();
}

void saveLastCheck(uint32_t epoch) {
    Preferences p;
    if (!p.begin("kanikey", false)) return;
    p.putUInt("fwcheck", epoch);
    p.end();
}

}  // namespace kk::ota

// initArduino() marks a pending image valid before setup() unless this returns
// true. KaniKey marks it valid itself after the loop has run (App.cpp).
extern "C" bool verifyRollbackLater() { return kk::ota::layoutIsOurs(); }

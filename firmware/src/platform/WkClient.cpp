#include "platform/WkClient.h"

#include <WiFiClientSecure.h>

#include <algorithm>
#include <cstring>
#include <esp_heap_caps.h>

#include "platform/RootCerts.h"

namespace kk {

namespace {
WkMemHook gBeforeRequest = nullptr;
WkMemHook gAfterRequest = nullptr;

// Shared response buffer: keeping bodies out of the heap leaves the UI's frame
// buffer block contiguous across the request that frees and restores it.
char gBodyBuffer[32768];

struct RequestHooks {
    RequestHooks() {
        if (gBeforeRequest) gBeforeRequest();
    }
    ~RequestHooks() {
        if (gAfterRequest) gAfterRequest();
    }
};

void logHeap(const char* when) {
    Serial.printf("[http] %s: heap %u free, %u largest, %u min\n", when,
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                         MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
}
}  // namespace

void wkSetRequestHooks(WkMemHook before, WkMemHook after) {
    gBeforeRequest = before;
    gAfterRequest = after;
}

namespace {
WkResponse request(const String& method, const String& path, const String& token,
                    const String& body, size_t maxBodyBytes, uint32_t timeoutMs) {
    WkResponse r;
    RequestHooks hooks;
    logHeap("before");
    const uint32_t t0 = millis();

    WiFiClientSecure client;
    client.setCACert(tgc::kRootCaPem);
    client.setTimeout(timeoutMs / 1000);

    if (!client.connect("api.wanikani.com", 443)) {
        char err[80] = "";
        r.tlsError = client.lastError(err, sizeof err);
        r.ms = millis() - t0;
        Serial.printf("[http] connect failed after %u ms, error %d %s\n",
                      static_cast<unsigned>(r.ms), r.tlsError, err);
        client.stop();
        logHeap("after failed connect");
        return r;
    }
    r.connected = true;

    client.print(method);
    client.print(" ");
    client.print(path);
    client.print(" HTTP/1.0\r\n"
                 "Host: api.wanikani.com\r\n"
                 "User-Agent: KaniKey/0.1 (unofficial WaniKani client)\r\n"
                 "Wanikani-Revision: 20170710\r\n"
                 "Accept: application/json\r\n"
                 "Connection: close\r\n");
    if (method == "POST") {
        client.print("Content-Type: application/json\r\n");
        client.print("Content-Length: ");
        client.print(body.length());
        client.print("\r\n");
    }
    if (!token.isEmpty()) {
        client.print("Authorization: Bearer ");
        client.print(token);
        client.print("\r\n");
    }
    client.print("\r\n");
    if (!body.isEmpty()) client.print(body);

    r.statusLine = client.readStringUntil('\n');
    r.statusLine.trim();
    if (r.statusLine.startsWith("HTTP/")) {
        const int sp = r.statusLine.indexOf(' ');
        if (sp > 0) r.httpCode = r.statusLine.substring(sp + 1, sp + 4).toInt();
    }

    int contentLength = -1;
    while (client.connected() || client.available()) {
        String line = client.readStringUntil('\n');
        if (line == "\r" || line.isEmpty()) break;
        if (line.startsWith("Content-Length:")) contentLength = line.substring(15).toInt();
        if (millis() - t0 > timeoutMs) break;
    }

    const size_t cap = std::min<size_t>(maxBodyBytes, sizeof gBodyBuffer);
    size_t bodyLen = 0;
    if (contentLength >= 0) {
        size_t want = static_cast<size_t>(contentLength);
        if (want > cap) {
            want = cap;
            r.truncated = true;
        }
        uint8_t buf[512];
        uint32_t lastData = millis();
        while (bodyLen < want) {
            const int avail = client.available();
            if (avail > 0) {
                const size_t take =
                    std::min<size_t>(sizeof buf, std::min<size_t>(static_cast<size_t>(avail),
                                                                  want - bodyLen));
                const int n = client.read(buf, take);
                if (n > 0) {
                    std::memcpy(gBodyBuffer + bodyLen, buf, static_cast<size_t>(n));
                    bodyLen += static_cast<size_t>(n);
                    lastData = millis();
                    continue;
                }
            }
            if (!client.connected() && !client.available()) break;
            if (millis() - lastData > 3000) break;
            delay(2);
        }
        if (bodyLen < want) r.truncated = true;
    } else {
        while (millis() - t0 < timeoutMs) {
            if (!client.available()) {
                if (!client.connected()) break;
                delay(2);
                continue;
            }
            if (bodyLen >= cap) {
                r.truncated = true;
                break;
            }
            const int c = client.read();
            if (c < 0) break;
            gBodyBuffer[bodyLen++] = static_cast<char>(c);
        }
    }

    r.body = std::string_view(gBodyBuffer, bodyLen);
    r.bytes = bodyLen;
    r.ms = millis() - t0;
    Serial.printf("[http] %s -> %d, %u body bytes, %u ms, cl=%d%s\n", path.c_str(),
                  r.httpCode, static_cast<unsigned>(r.bytes), static_cast<unsigned>(r.ms),
                  contentLength, r.truncated ? " (truncated)" : "");
    client.stop();
    logHeap("after");
    return r;
}
}  // namespace

WkResponse wkGet(const String& path, const String& token, size_t maxBodyBytes,
                 uint32_t timeoutMs) {
    return request("GET", path, token, String(), maxBodyBytes, timeoutMs);
}

WkResponse wkGetRetry(const String& path, const String& token, size_t maxBodyBytes,
                      uint32_t timeoutMs, uint8_t attempts) {
    WkResponse r;
    for (uint8_t i = 0; i < attempts; ++i) {
        r = wkGet(path, token, maxBodyBytes, timeoutMs);
        if (r.httpCode == 200 && !r.truncated) return r;
        Serial.printf("[http] retry %s (%d, %u bytes%s)\n", path.c_str(), r.httpCode,
                      static_cast<unsigned>(r.bytes), r.truncated ? ", truncated" : "");
        delay(300);
    }
    return r;
}

WkResponse wkPost(const String& path, const String& token, const String& body,
                  size_t maxBodyBytes, uint32_t timeoutMs) {
    return request("POST", path, token, body, maxBodyBytes, timeoutMs);
}

WkResponse wkPut(const String& path, const String& token, const String& body,
                 size_t maxBodyBytes, uint32_t timeoutMs) {
    return request("PUT", path, token, body, maxBodyBytes, timeoutMs);
}

}  // namespace kk

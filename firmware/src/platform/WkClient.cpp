#include "platform/WkClient.h"

#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

#include "platform/RootCerts.h"

namespace kk {

namespace {
void logHeap(const char* when) {
    Serial.printf("[http] %s: heap %u free, %u largest, %u min\n", when,
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                         MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
}
}  // namespace

WkResponse wkGet(const String& path, const String& token, size_t maxBodyBytes,
                 uint32_t timeoutMs) {
    WkResponse r;
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

    client.print("GET ");
    client.print(path);
    client.print(" HTTP/1.0\r\n"
                 "Host: api.wanikani.com\r\n"
                 "User-Agent: KaniKey/0.1 (unofficial WaniKani client)\r\n"
                 "Wanikani-Revision: 20170710\r\n"
                 "Accept: application/json\r\n"
                 "Connection: close\r\n");
    if (!token.isEmpty()) {
        client.print("Authorization: Bearer ");
        client.print(token);
        client.print("\r\n");
    }
    client.print("\r\n");

    r.statusLine = client.readStringUntil('\n');
    r.statusLine.trim();
    if (r.statusLine.startsWith("HTTP/")) {
        const int sp = r.statusLine.indexOf(' ');
        if (sp > 0) r.httpCode = r.statusLine.substring(sp + 1, sp + 4).toInt();
    }

    while (client.connected() || client.available()) {
        String line = client.readStringUntil('\n');
        if (line == "\r" || line.isEmpty()) break;
        if (millis() - t0 > timeoutMs) break;
    }

    while (millis() - t0 < timeoutMs) {
        if (!client.available()) {
            if (!client.connected()) break;
            delay(2);
            continue;
        }
        if (r.body.length() >= maxBodyBytes) {
            r.truncated = true;
            break;
        }
        const int c = client.read();
        if (c < 0) break;
        r.body += static_cast<char>(c);
    }

    r.bytes = r.body.length();
    r.ms = millis() - t0;
    Serial.printf("[http] %s -> %d, %u body bytes, %u ms%s\n", path.c_str(), r.httpCode,
                  static_cast<unsigned>(r.bytes), static_cast<unsigned>(r.ms),
                  r.truncated ? " (truncated)" : "");
    client.stop();
    logHeap("after");
    return r;
}

}  // namespace kk

#include "platform/WkProbe.h"

#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

#include <cstdlib>
#include <cstring>

#include "platform/RootCerts.h"

namespace kk {

namespace {
void logHeap(const char* when) {
    Serial.printf("[tls] %s: heap %u free, %u largest, %u min\n", when,
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                         MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
}
}  // namespace

WkProbeResult probeWaniKaniUser(const String& token) {
    WkProbeResult r;
    logHeap("before");
    const uint32_t t0 = millis();

    WiFiClientSecure client;
    client.setCACert(tgc::kRootCaPem);
    client.setTimeout(15);

    if (!client.connect("api.wanikani.com", 443)) {
        char err[80] = "";
        r.tlsError = client.lastError(err, sizeof err);
        r.ms = millis() - t0;
        Serial.printf("[tls] connect failed after %u ms, error %d %s\n",
                      static_cast<unsigned>(r.ms), r.tlsError, err);
        client.stop();
        logHeap("after failed connect");
        return r;
    }
    r.connected = true;
    r.tlsOk = true;

    client.print("GET /v2/user HTTP/1.1\r\n"
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

    // Drain the headers, then a little of the body, so the response is real.
    while (client.connected() || client.available()) {
        String line = client.readStringUntil('\n');
        r.bytes += line.length() + 1;
        if (line == "\r" || line.isEmpty()) break;
        if (millis() - t0 > 20000) break;
    }
    char body[513];
    size_t n = 0;
    while (client.available() && n < sizeof body - 1 && millis() - t0 < 20000) {
        body[n++] = static_cast<char>(client.read());
        ++r.bytes;
    }
    body[n] = 0;

    r.ms = millis() - t0;
    Serial.printf("[tls] %s after %u ms, %u bytes, tlsError %d\n", r.statusLine.c_str(),
                  static_cast<unsigned>(r.ms), static_cast<unsigned>(r.bytes), r.tlsError);
    if (n) Serial.printf("[tls] body: %.320s\n", body);
    if (r.httpCode == 200) {
        const char* u = strstr(body, "\"username\":\"");
        if (u) {
            u += 12;
            const char* e = strchr(u, '"');
            if (e) r.username = String(u).substring(0, static_cast<unsigned>(e - u));
        }
        const char* l = strstr(body, "\"level\":");
        if (l) r.level = atoi(l + 8);
    }
    client.stop();
    logHeap("after");
    return r;
}

}  // namespace kk

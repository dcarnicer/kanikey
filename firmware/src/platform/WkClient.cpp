#include "platform/WkClient.h"

#include <WiFiClientSecure.h>

#include <algorithm>
#include <esp_heap_caps.h>
#include <time.h>

#include "platform/RootCerts.h"
#include "Version.h"
#include "wk/HttpChunked.h"
#include "wk/RateLimit.h"

namespace kk {

namespace {
WkMemHook gBeforeRequest = nullptr;
WkMemHook gAfterRequest = nullptr;

// Shared response buffer: keeping bodies out of the heap leaves the UI's frame
// buffer block contiguous across the request that frees and restores it.
char gBodyBuffer[32768];

// Shared 429 gate: GET, POST and PUT all pass through request(), so once a 429
// arms this deadline no request can reach the network before it expires.
uint32_t gRateLimitDeadlineMs = 0;
uint32_t gRateLimitResetEpoch = 0;
bool gRateLimitGateActive = false;

// Shared 401 gate: once the API rejects the token, every request is answered
// locally with the same 401 until the token is saved, validated or cleared.
// Missing scope (403) is endpoint-specific and deliberately does not arm it.
bool gAuthBlocked = false;
int gAuthHttpCode = 0;

// Set by the first 2xx status; read by the OTA verification window.
bool gHadApiSuccess = false;

// The Cardputer has no RTC; anything earlier than this is a clock that has not
// been set yet, so an epoch reset header cannot be compared against it.
constexpr uint32_t kTrustedClockEpoch = 1700000000u;

// A stalled TLS handshake otherwise waits for the framework's 120 s default.
constexpr unsigned long kHandshakeTimeoutS = 15;

// A keep-alive socket that stops delivering body bytes is broken, not slow.
constexpr uint32_t kBodyIdleTimeoutMs = 3000;

// One socket for the whole session (SPEED-05). A non-session request uses the
// same object one request at a time; WiFiClientSecure keeps no response state
// between connect() and stop().
WiFiClientSecure gSessionClient;
WiFiClientSecure gLocalClient;
uint16_t gSessionDepth = 0;
uint32_t gSessionRequests = 0;

struct RequestHooks {
    explicit RequestHooks(bool enabled) : enabled(enabled) {
        if (enabled && gBeforeRequest) gBeforeRequest();
    }
    ~RequestHooks() {
        if (enabled && gAfterRequest) gAfterRequest();
    }
    bool enabled;
};

void logHeap(const char* when) {
    Serial.printf("[http] %s: heap %u free, %u largest, %u min\n", when,
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                         MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
}

// Reads one CRLF-terminated line. Returns false on EOF or the deadline instead
// of letting readStringUntil() sit on Stream's timeout when the peer is gone.
bool readLine(WiFiClientSecure& client, String& line, uint32_t deadline) {
    line = "";
    while (static_cast<int32_t>(deadline - millis()) > 0) {
        if (client.available() > 0) {
            line = client.readStringUntil('\n');
            return true;
        }
        if (!client.connected()) return false;
        delay(1);
    }
    return false;
}

struct Attempt {
    WkResponse r;
    bool retryable = false;
    bool keepAlive = false;
};

// One request on an already chosen connection. A session reuses its client;
// otherwise the caller's client is stopped and reopened for this request.
Attempt performRequest(WiFiClientSecure& client, const String& method, const String& path,
                       const String& token, const String& body, size_t maxBodyBytes,
                       uint32_t timeoutMs, char* buffer, size_t bufferSize) {
    Attempt a;
    WkResponse& r = a.r;
    const uint32_t t0 = millis();
    const bool session = gSessionDepth > 0;
    const bool reused = client.connected();

    if (reused) {
        Serial.printf("[http] %s reuse\n", path.c_str());
    } else {
        client.stop();
        client.setCACert(tgc::kRootCaPem);
        client.setHandshakeTimeout(kHandshakeTimeoutS);
        client.setTimeout(timeoutMs / 1000);
        if (!client.connect("api.wanikani.com", 443)) {
            char err[80] = "";
            r.tlsError = client.lastError(err, sizeof err);
            r.ms = millis() - t0;
            Serial.printf("[http] connect failed after %u ms, error %d %s\n",
                          static_cast<unsigned>(r.ms), r.tlsError, err);
            client.stop();
            a.retryable = true;
            return a;
        }
        Serial.printf("[http] %s connect %u ms\n", path.c_str(),
                      static_cast<unsigned>(millis() - t0));
    }
    r.connected = true;
    // WiFiClientSecure::setTimeout() hides Stream::setTimeout(): the former
    // sets the socket options, the latter the readStringUntil() limit, which
    // would otherwise stay at the 1 s default.
    client.setTimeout(timeoutMs / 1000);
    // Header lines are short; a dead keep-alive socket must not sit on the
    // full request timeout before the one retry can happen.
    static_cast<Stream&>(client).setTimeout(std::min<uint32_t>(timeoutMs, 5000));

    String req;
    req.reserve(260 + path.length() + token.length() + body.length());
    req += method;
    req += ' ';
    req += path;
    if (session) {
        req += " HTTP/1.1\r\n"
               "Host: api.wanikani.com\r\n"
               "User-Agent: KaniKey/";
    } else {
        // Outside a session there is nothing to reuse: keep the old HTTP/1.0
        // shape so a chunked body can never appear on this path.
        req += " HTTP/1.0\r\n"
               "Host: api.wanikani.com\r\n"
               "User-Agent: KaniKey/";
    }
    req += kk::kFirmwareVersion;
    req += " (unofficial WaniKani client)\r\n"
           "Wanikani-Revision: 20170710\r\n"
           "Accept: application/json\r\n";
    req += session ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    if (method == "POST" || method == "PUT") {
        req += "Content-Type: application/json\r\n";
        req += "Content-Length: ";
        req += String(body.length());
        req += "\r\n";
    }
    if (!token.isEmpty()) {
        req += "Authorization: Bearer ";
        req += token;
        req += "\r\n";
    }
    req += "\r\n";
    req += body;

    const size_t written =
        client.write(reinterpret_cast<const uint8_t*>(req.c_str()), req.length());
    if (written != req.length()) {
        r.ms = millis() - t0;
        Serial.printf("[http] %s write %u/%u bytes\n", path.c_str(),
                      static_cast<unsigned>(written), static_cast<unsigned>(req.length()));
        // A GET may be retried after any transport failure. A POST/PUT is only
        // retried when no byte left the device, so a review cannot be sent twice.
        a.retryable = written == 0 || method == "GET";
        client.stop();
        return a;
    }

    String status;
    if (!readLine(client, status, t0 + timeoutMs)) {
        r.ms = millis() - t0;
        Serial.printf("[http] %s no status line\n", path.c_str());
        a.retryable = method == "GET";
        client.stop();
        return a;
    }
    status.trim();
    r.statusLine = status;
    const bool http11 = r.statusLine.startsWith("HTTP/1.1");
    if (r.statusLine.startsWith("HTTP/")) {
        const int sp = r.statusLine.indexOf(' ');
        if (sp > 0) r.httpCode = r.statusLine.substring(sp + 1, sp + 4).toInt();
    }
    if (r.httpCode >= 200 && r.httpCode < 300) gHadApiSuccess = true;

    int contentLength = -1;
    bool chunked = false;
    bool connectionClose = false;
    bool connectionKeepAlive = false;
    bool headerFailed = false;
    while (true) {
        String line;
        if (!readLine(client, line, t0 + timeoutMs)) {
            headerFailed = true;
            break;
        }
        if (line == "\r" || line.isEmpty()) break;
        String header = line;
        header.toLowerCase();
        if (header.startsWith("content-length:")) {
            contentLength = header.substring(15).toInt();
        } else if (header.startsWith("transfer-encoding:")) {
            if (header.indexOf("chunked") >= 0) chunked = true;
        } else if (header.startsWith("connection:")) {
            if (header.indexOf("close") >= 0) connectionClose = true;
            if (header.indexOf("keep-alive") >= 0) connectionKeepAlive = true;
        } else if (header.startsWith("ratelimit-reset:")) {
            r.rateLimitReset = static_cast<uint32_t>(header.substring(16).toInt());
        } else if (header.startsWith("ratelimit-remaining:")) {
            r.rateLimitRemaining = header.substring(20).toInt();
        }
    }
    if (headerFailed) {
        r.truncated = true;
        r.ms = millis() - t0;
        Serial.printf("[http] %s header read failed\n", path.c_str());
        a.retryable = method == "GET";
        client.stop();
        return a;
    }

    const size_t cap = std::min(maxBodyBytes, bufferSize);
    const bool noBody = r.httpCode / 100 == 1 || r.httpCode == 204 || r.httpCode == 304;
    size_t bodyLen = 0;
    bool bodyComplete = false;
    bool chunkedComplete = false;
    bool truncated = false;
    bool closeDelimited = false;

    if (r.httpCode == 0 || noBody) {
        bodyComplete = r.httpCode != 0;
    } else if (chunked) {
        size_t rawLen = 0;
        size_t decoded = 0;
        ChunkedStatus chunkStatus = ChunkedStatus::Incomplete;
        uint32_t lastData = millis();
        while (rawLen < bufferSize) {
            const int avail = client.available();
            int n = 0;
            if (avail > 0) {
                const size_t take =
                    std::min<size_t>(static_cast<size_t>(avail), bufferSize - rawLen);
                n = client.read(reinterpret_cast<uint8_t*>(buffer) + rawLen, take);
                if (n > 0) {
                    rawLen += static_cast<size_t>(n);
                    lastData = millis();
                }
            }
            const bool complete =
                chunkedDecodeInPlace(buffer, rawLen, cap, &decoded, &chunkStatus);
            if (complete) {
                chunkedComplete = true;
                bodyLen = decoded;
                break;
            }
            if (chunkStatus == ChunkedStatus::Malformed ||
                chunkStatus == ChunkedStatus::TooLarge) {
                break;
            }
            if (!client.connected() && !client.available()) break;
            if (millis() - lastData > kBodyIdleTimeoutMs) break;
            if (static_cast<int32_t>(millis() - t0) > static_cast<int32_t>(timeoutMs)) break;
            if (avail <= 0) delay(2);
            else if (n <= 0) delay(1);
        }
        if (!chunkedComplete) {
            truncated = true;
            bodyLen = 0;
            const char* why = chunkStatus == ChunkedStatus::Malformed
                                  ? "malformed"
                                  : chunkStatus == ChunkedStatus::TooLarge ? "too large"
                                                                           : "incomplete";
            Serial.printf("[http] %s chunked body %s (%u wire bytes)\n", path.c_str(), why,
                          static_cast<unsigned>(rawLen));
        }
        bodyComplete = chunkedComplete;
    } else if (contentLength >= 0) {
        size_t want = static_cast<size_t>(contentLength);
        if (want > cap) {
            want = cap;
            truncated = true;
        }
        uint32_t lastData = millis();
        while (bodyLen < want) {
            const int avail = client.available();
            if (avail > 0) {
                const size_t take =
                    std::min<size_t>(static_cast<size_t>(avail), want - bodyLen);
                const int n = client.read(reinterpret_cast<uint8_t*>(buffer) + bodyLen, take);
                if (n > 0) {
                    bodyLen += static_cast<size_t>(n);
                    lastData = millis();
                    continue;
                }
            }
            if (!client.connected() && !client.available()) break;
            if (millis() - lastData > kBodyIdleTimeoutMs) break;
            if (static_cast<int32_t>(millis() - t0) > static_cast<int32_t>(timeoutMs)) break;
            delay(2);
        }
        if (bodyLen < want) truncated = true;
        bodyComplete = !truncated && bodyLen == static_cast<size_t>(contentLength);
    } else {
        // Close-delimited response: the only safe read is until the peer closes.
        closeDelimited = true;
        while (static_cast<int32_t>(millis() - t0) < static_cast<int32_t>(timeoutMs)) {
            if (client.available() > 0) {
                if (bodyLen >= cap) {
                    truncated = true;
                    break;
                }
                const size_t take = std::min<size_t>(
                    static_cast<size_t>(client.available()), cap - bodyLen);
                const int n = client.read(reinterpret_cast<uint8_t*>(buffer) + bodyLen, take);
                if (n <= 0) break;
                bodyLen += static_cast<size_t>(n);
                continue;
            }
            if (!client.connected()) {
                bodyComplete = true;
                break;
            }
            delay(2);
        }
    }

    r.body = std::string_view(buffer, bodyLen);
    r.bytes = bodyLen;
    r.truncated = truncated;

    // Keep the socket only for a complete, framed response that the server did
    // not ask to close. 429 closes so the retry gate starts from a fresh socket.
    a.keepAlive = session && r.httpCode > 0 && r.httpCode != 429 && !connectionClose &&
                  !truncated && (http11 || connectionKeepAlive);
    if (a.keepAlive) {
        if (noBody)
            a.keepAlive = bodyComplete;
        else if (chunked)
            a.keepAlive = chunkedComplete;
        else if (contentLength >= 0)
            a.keepAlive = bodyComplete;
        else
            a.keepAlive = false;
    }
    if (closeDelimited) a.keepAlive = false;

    r.ms = millis() - t0;
    Serial.printf("[http] %s -> %d, %u body bytes, %u ms, cl=%d%s%s%s\n", path.c_str(),
                  r.httpCode, static_cast<unsigned>(r.bytes), static_cast<unsigned>(r.ms),
                  contentLength, chunked ? " chunked" : "",
                  r.truncated ? " (truncated)" : "", a.keepAlive ? " keep" : "");
    if (!a.keepAlive) client.stop();
    return a;
}
}  // namespace

void wkSetRequestHooks(WkMemHook before, WkMemHook after) {
    gBeforeRequest = before;
    gAfterRequest = after;
}

bool wkSessionBegin() {
    if (gSessionDepth == 0) {
        gSessionRequests = 0;
        gSessionClient.stop();
    }
    ++gSessionDepth;
    return true;
}

void wkSessionEnd() {
    if (gSessionDepth == 0) return;
    --gSessionDepth;
    if (gSessionDepth > 0) return;
    gSessionClient.stop();
    const uint32_t requests = gSessionRequests;
    gSessionRequests = 0;
    if (requests) Serial.printf("[http] session close after %u requests\n",
                                static_cast<unsigned>(requests));
    // Restore the canvas once, after the whole sequence, not after each request.
    if (requests && gAfterRequest) gAfterRequest();
}

bool wkSessionActive() { return gSessionDepth > 0; }

uint32_t wkRateLimitWaitSeconds() {
    if (!gRateLimitGateActive) return 0;
    const int32_t left = static_cast<int32_t>(gRateLimitDeadlineMs - millis());
    if (left <= 0) {
        gRateLimitGateActive = false;
        return 0;
    }
    return (static_cast<uint32_t>(left) + 999u) / 1000u;
}

bool wkAuthBlocked() { return gAuthBlocked; }

void wkClearAuthBlock() {
    if (gAuthBlocked) Serial.println("[http] auth gate cleared");
    gAuthBlocked = false;
    gAuthHttpCode = 0;
}

bool wkHadApiSuccess() { return gHadApiSuccess; }

namespace {
WkResponse request(const String& method, const String& path, const String& token,
                   const String& body, size_t maxBodyBytes, uint32_t timeoutMs,
                   char* externalBuffer = nullptr, size_t externalCapacity = 0) {
    const uint32_t startMs = millis();
    if (gAuthBlocked) {
        WkResponse r;
        r.connected = true;
        r.httpCode = gAuthHttpCode ? gAuthHttpCode : 401;
        Serial.printf("[http] %s blocked by auth (%d)\n", path.c_str(), r.httpCode);
        return r;
    }
    if (gRateLimitGateActive) {
        const int32_t left = static_cast<int32_t>(gRateLimitDeadlineMs - startMs);
        if (left > 0) {
            WkResponse r;
            r.connected = true;
            r.httpCode = 429;
            r.rateLimitReset = gRateLimitResetEpoch;
            r.retryAfter = (static_cast<uint32_t>(left) + 999u) / 1000u;
            Serial.printf("[http] %s blocked by rate limit, %u s left\n", path.c_str(),
                          static_cast<unsigned>(r.retryAfter));
            return r;
        }
        gRateLimitGateActive = false;
    }

    const bool session = gSessionDepth > 0;
    RequestHooks hooks(!session);
    if (session) {
        ++gSessionRequests;
        // The hook releases the canvas; it is idempotent when already released.
        if (gBeforeRequest) gBeforeRequest();
    }
    logHeap("before");

    const size_t bufferSize =
        externalBuffer && externalCapacity ? externalCapacity : sizeof gBodyBuffer;
    char* const buffer = externalBuffer && externalCapacity ? externalBuffer : gBodyBuffer;

    WkResponse r;
    bool retried = false;
    for (;;) {
        WiFiClientSecure& client = session ? gSessionClient : gLocalClient;
        Attempt a = performRequest(client, method, path, token, body, maxBodyBytes, timeoutMs,
                                   buffer, bufferSize);
        r = std::move(a.r);
        if (!session || !a.retryable || retried) break;
        // A reused connection may have been closed by the server between
        // requests. One fresh connection is allowed before the error is real.
        retried = true;
        Serial.printf("[http] %s retrying on a new connection\n", path.c_str());
        client.stop();
        delay(50);
    }

    r.ms = millis() - startMs;
    if (r.httpCode == 401) {
        gAuthBlocked = true;
        gAuthHttpCode = r.httpCode;
        Serial.printf("[http] 401 token rejected; blocking requests until the token changes\n");
    }
    if (r.httpCode == 429) {
        const time_t now = time(nullptr);
        const uint32_t nowEpoch = now > 0 ? static_cast<uint32_t>(now) : 0;
        const bool trusted = nowEpoch >= kTrustedClockEpoch;
        const uint32_t wait = rateLimitWaitSeconds(r.rateLimitReset, nowEpoch, trusted);
        r.retryAfter = wait;
        gRateLimitResetEpoch = r.rateLimitReset;
        gRateLimitDeadlineMs = millis() + wait * 1000u;
        gRateLimitGateActive = true;
        Serial.printf("[http] 429 rate limited: reset %u, remaining %d, wait %u s, clock %s\n",
                      static_cast<unsigned>(r.rateLimitReset),
                      static_cast<int>(r.rateLimitRemaining), static_cast<unsigned>(wait),
                      trusted ? "ok" : "untrusted");
    }
    logHeap("after");
    return r;
}
}  // namespace

WkResponse wkGet(const String& path, const String& token, size_t maxBodyBytes,
                 uint32_t timeoutMs) {
    return request("GET", path, token, String(), maxBodyBytes, timeoutMs);
}

WkResponse wkGetToBuffer(const String& path, const String& token, char* body,
                         size_t capacity, size_t maxBodyBytes, uint32_t timeoutMs) {
    if (!body || capacity == 0)
        return request("GET", path, token, String(), maxBodyBytes, timeoutMs);
    return request("GET", path, token, String(), maxBodyBytes, timeoutMs, body, capacity);
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

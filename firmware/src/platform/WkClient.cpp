#include "platform/WkClient.h"

#include <WiFiClientSecure.h>

#include <algorithm>
#include <esp_heap_caps.h>
#include <time.h>

#include "platform/RootCerts.h"
#include "Version.h"
#include "wk/DevOverrides.h"
#include "wk/HeaderLine.h"
#include "wk/HttpChunked.h"
#include "wk/HttpRetry.h"
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

#ifdef KANIKEY_DEV
// Dev-only request overrides (`cap`, `devhost`). RAM only: a reboot clears them.
uint32_t gDevBodyCap = 0;
bool gDevHostOn = false;
char gDevHost[64] = "";
uint16_t gDevPort = 0;
#endif

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

// Reads one CRLF-terminated line into a fixed buffer (MEM-17; it was a String,
// one heap realloc per 16 characters). Returns false on EOF or the deadline
// before any byte arrives, as before. After the first byte the line is read up
// to '\n', or until the socket closes or `gapMs` passes with no byte: the limit
// readStringUntil() had through Stream::setTimeout(). Bytes past the buffer are
// read and dropped, so the next line still starts in the right place.
bool readLine(WiFiClientSecure& client, kk::HeaderLine& line, uint32_t deadline,
              uint32_t gapMs) {
    line.clear();
    while (static_cast<int32_t>(deadline - millis()) > 0) {
        if (client.available() > 0) {
            uint32_t gapStart = millis();
            while (true) {
                if (client.available() > 0) {
                    const int c = client.read();
                    if (c < 0 || c == '\n') break;
                    line.push(static_cast<char>(c));
                    gapStart = millis();
                    continue;
                }
                if (!client.connected() || millis() - gapStart > gapMs) break;
                delay(1);
            }
            return true;
        }
        if (!client.connected()) return false;
        delay(1);
    }
    return false;
}

// A reused static client must never close an fd it does not own. The 2.x
// stop() left the socket at 0, so a second stop could close fd 0 (now another
// socket). 3.x stop_ssl_socket() closes the fd and sets it to -1 itself; the
// socket() accessor still forces the sentinel as a cheap guard.
void closeClient(WiFiClientSecure& client) {
    client.stop();
    client.socket();  // sets sslclient->socket = -1
}

struct Endpoint {
    const char* host;
    uint16_t port;
    bool insecure;  // dev `devhost` only: certificate checks off
};

// The WaniKani API, or the dev host while `devhost` is set. Release builds
// have no way to leave the pinned root CA.
Endpoint activeEndpoint() {
#ifdef KANIKEY_DEV
    if (gDevHostOn) return Endpoint{gDevHost, gDevPort, true};
#endif
    return Endpoint{"api.wanikani.com", 443, false};
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
        closeClient(client);
        const Endpoint endpoint = activeEndpoint();
        if (endpoint.insecure)
            client.setInsecure();
        else
            client.setCACert(tgc::kRootCaPem);
        client.setHandshakeTimeout(kHandshakeTimeoutS);
        // 3.x: setConnectionTimeout() takes milliseconds (the 2.x setTimeout(seconds) is gone).
        client.setConnectionTimeout(timeoutMs);
#ifdef KANIKEY_DEV
        if (endpoint.insecure)
            Serial.printf("[http] dev host %s:%u, certificate check off\n", endpoint.host,
                          static_cast<unsigned>(endpoint.port));
#endif
        if (!client.connect(endpoint.host, endpoint.port)) {
            char err[80] = "";
            r.tlsError = client.lastError(err, sizeof err);
            r.ms = millis() - t0;
            Serial.printf("[http] connect failed after %u ms, error %d %s\n",
                          static_cast<unsigned>(r.ms), r.tlsError, err);
            closeClient(client);
            a.retryable = true;
            return a;
        }
        Serial.printf("[http] %s connect %u ms\n", path.c_str(),
                      static_cast<unsigned>(millis() - t0));
    }
    r.connected = true;
    // setConnectionTimeout() (ms) sets the socket send/receive timeouts; the
    // Stream::setTimeout() below is the readStringUntil() limit, which would
    // otherwise stay at the 1 s default. On 3.x there is no setTimeout() member
    // to hide Stream's, so both calls are explicit.
    client.setConnectionTimeout(timeoutMs);
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
        closeClient(client);
        return a;
    }

    // Header lines are read with the same limits as before (the first-byte
    // deadline, and a per-byte gap of the request timeout up to 5 s).
    const uint32_t lineGapMs = std::min<uint32_t>(timeoutMs, 5000);
    kk::HeaderLine statusLine;
    if (!readLine(client, statusLine, t0 + timeoutMs, lineGapMs)) {
        r.ms = millis() - t0;
        Serial.printf("[http] %s no status line\n", path.c_str());
        a.retryable = method == "GET";
        closeClient(client);
        return a;
    }
    statusLine.trim();
    r.statusLine = statusLine.c_str();
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
        kk::HeaderLine line;
        if (!readLine(client, line, t0 + timeoutMs, lineGapMs)) {
            headerFailed = true;
            break;
        }
        if (line.blank()) break;
        line.toLower();
        if (line.hasPrefix("content-length:")) {
            contentLength = static_cast<int>(line.valueFrom(15));
        } else if (line.hasPrefix("transfer-encoding:")) {
            if (line.contains("chunked")) chunked = true;
        } else if (line.hasPrefix("connection:")) {
            if (line.contains("close")) connectionClose = true;
            if (line.contains("keep-alive")) connectionKeepAlive = true;
        } else if (line.hasPrefix("ratelimit-reset:")) {
            r.rateLimitReset = static_cast<uint32_t>(line.valueFrom(16));
        } else if (line.hasPrefix("ratelimit-remaining:")) {
            r.rateLimitRemaining = static_cast<int32_t>(line.valueFrom(20));
        }
    }
    if (headerFailed) {
        r.truncated = true;
        r.ms = millis() - t0;
        Serial.printf("[http] %s header read failed\n", path.c_str());
        a.retryable = method == "GET";
        closeClient(client);
        return a;
    }

#ifdef KANIKEY_DEV
    const size_t cap = effectiveBodyCap(maxBodyBytes, bufferSize, gDevBodyCap);
    if (gDevBodyCap)
        Serial.printf("[http] dev cap %u, %s effective %u\n", static_cast<unsigned>(gDevBodyCap),
                      path.c_str(), static_cast<unsigned>(cap));
#else
    const size_t cap = std::min(maxBodyBytes, bufferSize);
#endif
    const bool noBody = r.httpCode / 100 == 1 || r.httpCode == 204 || r.httpCode == 304;
    size_t bodyLen = 0;
    bool bodyComplete = false;
    bool chunkedComplete = false;
    bool truncated = false;
    bool closeDelimited = false;
    bool capOverflow = false;

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
            // Overflow: the decoded body passed the cap, or the wire bytes
            // filled the buffer while the stream was still open. A malformed
            // stream or a stalled short read is not an overflow.
            capOverflow = chunkStatus == ChunkedStatus::TooLarge ||
                          (chunkStatus == ChunkedStatus::Incomplete && rawLen >= bufferSize);
            const char* why = chunkStatus == ChunkedStatus::Malformed
                                  ? "malformed"
                                  : chunkStatus == ChunkedStatus::TooLarge ? "too large"
                                                                           : "incomplete";
            Serial.printf("[http] %s chunked body %s (%u wire bytes%s)\n", path.c_str(), why,
                          static_cast<unsigned>(rawLen), capOverflow ? ", cap overflow" : "");
        }
        bodyComplete = chunkedComplete;
    } else if (contentLength >= 0) {
        size_t want = static_cast<size_t>(contentLength);
        const bool declaredOverCap = want > cap;
        if (declaredOverCap) {
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
        // The declared length is over the cap and the cap was reached: overflow.
        // A short read below the cap is not, and keeps its one API-04 retry.
        capOverflow = isCapOverflow(bodyLen, cap, declaredOverCap);
        bodyComplete = !truncated && bodyLen == static_cast<size_t>(contentLength);
    } else {
        // Close-delimited response: the only safe read is until the peer closes.
        closeDelimited = true;
        while (static_cast<int32_t>(millis() - t0) < static_cast<int32_t>(timeoutMs)) {
            if (client.available() > 0) {
                if (bodyLen >= cap) {
                    // More bytes are still arriving past the cap: overflow.
                    truncated = true;
                    capOverflow = isCapOverflow(bodyLen, cap, true);
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
    r.capOverflow = capOverflow;

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
    Serial.printf("[http] %s -> %d, %u body bytes, %u ms, cl=%d%s%s%s%s\n", path.c_str(),
                  r.httpCode, static_cast<unsigned>(r.bytes), static_cast<unsigned>(r.ms),
                  contentLength, chunked ? " chunked" : "",
                  r.truncated ? " (truncated)" : "", capOverflow ? " (cap overflow)" : "",
                  a.keepAlive ? " keep" : "");
    if (!a.keepAlive) closeClient(client);
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
        closeClient(gSessionClient);
    }
    ++gSessionDepth;
    return true;
}

void wkSessionEnd() {
    if (gSessionDepth == 0) return;
    --gSessionDepth;
    if (gSessionDepth > 0) return;
    closeClient(gSessionClient);
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

#ifdef KANIKEY_DEV
void wkDevSetBodyCap(uint32_t bytes) { gDevBodyCap = bytes; }

uint32_t wkDevBodyCap() { return gDevBodyCap; }

bool wkDevSetHost(std::string_view host, uint32_t port) {
    if (!devHostAllowed(host, port)) return false;
    host.copy(gDevHost, host.size());
    gDevHost[host.size()] = '\0';
    gDevPort = static_cast<uint16_t>(port);
    gDevHostOn = true;
    // Sockets opened for the old endpoint must not carry over to the new one.
    if (gSessionDepth == 0) {
        closeClient(gSessionClient);
        closeClient(gLocalClient);
    }
    return true;
}

void wkDevClearHost() {
    gDevHostOn = false;
    gDevHost[0] = '\0';
    gDevPort = 0;
    if (gSessionDepth == 0) {
        closeClient(gSessionClient);
        closeClient(gLocalClient);
    }
}

bool wkDevHostActive() { return gDevHostOn; }

const char* wkDevHostName() { return gDevHost; }

uint16_t wkDevHostPort() { return gDevPort; }
#endif

namespace {
WkResponse request(const String& method, const String& path, const String& token,
                   const String& body, size_t maxBodyBytes, uint32_t timeoutMs,
                   char* externalBuffer = nullptr, size_t externalCapacity = 0) {
    const uint32_t startMs = millis();
#if defined(KANIKEY_DEV) && defined(KANIKEY_DEV_FAIL_API)
    // FIX-20 test image: every request fails before a socket is opened, so the
    // image never reports an API success and only the 10-minute window can
    // mark it valid.
    {
        WkResponse r;
        Serial.printf("[http] %s dev fail-api: not connecting\n", path.c_str());
        return r;
    }
#endif
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
        closeClient(client);
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

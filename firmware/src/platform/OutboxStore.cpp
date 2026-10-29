#include "platform/OutboxStore.h"

#include <LittleFS.h>

#include <cstring>

#include "wk/Crc32.h"

namespace kk {

namespace {
constexpr const char* kPath = "/state/outbox.bin";
constexpr const char* kTmp = "/state/outbox.tmp";
constexpr const char* kBak = "/state/outbox.bak";
constexpr uint16_t kVersion = 1;
constexpr size_t kHeaderBytes = 16;
constexpr size_t kRecordBytes = 24;

void put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}
void put32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}
uint16_t get16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t get32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool readFile(const char* path, std::vector<uint8_t>* out) {
    if (!LittleFS.exists(path)) return false;
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    out->clear();
    out->reserve(static_cast<size_t>(f.size()));
    while (f.available()) {
        const int c = f.read();
        if (c < 0) break;
        out->push_back(static_cast<uint8_t>(c));
    }
    f.close();
    return true;
}

bool parse(const std::vector<uint8_t>& bytes, std::vector<OutboxItem>* out) {
    if (bytes.size() < kHeaderBytes) return false;
    if (std::memcmp(bytes.data(), "KKOB", 4) != 0) return false;
    if (get16(bytes.data() + 4) != kVersion) return false;
    const uint16_t count = get16(bytes.data() + 6);
    if (count > kOutboxMax) return false;
    if (bytes.size() != kHeaderBytes + static_cast<size_t>(count) * kRecordBytes) return false;
    const uint32_t want = get32(bytes.data() + 8);
    const uint32_t got = crc32(bytes.data() + kHeaderBytes, bytes.size() - kHeaderBytes);
    if (want != got) return false;
    out->clear();
    out->reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t* p = bytes.data() + kHeaderBytes + static_cast<size_t>(i) * kRecordBytes;
        OutboxItem item;
        item.seq = get32(p);
        item.assignmentId = get32(p + 4);
        item.subjectId = get32(p + 8);
        item.incorrectMeaning = get16(p + 12);
        item.incorrectReading = get16(p + 14);
        item.createdAt = get32(p + 16);
        item.state = static_cast<OutboxState>(p[20]);
        item.attempts = p[21];
        item.httpCode = get16(p + 22);
        out->push_back(item);
    }
    return true;
}

bool writeAll(File& f, const uint8_t* data, size_t n) {
    return f.write(data, n) == n;
}
}  // namespace

bool outboxLoad(std::vector<OutboxItem>& items) {
    std::vector<uint8_t> bytes;
    if (!readFile(kPath, &bytes) || !parse(bytes, &items)) {
        bytes.clear();
        if (readFile(kBak, &bytes) && parse(bytes, &items)) {
            Serial.println("[outbox] recovered from .bak");
        } else {
            items.clear();
            return false;
        }
    }
    Serial.printf("[outbox] loaded %u pending\n", static_cast<unsigned>(items.size()));
    return true;
}

bool outboxSave(const std::vector<OutboxItem>& items) {
    if (items.size() > kOutboxMax) return false;
    LittleFS.mkdir("/state");
    const size_t n = items.size();
    std::vector<uint8_t> bytes(kHeaderBytes + n * kRecordBytes);
    std::memcpy(bytes.data(), "KKOB", 4);
    put16(bytes.data() + 4, kVersion);
    put16(bytes.data() + 6, static_cast<uint16_t>(n));
    for (size_t i = 0; i < n; ++i) {
        const OutboxItem& it = items[i];
        uint8_t* p = bytes.data() + kHeaderBytes + i * kRecordBytes;
        put32(p, it.seq);
        put32(p + 4, it.assignmentId);
        put32(p + 8, it.subjectId);
        put16(p + 12, it.incorrectMeaning);
        put16(p + 14, it.incorrectReading);
        put32(p + 16, it.createdAt);
        p[20] = static_cast<uint8_t>(it.state);
        p[21] = it.attempts;
        put16(p + 22, it.httpCode);  // bytes 22-23 were reserved in v1
    }
    put32(bytes.data() + 8, crc32(bytes.data() + kHeaderBytes, n * kRecordBytes));

    File f = LittleFS.open(kTmp, "w");
    if (!f) return false;
    const bool ok = writeAll(f, bytes.data(), bytes.size());
    f.close();
    if (!ok) {
        LittleFS.remove(kTmp);
        return false;
    }
    if (LittleFS.exists(kBak)) LittleFS.remove(kBak);
    if (LittleFS.exists(kPath)) LittleFS.rename(kPath, kBak);
    if (!LittleFS.rename(kTmp, kPath)) {
        LittleFS.remove(kPath);
        if (!LittleFS.rename(kTmp, kPath)) {
            LittleFS.remove(kTmp);
            return false;
        }
    }
    return true;
}

}  // namespace kk

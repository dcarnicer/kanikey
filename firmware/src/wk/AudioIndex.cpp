#include "wk/AudioIndex.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "wk/Crc32.h"

namespace kk {

namespace {
constexpr uint16_t kVersion = 1;
constexpr size_t kHeaderBytes = 16;
constexpr size_t kEntryBytes = 16;

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
}  // namespace

bool audioFileName(const AudioEntry& entry, char* out, size_t n) {
    if (!out || n == 0 || entry.subjectId == 0) return false;
    const char* ext = entry.format == AudioFormat::Ogg ? "ogg" : "mp3";
    const int written = snprintf(out, n, "%lu-%u.%s",
                                 static_cast<unsigned long>(entry.subjectId),
                                 static_cast<unsigned>(entry.voice), ext);
    return written > 0 && static_cast<size_t>(written) < n;
}

AudioFormat audioFormatFromContentType(const char* contentType) {
    if (contentType && std::strstr(contentType, "ogg")) return AudioFormat::Ogg;
    return AudioFormat::Mp3;
}

bool audioChooseEntry(const std::vector<AudioEntry>& entries, uint32_t subjectId, uint8_t voice,
                      AudioEntry* out) {
    if (!out) return false;
    for (const AudioEntry& entry : entries)
        if (entry.subjectId == subjectId && entry.voice == voice) {
            *out = entry;
            return true;
        }
    for (const AudioEntry& entry : entries)
        if (entry.subjectId == subjectId) {
            *out = entry;
            return true;
        }
    return false;
}

std::vector<AudioEntry> audioEvict(const std::vector<AudioEntry>& entries, uint64_t bytes,
                                   uint64_t addBytes) {
    std::vector<AudioEntry> order = entries;
    std::sort(order.begin(), order.end(), [](const AudioEntry& a, const AudioEntry& b) {
        if (a.usedAt != b.usedAt) return a.usedAt < b.usedAt;
        return a.subjectId < b.subjectId;
    });
    std::vector<AudioEntry> out;
    for (const AudioEntry& entry : order) {
        if (bytes + addBytes <= kAudioCacheMaxBytes) break;
        out.push_back(entry);
        bytes = bytes > entry.bytes ? bytes - entry.bytes : 0;
    }
    return out;
}

bool encodeAudioIndex(const std::vector<AudioEntry>& entries, uint32_t accountKey,
                      uint32_t savedAt, std::vector<uint8_t>* out) {
    if (!out || entries.size() > kAudioIndexMaxEntries) return false;
    const size_t payloadBytes = kHeaderBytes + entries.size() * kEntryBytes;
    std::vector<uint8_t> bytes(payloadBytes + 4, 0);
    std::memcpy(bytes.data(), "KKAU", 4);
    put16(bytes.data() + 4, kVersion);
    put16(bytes.data() + 6, static_cast<uint16_t>(entries.size()));
    put32(bytes.data() + 8, accountKey);
    put32(bytes.data() + 12, savedAt);
    for (size_t i = 0; i < entries.size(); ++i) {
        const AudioEntry& entry = entries[i];
        uint8_t* p = bytes.data() + kHeaderBytes + i * kEntryBytes;
        put32(p, entry.subjectId);
        put32(p + 4, entry.bytes);
        put32(p + 8, entry.usedAt);
        p[12] = entry.voice;
        p[13] = static_cast<uint8_t>(entry.format);
    }
    put32(bytes.data() + payloadBytes, crc32(bytes.data(), payloadBytes));
    *out = std::move(bytes);
    return true;
}

bool decodeAudioIndex(const uint8_t* data, size_t size, uint32_t accountKey,
                      std::vector<AudioEntry>* out) {
    if (!data || !out || size < kHeaderBytes + 4) return false;
    if (std::memcmp(data, "KKAU", 4) != 0) return false;
    if (get16(data + 4) != kVersion) return false;
    const uint16_t count = get16(data + 6);
    if (count > kAudioIndexMaxEntries) return false;
    const size_t payloadBytes = kHeaderBytes + static_cast<size_t>(count) * kEntryBytes;
    if (size != payloadBytes + 4) return false;
    if (get32(data + 8) != accountKey) return false;
    if (get32(data + payloadBytes) != crc32(data, payloadBytes)) return false;
    out->clear();
    out->reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t* p = data + kHeaderBytes + static_cast<size_t>(i) * kEntryBytes;
        AudioEntry entry;
        entry.subjectId = get32(p);
        entry.bytes = get32(p + 4);
        entry.usedAt = get32(p + 8);
        entry.voice = p[12] > 1 ? 0 : p[12];
        entry.format = p[13] == 1 ? AudioFormat::Ogg : AudioFormat::Mp3;
        if (entry.subjectId) out->push_back(entry);
    }
    return true;
}

}  // namespace kk

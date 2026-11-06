#include "wk/Practice.h"

#include <algorithm>
#include <cstring>

#include "wk/Crc32.h"

namespace kk {

namespace {
constexpr uint16_t kVersion = 1;
constexpr size_t kHeaderBytes = 16;
constexpr size_t kEntryBytes = 24;

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
uint32_t windowSeconds(int mode) {
    if (mode == 1) return 24u * 3600u;
    if (mode == 2) return 3u * 86400u;
    if (mode == 3) return 7u * 86400u;
    return 0;
}
}  // namespace

std::vector<Assignment> selectRecentLessons(const std::vector<Assignment>& assignments, int mode,
                                            uint32_t now, size_t limit) {
    std::vector<Assignment> out;
    const uint32_t window = windowSeconds(mode);
    for (const Assignment& a : assignments) {
        if (a.hidden || a.srsStage == 0 || a.srsStage > 4) continue;
        if (window) {
            if (a.startedAt == 0 || a.startedAt > now || now - a.startedAt > window) continue;
        }
        out.push_back(a);
    }
    std::sort(out.begin(), out.end(), [](const Assignment& a, const Assignment& b) {
        if (a.startedAt != b.startedAt) return a.startedAt > b.startedAt;
        return a.subjectId < b.subjectId;
    });
    if (limit && out.size() > limit) out.resize(limit);
    return out;
}

std::vector<Assignment> selectCurrentLevel(const std::vector<Assignment>& assignments,
                                           uint8_t level, size_t limit) {
    std::vector<Assignment> out;
    for (const Assignment& a : assignments) {
        if (a.hidden || a.srsStage < 1 || a.srsStage > 4) continue;
        if (a.type != SubjectType::Kanji || a.level != level) continue;
        if (a.lessonPosition == 0) continue;
        out.push_back(a);
    }
    std::sort(out.begin(), out.end(), [](const Assignment& a, const Assignment& b) {
        if (a.lessonPosition != b.lessonPosition) return a.lessonPosition < b.lessonPosition;
        return a.subjectId < b.subjectId;
    });
    if (limit && out.size() > limit) out.resize(limit);
    return out;
}

bool encodePracticePool(const std::vector<Assignment>& assignments, uint32_t accountKey,
                        uint32_t savedAt, std::vector<uint8_t>* out) {
    if (!out || assignments.size() > kPracticePoolMax) return false;
    const size_t payloadBytes = kHeaderBytes + assignments.size() * kEntryBytes;
    std::vector<uint8_t> bytes(payloadBytes + 4, 0);
    std::memcpy(bytes.data(), "KKAP", 4);
    put16(bytes.data() + 4, kVersion);
    put16(bytes.data() + 6, static_cast<uint16_t>(assignments.size()));
    put32(bytes.data() + 8, accountKey);
    put32(bytes.data() + 12, savedAt);
    for (size_t i = 0; i < assignments.size(); ++i) {
        const Assignment& a = assignments[i];
        uint8_t* p = bytes.data() + kHeaderBytes + i * kEntryBytes;
        put32(p, a.id);
        put32(p + 4, a.subjectId);
        p[8] = static_cast<uint8_t>(a.type);
        p[9] = a.srsStage;
        put16(p + 10, a.lessonPosition);
        put32(p + 12, a.availableAt);
        put32(p + 16, a.startedAt);
        p[20] = a.hidden ? 1 : 0;
        p[21] = a.level;
    }
    put32(bytes.data() + payloadBytes, crc32(bytes.data(), payloadBytes));
    *out = std::move(bytes);
    return true;
}

bool decodePracticePool(const uint8_t* data, size_t size, uint32_t accountKey,
                        std::vector<Assignment>* out) {
    if (!data || !out || size < kHeaderBytes + 4) return false;
    if (std::memcmp(data, "KKAP", 4) != 0) return false;
    if (get16(data + 4) != kVersion) return false;
    const uint16_t count = get16(data + 6);
    if (count > kPracticePoolMax) return false;
    const size_t payloadBytes = kHeaderBytes + static_cast<size_t>(count) * kEntryBytes;
    if (size != payloadBytes + 4) return false;
    if (get32(data + 8) != accountKey) return false;
    if (get32(data + payloadBytes) != crc32(data, payloadBytes)) return false;
    out->clear();
    out->reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t* p = data + kHeaderBytes + static_cast<size_t>(i) * kEntryBytes;
        Assignment a;
        a.id = get32(p);
        a.subjectId = get32(p + 4);
        const uint8_t type = p[8];
        if (type > static_cast<uint8_t>(SubjectType::KanaVocabulary)) continue;
        a.type = static_cast<SubjectType>(type);
        a.srsStage = p[9];
        a.lessonPosition = get16(p + 10);
        a.availableAt = get32(p + 12);
        a.startedAt = get32(p + 16);
        a.hidden = p[20] != 0;
        a.level = p[21];
        if (a.subjectId) out->push_back(a);
    }
    return true;
}

}  // namespace kk

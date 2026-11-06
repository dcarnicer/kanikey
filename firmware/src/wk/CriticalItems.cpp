#include "wk/CriticalItems.h"

#include <algorithm>
#include <cstring>

#include "wk/Crc32.h"

namespace kk {

namespace {
constexpr uint16_t kVersion = 1;
constexpr size_t kHeaderBytes = 16;
constexpr size_t kEntryBytes = 8;
constexpr size_t kMaxItems = 2000;

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

std::vector<CriticalItem> selectCriticalItems(const std::vector<ReviewStatistic>& statistics,
                                              const std::vector<Assignment>& assignments,
                                              uint8_t maxPercent, uint16_t minAnswers) {
    std::vector<CriticalItem> out;
    for (const ReviewStatistic& stat : statistics) {
        if (stat.hidden || stat.percentageCorrect >= maxPercent ||
            stat.totalAnswers() < minAnswers)
            continue;
        const Assignment* assignment = nullptr;
        for (const Assignment& candidate : assignments)
            if (candidate.subjectId == stat.subjectId) {
                assignment = &candidate;
                break;
            }
        if (!assignment || assignment->hidden || assignment->srsStage == 9) continue;
        out.push_back({stat.subjectId, stat.percentageCorrect});
    }
    std::sort(out.begin(), out.end(), [](const CriticalItem& a, const CriticalItem& b) {
        if (a.percentageCorrect != b.percentageCorrect)
            return a.percentageCorrect < b.percentageCorrect;
        return a.subjectId < b.subjectId;
    });
    return out;
}

bool criticalContains(const std::vector<CriticalItem>& items, uint32_t subjectId) {
    for (const CriticalItem& item : items)
        if (item.subjectId == subjectId) return true;
    return false;
}

bool encodeCriticalList(const std::vector<CriticalItem>& items, uint32_t accountKey,
                        uint32_t savedAt, std::vector<uint8_t>* out) {
    if (!out || items.size() > kMaxItems) return false;
    const size_t payloadBytes = kHeaderBytes + items.size() * kEntryBytes;
    std::vector<uint8_t> bytes(payloadBytes + 4, 0);
    std::memcpy(bytes.data(), "KKCR", 4);
    put16(bytes.data() + 4, kVersion);
    put16(bytes.data() + 6, static_cast<uint16_t>(items.size()));
    put32(bytes.data() + 8, accountKey);
    put32(bytes.data() + 12, savedAt);
    for (size_t i = 0; i < items.size(); ++i) {
        uint8_t* p = bytes.data() + kHeaderBytes + i * kEntryBytes;
        put32(p, items[i].subjectId);
        p[4] = items[i].percentageCorrect;
    }
    put32(bytes.data() + payloadBytes, crc32(bytes.data(), payloadBytes));
    *out = std::move(bytes);
    return true;
}

bool decodeCriticalList(const uint8_t* data, size_t size, uint32_t accountKey,
                        std::vector<CriticalItem>* out) {
    if (!data || !out || size < kHeaderBytes + 4) return false;
    if (std::memcmp(data, "KKCR", 4) != 0) return false;
    if (get16(data + 4) != kVersion) return false;
    const uint16_t count = get16(data + 6);
    if (count > kMaxItems) return false;
    const size_t payloadBytes = kHeaderBytes + static_cast<size_t>(count) * kEntryBytes;
    if (size != payloadBytes + 4) return false;
    if (get32(data + 8) != accountKey) return false;
    if (get32(data + payloadBytes) != crc32(data, payloadBytes)) return false;
    out->clear();
    out->reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t* p = data + kHeaderBytes + static_cast<size_t>(i) * kEntryBytes;
        CriticalItem item;
        item.subjectId = get32(p);
        item.percentageCorrect = p[4] > 100 ? 100 : p[4];
        if (item.subjectId) out->push_back(item);
    }
    return true;
}

}  // namespace kk

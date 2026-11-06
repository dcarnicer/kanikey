#pragma once
// Account-wide critical (leech) items: selection from /review_statistics plus
// the assignments that prove the item is still active. Pure C++.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "wk/ReviewStatistics.h"
#include "wk/Types.h"

namespace kk {

struct CriticalItem {
    uint32_t subjectId = 0;
    uint8_t percentageCorrect = 0;
};

// Keeps statistics strictly below `maxPercent` with at least `minAnswers`
// total answers, drops hidden/burned assignments and sorts worst first.
std::vector<CriticalItem> selectCriticalItems(const std::vector<ReviewStatistic>& statistics,
                                              const std::vector<Assignment>& assignments,
                                              uint8_t maxPercent, uint16_t minAnswers);

bool criticalContains(const std::vector<CriticalItem>& items, uint32_t subjectId);

// Persistent form: header (magic, version, count, account, savedAt, CRC) and
// 8-byte entries { subjectId, percentage, reserved[3] }.
bool encodeCriticalList(const std::vector<CriticalItem>& items, uint32_t accountKey,
                        uint32_t savedAt, std::vector<uint8_t>* out);
bool decodeCriticalList(const uint8_t* data, size_t size, uint32_t accountKey,
                        std::vector<CriticalItem>* out);

}  // namespace kk

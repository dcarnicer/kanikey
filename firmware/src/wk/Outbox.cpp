#include "wk/Outbox.h"

#include <algorithm>

namespace kk {

bool outboxShouldGiveUp(uint8_t attempts) { return attempts >= kOutboxMaxAttempts; }

uint32_t outboxAdd(std::vector<OutboxItem>& items, uint32_t assignmentId, uint32_t subjectId,
                   uint16_t incorrectMeaning, uint16_t incorrectReading, uint32_t createdAt) {
    if (items.size() >= kOutboxMax || assignmentId == 0) return 0;
    uint32_t next = 1;
    for (const OutboxItem& it : items) next = std::max(next, it.seq + 1);
    OutboxItem item;
    item.seq = next;
    item.assignmentId = assignmentId;
    item.subjectId = subjectId;
    item.incorrectMeaning = incorrectMeaning;
    item.incorrectReading = incorrectReading;
    item.createdAt = createdAt;
    items.push_back(item);
    return next;
}

bool outboxRemove(std::vector<OutboxItem>& items, uint32_t seq) {
    const auto it = std::find_if(items.begin(), items.end(),
                                 [seq](const OutboxItem& x) { return x.seq == seq; });
    if (it == items.end()) return false;
    items.erase(it);
    return true;
}

OutboxItem* outboxFind(std::vector<OutboxItem>& items, uint32_t seq) {
    for (OutboxItem& it : items)
        if (it.seq == seq) return &it;
    return nullptr;
}

const OutboxItem* outboxFind(const std::vector<OutboxItem>& items, uint32_t seq) {
    for (const OutboxItem& it : items)
        if (it.seq == seq) return &it;
    return nullptr;
}

size_t outboxPendingCount(const std::vector<OutboxItem>& items) {
    size_t n = 0;
    for (const OutboxItem& it : items)
        if (it.state != OutboxState::Rejected && it.state != OutboxState::Stuck) ++n;
    return n;
}

size_t outboxRejectedCount(const std::vector<OutboxItem>& items) {
    size_t n = 0;
    for (const OutboxItem& it : items)
        if (it.state == OutboxState::Rejected) ++n;
    return n;
}

size_t outboxStuckCount(const std::vector<OutboxItem>& items) {
    size_t n = 0;
    for (const OutboxItem& it : items)
        if (it.state == OutboxState::Stuck) ++n;
    return n;
}

size_t outboxAttentionCount(const std::vector<OutboxItem>& items) {
    size_t n = 0;
    for (const OutboxItem& it : items)
        if (it.state == OutboxState::Rejected || it.state == OutboxState::Stuck) ++n;
    return n;
}

}  // namespace kk

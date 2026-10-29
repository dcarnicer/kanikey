#pragma once
// Completed reviews waiting to reach WaniKani. Pure C++ model; persistence is
// in platform/OutboxStore.
//
// Pending   ready to POST
// Sending   a POST was started but its result is unknown (crash / timeout)
// Ambiguous the request may have reached WaniKani; reconcile before retrying
// Rejected  WaniKani refused it (4xx other than auth/rate limit)
// Stuck     the retry cap was reached; never sent again, user-dismissible
#include <cstdint>
#include <vector>

namespace kk {

enum class OutboxState : uint8_t {
    Pending = 0,
    Sending = 1,
    Ambiguous = 2,
    Rejected = 3,
    Stuck = 4,
};

struct OutboxItem {
    uint32_t seq = 0;
    uint32_t assignmentId = 0;
    uint32_t subjectId = 0;
    uint16_t incorrectMeaning = 0;
    uint16_t incorrectReading = 0;
    uint32_t createdAt = 0;  // Unix seconds; 0 = omit created_at
    OutboxState state = OutboxState::Pending;
    uint8_t attempts = 0;
    uint16_t httpCode = 0;  // rejection reason; 0 = unknown
};

constexpr size_t kOutboxMax = 128;
constexpr uint8_t kOutboxMaxAttempts = 50;

// True when an item that would be retried has used up its POST attempts.
bool outboxShouldGiveUp(uint8_t attempts);

// Adds a review, returns its new sequence (0 when the outbox is full or invalid).
uint32_t outboxAdd(std::vector<OutboxItem>& items, uint32_t assignmentId, uint32_t subjectId,
                   uint16_t incorrectMeaning, uint16_t incorrectReading, uint32_t createdAt);
bool outboxRemove(std::vector<OutboxItem>& items, uint32_t seq);
OutboxItem* outboxFind(std::vector<OutboxItem>& items, uint32_t seq);
const OutboxItem* outboxFind(const std::vector<OutboxItem>& items, uint32_t seq);
size_t outboxPendingCount(const std::vector<OutboxItem>& items);
size_t outboxRejectedCount(const std::vector<OutboxItem>& items);
size_t outboxStuckCount(const std::vector<OutboxItem>& items);
size_t outboxAttentionCount(const std::vector<OutboxItem>& items);

}  // namespace kk

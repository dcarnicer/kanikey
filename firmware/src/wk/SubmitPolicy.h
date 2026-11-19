#pragma once
// SPEED-01: when completed reviews are submitted to WaniKani. R12 stores the
// choice as its value (0 At session end, 5 Every 5, 1 Immediately), so the
// enum values match the NVS values. Pure C++: no app or Arduino headers.
#include <cstddef>
#include <cstdint>

#include "wk/Outbox.h"

namespace kk {

enum class SubmitPolicy : uint8_t {
    AtSessionEnd = 0,
    EveryFive = 5,
    Immediately = 1,
};

// Registry value to policy. Anything unknown falls back to AtSessionEnd, the
// default (a row with an invalid stored value shows the default too).
SubmitPolicy submitPolicyFromValue(int value);

// True when the completedCount-th item of the session is the moment to send
// the session's pending items. EveryFive fires on 5, 10, ...; Immediately on
// every completion; AtSessionEnd never sends mid-session.
bool submitAfterItem(SubmitPolicy policy, uint32_t completedCount);

// True when the summary may run a send loop for items still pending. Both
// AtSessionEnd and EveryFive can leave the tail of a session unsent.
bool submitAtSessionEnd(SubmitPolicy policy);

// The pre-review SRS stage for each queued outbox record. There is one session
// pending at a time; the previous code kept only the last pair, so only the
// final item's stage move could be reported. Capacity follows the outbox, and
// a full tracker drops the oldest entry (which the outbox size guarantees
// cannot happen in a normal session).
class StageTracker {
  public:
    static constexpr size_t kCapacity = kOutboxMax;

    // Records seq/assignmentId/fromStage and an optional tag (the journal
    // handle). A repeated assignmentId updates its existing entry so a
    // re-queued item keeps one slot.
    void record(uint32_t seq, uint32_t assignmentId, uint8_t fromStage, uint32_t tag = 0);
    // 0 when the assignment was queued before a reboot or is unknown.
    uint8_t fromStage(uint32_t assignmentId) const;
    // The tag recorded with the assignment, 0 when unknown.
    uint32_t tagFor(uint32_t assignmentId) const;
    bool forget(uint32_t assignmentId);
    void clear();
    size_t size() const { return count_; }
    uint32_t seqAt(size_t index) const;
    uint32_t assignmentAt(size_t index) const;

  private:
    struct Entry {
        uint32_t seq = 0;
        uint32_t assignmentId = 0;
        uint32_t tag = 0;
        uint8_t fromStage = 0;
    };
    Entry entries_[kCapacity];
    size_t count_ = 0;
};

}  // namespace kk

#include "wk/SubmitPolicy.h"

namespace kk {

SubmitPolicy submitPolicyFromValue(int value) {
    switch (value) {
        case static_cast<int>(SubmitPolicy::Immediately):
            return SubmitPolicy::Immediately;
        case static_cast<int>(SubmitPolicy::EveryFive):
            return SubmitPolicy::EveryFive;
        default:
            return SubmitPolicy::AtSessionEnd;
    }
}

bool submitAfterItem(SubmitPolicy policy, uint32_t completedCount) {
    if (completedCount == 0) return false;
    switch (policy) {
        case SubmitPolicy::EveryFive:
            return completedCount % 5 == 0;
        case SubmitPolicy::Immediately:
            return true;
        default:
            return false;
    }
}

bool submitAtSessionEnd(SubmitPolicy policy) {
    return policy != SubmitPolicy::Immediately;
}

void StageTracker::record(uint32_t seq, uint32_t assignmentId, uint8_t fromStage) {
    if (seq == 0 || assignmentId == 0) return;
    for (size_t i = 0; i < count_; ++i) {
        if (entries_[i].assignmentId != assignmentId) continue;
        entries_[i].seq = seq;
        entries_[i].fromStage = fromStage;
        return;
    }
    if (count_ == kCapacity) {
        // Drop the oldest slot; the outbox cap makes this unreachable for a
        // session that fits in the outbox.
        for (size_t i = 1; i < count_; ++i) entries_[i - 1] = entries_[i];
        --count_;
    }
    entries_[count_].seq = seq;
    entries_[count_].assignmentId = assignmentId;
    entries_[count_].fromStage = fromStage;
    ++count_;
}

uint8_t StageTracker::fromStage(uint32_t assignmentId) const {
    for (size_t i = 0; i < count_; ++i)
        if (entries_[i].assignmentId == assignmentId) return entries_[i].fromStage;
    return 0;
}

bool StageTracker::forget(uint32_t assignmentId) {
    for (size_t i = 0; i < count_; ++i) {
        if (entries_[i].assignmentId != assignmentId) continue;
        for (size_t j = i + 1; j < count_; ++j) entries_[j - 1] = entries_[j];
        --count_;
        entries_[count_] = Entry{};
        return true;
    }
    return false;
}

void StageTracker::clear() {
    for (size_t i = 0; i < count_; ++i) entries_[i] = Entry{};
    count_ = 0;
}

uint32_t StageTracker::seqAt(size_t index) const {
    return index < count_ ? entries_[index].seq : 0;
}

uint32_t StageTracker::assignmentAt(size_t index) const {
    return index < count_ ? entries_[index].assignmentId : 0;
}

}  // namespace kk

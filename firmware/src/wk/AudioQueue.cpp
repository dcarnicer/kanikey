#include "wk/AudioQueue.h"

#include <algorithm>

namespace kk {

void AudioQueue::useAccount(uint32_t accountKey) {
    if (accountKey == accountKey_) return;
    items_.clear();
    accountKey_ = accountKey;
}

AudioQueue::Add AudioQueue::enqueue(uint32_t subjectId, uint8_t voice, const std::string& url,
                                    const std::string& contentType) {
    if (contains(subjectId, voice)) return Add::Duplicate;
    if (items_.size() >= kAudioQueueMax) return Add::Full;
    AudioQueueItem item;
    item.subjectId = subjectId;
    item.voice = voice;
    item.url = url;
    item.contentType = contentType;
    items_.push_back(std::move(item));
    return Add::Added;
}

bool AudioQueue::contains(uint32_t subjectId, uint8_t voice) const {
    return std::any_of(items_.begin(), items_.end(), [=](const AudioQueueItem& item) {
        return item.subjectId == subjectId && item.voice == voice;
    });
}

void AudioQueue::popFront() {
    if (!items_.empty()) items_.erase(items_.begin());
}

bool AudioQueue::failFront() {
    if (items_.empty()) return false;
    AudioQueueItem item = std::move(items_.front());
    items_.erase(items_.begin());
    ++item.failures;
    if (item.failures >= kAudioQueueMaxFailures) return true;
    items_.push_back(std::move(item));
    return false;
}

bool audioDrainAllowed(const AudioDrainState& s) {
    return s.queued && s.screenIdle && !s.sessionActive && !s.syncRunning && s.wifiUp &&
           s.sdOk && s.tokenUsable && s.msSinceAttempt >= s.spacingMs;
}

bool audioDrainHeapOk(size_t largestFree, size_t totalFree) {
    return largestFree >= kAudioDrainMinLargest && totalFree >= kAudioDrainMinFree;
}

}  // namespace kk

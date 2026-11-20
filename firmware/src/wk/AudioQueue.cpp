#include "wk/AudioQueue.h"

namespace kk {

void AudioQueue::useAccount(uint32_t accountKey) {
    if (accountKey == accountKey_) return;
    count_ = 0;
    accountKey_ = accountKey;
}

AudioQueue::Add AudioQueue::enqueue(uint32_t subjectId, uint8_t voice, const std::string& url) {
    if (contains(subjectId, voice)) return Add::Duplicate;
    if (url.size() > kAudioUrlMax) return Add::TooLong;
    if (count_ >= kAudioQueueMax) return Add::Full;
    AudioQueueItem& item = items_[count_++];
    item.subjectId = subjectId;
    item.voice = voice;
    item.failures = 0;
    item.urlLen = static_cast<uint8_t>(url.size());
    std::memcpy(item.url, url.data(), url.size());
    item.url[url.size()] = '\0';
    return Add::Added;
}

bool AudioQueue::contains(uint32_t subjectId, uint8_t voice) const {
    for (size_t i = 0; i < count_; ++i)
        if (items_[i].subjectId == subjectId && items_[i].voice == voice) return true;
    return false;
}

void AudioQueue::removeFront() {
    for (size_t i = 1; i < count_; ++i) items_[i - 1] = items_[i];
    --count_;
}

void AudioQueue::popFront() {
    if (count_ > 0) removeFront();
}

bool AudioQueue::failFront() {
    if (count_ == 0) return false;
    AudioQueueItem item = items_[0];
    removeFront();
    ++item.failures;
    if (item.failures >= kAudioQueueMaxFailures) return true;
    items_[count_++] = item;  // a slot is free: removeFront() made one
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

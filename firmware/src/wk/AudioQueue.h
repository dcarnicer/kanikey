#pragma once
// AUD-02 download queue, pure rules. The queue keeps the clip targets in RAM in
// arrival order, with a bound, dedupe by subject and voice, and a failure limit
// per entry. The storage is a fixed array inside the object: enqueueing never
// allocates, so the queue adds nothing to the heap fragmentation that a clip
// download needs (hardware 2026-11-20: 127 KB free, largest block 41 KB, and the
// drain refused). audioDrainAllowed() and audioDrainHeapOk() decide when one
// clip may be downloaded. No Arduino headers, so the native tests cover them.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace kk {

inline constexpr size_t kAudioQueueMax = 24;
inline constexpr uint8_t kAudioQueueMaxFailures = 2;

// Longest URL the queue keeps, in bytes without the terminating NUL. WaniKani
// clip URLs were 60-100 characters on 2026-11-20. A longer one is refused
// (AudioQueue::Add::TooLong) and logged by the caller, never truncated.
inline constexpr size_t kAudioUrlMax = 128;

// Heap a clip download needs once the canvas is released: the TLS handshake
// needs a large contiguous block, and the total must leave room beside it.
// 28 KB since the MEM-02 core (2026-11-20): mbedTLS now takes one 16.7 KB input
// buffer and a 4.4 KB output buffer (it was two 16.7 KB buffers, when 23.5 KB
// failed with `start_ssl_client: -1` and 45 KB was the rule). The old rule left
// the Home drain stuck at 34.8 KB largest / 139 KB free after one clip.
inline constexpr size_t kAudioDrainMinLargest = 28 * 1024;
inline constexpr size_t kAudioDrainMinFree = 60 * 1024;

// Minimum time between download attempts on the Home tick: after a clip (or a
// failure) and, longer, after a heap refusal, so a short heap does not churn
// the canvas every loop.
inline constexpr uint32_t kAudioDrainSpacingMs = 2000;
inline constexpr uint32_t kAudioDrainRefusedSpacingMs = 10000;

struct AudioQueueItem {
    uint32_t subjectId = 0;
    uint8_t voice = 0;     // 0 female, 1 male
    uint8_t failures = 0;  // failed attempts so far
    uint8_t urlLen = 0;    // bytes in url
    char url[kAudioUrlMax + 1] = {};  // the MP3 URL of the chosen clip, NUL-terminated
};

class AudioQueue {
public:
    enum class Add : uint8_t { Added, Duplicate, Full, TooLong };

    uint32_t accountKey() const { return accountKey_; }

    // Keeps the queue for `accountKey`. A different account empties it: the
    // URLs belong to one token.
    void useAccount(uint32_t accountKey);

    Add enqueue(uint32_t subjectId, uint8_t voice, const std::string& url);
    bool contains(uint32_t subjectId, uint8_t voice) const;

    bool empty() const { return count_ == 0; }
    size_t size() const { return count_; }

    // Requires !empty(). The reference is valid until the next mutation.
    const AudioQueueItem& front() const { return items_[0]; }

    // Success: the front entry leaves the queue.
    void popFront();

    // Failure of the front entry. It moves to the back so the others get a
    // turn; at kAudioQueueMaxFailures it is dropped. Returns true when dropped.
    bool failFront();

    void clear() { count_ = 0; }

private:
    // Drops items_[0] and shifts the rest forward. Requires !empty().
    void removeFront();

    uint32_t accountKey_ = 0;
    size_t count_ = 0;
    AudioQueueItem items_[kAudioQueueMax];
};

// Everything the drain decision depends on, read by the caller at the moment
// of the tick. msSinceAttempt is kAudioDrainNever before the first attempt.
inline constexpr uint32_t kAudioDrainNever = 0xFFFFFFFFu;
struct AudioDrainState {
    bool queued = false;         // the queue has an entry
    bool screenIdle = false;     // Home with no modal, or an inline batch hook
    bool sessionActive = false;  // a WkClient session is open (TLS state held)
    bool syncRunning = false;    // the sync job or its flag
    bool wifiUp = false;
    bool sdOk = false;
    bool tokenUsable = false;    // a token, not auth-blocked, not rate limited
    uint32_t msSinceAttempt = kAudioDrainNever;
    uint32_t spacingMs = kAudioDrainSpacingMs;
};

// True when every non-heap rule holds: the queue has work, the device is idle,
// no API session or sync holds the link or the TLS heap, Wi-Fi and the card are
// up, the token is usable, and the attempt spacing has passed.
bool audioDrainAllowed(const AudioDrainState& state);

// The heap rule, checked after the canvas is released.
bool audioDrainHeapOk(size_t largestFree, size_t totalFree);

}  // namespace kk

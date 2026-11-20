#pragma once
// AUD-02 download queue, pure rules. The queue keeps the clip targets of a
// batch in RAM (URLs copied, so the batch can be evicted), in arrival order,
// with a bound, dedupe by subject and voice, and a failure limit per entry.
// audioDrainAllowed() and audioDrainHeapOk() decide when one clip may be
// downloaded. No Arduino headers, so the native tests cover them.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kk {

inline constexpr size_t kAudioQueueMax = 24;
inline constexpr uint8_t kAudioQueueMaxFailures = 2;

// Heap a clip download needs once the canvas is released: the TLS handshake
// needs a large contiguous block (hardware 2026-11-20: 23.5 KB largest failed,
// `start_ssl_client: -1`), and the total must leave room beside it.
inline constexpr size_t kAudioDrainMinLargest = 45 * 1024;
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
    std::string url;       // the MP3 URL of the chosen clip
    std::string contentType;
};

class AudioQueue {
public:
    enum class Add : uint8_t { Added, Duplicate, Full };

    uint32_t accountKey() const { return accountKey_; }

    // Keeps the queue for `accountKey`. A different account empties it: the
    // URLs belong to one token.
    void useAccount(uint32_t accountKey);

    Add enqueue(uint32_t subjectId, uint8_t voice, const std::string& url,
                const std::string& contentType);
    bool contains(uint32_t subjectId, uint8_t voice) const;

    bool empty() const { return items_.empty(); }
    size_t size() const { return items_.size(); }

    // Requires !empty(). The reference is valid until the next mutation.
    const AudioQueueItem& front() const { return items_.front(); }

    // Success: the front entry leaves the queue.
    void popFront();

    // Failure of the front entry. It moves to the back so the others get a
    // turn; at kAudioQueueMaxFailures it is dropped. Returns true when dropped.
    bool failFront();

    void clear() { items_.clear(); }

private:
    uint32_t accountKey_ = 0;
    std::vector<AudioQueueItem> items_;
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

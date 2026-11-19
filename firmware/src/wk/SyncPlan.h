#pragma once
// Which sync a boot needs (SPEED-02) and whether the review batch should be
// prefetched (SPEED-04). Pure logic: no Arduino headers.
//
// The Y1 setting picks the requested kind (Off/Quick/Full). A manual Sync now
// bypasses this function and always runs a full sync.
#include <cstddef>
#include <cstdint>

namespace kk {

enum class SyncKind : uint8_t { Skip, Quick, Full };

// Y1 "Auto sync on wake" values. They are stored as their meaning, not as a
// choice index, so reordering the menu cannot change a stored setting.
constexpr int kAutoSyncOff = 0;
constexpr int kAutoSyncQuick = 1;
constexpr int kAutoSyncFull = 2;

// Intervals can only be compared with a trusted clock (after SNTP).
constexpr uint32_t kSyncTrustedEpoch = 1700000000u;
constexpr uint32_t kSyncSkipRecentSeconds = 5u * 60u;
constexpr uint32_t kUserRefreshSeconds = 6u * 3600u;
constexpr uint32_t kFullSyncDefaultSeconds = 6u * 3600u;

struct SyncTimes {
    uint32_t now = 0;       // effectiveNow() at the decision
    uint32_t lastQuick = 0; // last successful /summary
    uint32_t lastFull = 0;  // last completed full sync
    uint32_t lastUser = 0;  // last successful /user
};

// Returns what an automatic sync should do. `requested` is the Y1 choice
// (Skip, Quick or Full); a Full request always stays Full. A Quick request
// becomes Full when the last full sync is due, and any Quick request is Skip
// while the last successful sync is younger than five minutes. With an
// untrusted clock no interval is compared: a cold boot runs Full, a wake runs
// Quick. Manual Sync now passes Full and therefore always runs a full sync.
SyncKind planSync(SyncKind requested, bool coldBoot, const SyncTimes& times,
                  uint32_t fullEverySeconds);

// True when an automatic quick sync must fetch /user: on a cold power-on or
// when the last /user is six hours old. An unknown or untrusted time fetches,
// because the freshness cannot be proven.
bool quickNeedsUser(const SyncTimes& times, bool coldBoot);

// SPEED-04: prefetch only when reviews are due, the cached batch is short and
// the device is not running on a low battery.
bool shouldPrefetch(uint32_t due, size_t cachedCount, size_t batchSize, bool lowBattery);

}  // namespace kk

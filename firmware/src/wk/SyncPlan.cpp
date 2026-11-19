#include "wk/SyncPlan.h"

namespace kk {

SyncKind planSync(SyncKind requested, bool coldBoot, const SyncTimes& times,
                  uint32_t fullEverySeconds) {
    if (requested == SyncKind::Skip) return SyncKind::Skip;
    // Full is explicit (manual Sync now or Y1 = Full): never downgraded.
    if (requested == SyncKind::Full) return SyncKind::Full;

    // Without a trusted clock the intervals cannot be compared. A cold boot is
    // the only case that can prove the cache may be old.
    if (times.now < kSyncTrustedEpoch)
        return coldBoot ? SyncKind::Full : SyncKind::Quick;

    // A sync less than five minutes old is skipped, including the outbox
    // flush: the cache can be shown as it is.
    if (times.lastQuick && times.now >= times.lastQuick &&
        times.now - times.lastQuick < kSyncSkipRecentSeconds)
        return SyncKind::Skip;

    // A missing, future (clock moved back) or due full sync runs Full so the
    // level progress and SRS distribution catch up.
    if (times.lastFull == 0 || times.now < times.lastFull ||
        times.now - times.lastFull >= fullEverySeconds)
        return SyncKind::Full;
    return SyncKind::Quick;
}

bool quickNeedsUser(const SyncTimes& times, bool coldBoot) {
    if (coldBoot) return true;
    if (times.now < kSyncTrustedEpoch) return true;
    if (times.lastUser == 0 || times.now < times.lastUser) return true;
    return times.now - times.lastUser >= kUserRefreshSeconds;
}

bool shouldPrefetch(uint32_t due, size_t cachedCount, size_t batchSize, bool lowBattery) {
    if (lowBattery || due == 0 || batchSize == 0) return false;
    return cachedCount < batchSize;
}

}  // namespace kk

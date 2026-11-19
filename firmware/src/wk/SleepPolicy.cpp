#include "wk/SleepPolicy.h"

namespace kk {

SleepDuringReview sleepDuringReviewFromValue(int value) {
    return value == 600 ? SleepDuringReview::After10Min : SleepDuringReview::Never;
}

bool reviewMayDeepSleep(SleepDuringReview policy, uint32_t idleMs) {
    return policy == SleepDuringReview::After10Min && idleMs >= kReviewSleepMs;
}

bool deepSleepAllowed(bool reviewOpen, SleepDuringReview policy, uint32_t idleMs) {
    return !reviewOpen || reviewMayDeepSleep(policy, idleMs);
}

bool sleepDeadlineReached(uint32_t idleMs, uint32_t sleepMs) {
    return sleepMs != 0 && idleMs >= sleepMs;
}

uint32_t lightSleepSliceMs(uint32_t idleMs, uint32_t blankMs, uint32_t sleepMs) {
    // No blank, no light sleep. A zero P4 timer means "never", so there is no
    // deadline to wake for either.
    if (blankMs == 0 || sleepMs == 0) return 0;
    if (idleMs >= sleepMs) return 0;
    return sleepMs - idleMs;
}

uint32_t reviewSleepSliceMs(SleepDuringReview policy, uint32_t idleMs) {
    if (policy != SleepDuringReview::After10Min) return 0;
    if (idleMs >= kReviewSleepMs) return 0;
    return kReviewSleepMs - idleMs;
}

uint32_t lightSleepTimerMs(uint32_t idleMs, uint32_t blankMs, uint32_t sleepMs, bool reviewOpen,
                           SleepDuringReview policy) {
    if (reviewOpen) return reviewSleepSliceMs(policy, idleMs);
    return lightSleepSliceMs(idleMs, blankMs, sleepMs);
}

}  // namespace kk

#include "wk/WorkloadGuard.h"

#include <cstdio>

namespace kk {

namespace {

bool over(uint32_t value, uint32_t limit) {
    return limit != 0 && value > limit;
}

}  // namespace

uint8_t workloadWarning(uint32_t apprentice, uint32_t dueReviews, uint32_t startedToday,
                        const WorkloadThresholds& thresholds) {
    uint8_t reasons = kWorkloadNone;
    if (over(apprentice, thresholds.apprentice)) reasons |= kWorkloadApprentice;
    if (over(dueReviews, thresholds.dueReviews)) reasons |= kWorkloadDueReviews;
    if (over(startedToday, thresholds.startedToday)) reasons |= kWorkloadStartedToday;
    return reasons;
}

bool formatWorkloadReason(uint8_t reasons, WorkloadFlag flag, uint32_t apprentice,
                          uint32_t dueReviews, uint32_t startedToday,
                          const WorkloadThresholds& thresholds, char* out, size_t n) {
    if (!out || n == 0 || (reasons & flag) == 0) return false;
    switch (flag) {
        case kWorkloadApprentice:
            snprintf(out, n, "Apprentice items: %u (limit %u)",
                     static_cast<unsigned>(apprentice),
                     static_cast<unsigned>(thresholds.apprentice));
            return true;
        case kWorkloadDueReviews:
            snprintf(out, n, "Reviews due: %u (limit %u)", static_cast<unsigned>(dueReviews),
                     static_cast<unsigned>(thresholds.dueReviews));
            return true;
        case kWorkloadStartedToday:
            snprintf(out, n, "Lessons started today: %u (limit %u)",
                     static_cast<unsigned>(startedToday),
                     static_cast<unsigned>(thresholds.startedToday));
            return true;
        default:
            return false;
    }
}

}  // namespace kk

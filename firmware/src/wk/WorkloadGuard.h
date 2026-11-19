#pragma once
// LES-05 workload guard: decides whether starting a lesson batch should warn
// about the Apprentice pile, the due review backlog or the lessons already
// started today. Pure C++; the controller fills the counts.
#include <cstddef>
#include <cstdint>

namespace kk {

// One bit per reason; a limit of 0 disables that check.
enum WorkloadFlag : uint8_t {
    kWorkloadNone = 0,
    kWorkloadApprentice = 1u << 0,
    kWorkloadDueReviews = 1u << 1,
    kWorkloadStartedToday = 1u << 2,
};

struct WorkloadThresholds {
    uint32_t apprentice = 0;    // L4 `ls.appr`, 0 = off
    uint32_t dueReviews = 0;    // L5 `ls.due`, 0 = off
    uint32_t startedToday = 0;  // L6 `ls.daily`, 0 = off
};

// Returns the WorkloadFlag bits whose enabled limit is exceeded.
uint8_t workloadWarning(uint32_t apprentice, uint32_t dueReviews, uint32_t startedToday,
                        const WorkloadThresholds& thresholds);

// Writes the screen line for one reason, for example
// "Apprentice items: 120 (limit 100)". Returns false when the flag is not in
// `reasons` or `out` is invalid.
bool formatWorkloadReason(uint8_t reasons, WorkloadFlag flag, uint32_t apprentice,
                          uint32_t dueReviews, uint32_t startedToday,
                          const WorkloadThresholds& thresholds, char* out, size_t n);

}  // namespace kk

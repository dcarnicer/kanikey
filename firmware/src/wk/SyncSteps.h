#pragma once
// The non-modal sync job's step machine (SPEED-03). Pure logic: the app layer
// performs one request per step and asks this module what comes next.
#include <cstdint>

#include "wk/SyncPlan.h"

namespace kk {

enum class SyncStep : uint8_t {
    Idle,
    Wifi,
    Time,
    Outbox,
    Account,
    Summary,
    Level,
    Srs,
    Critical,
    Prefetch,
    Done,
    Failed,
};

// Optional steps may be skipped when an interruption (Enter) or a missing
// feature makes them unnecessary; a failure never aborts the sync.
bool stepIsOptional(SyncStep step);

// Number of counted steps for the Home "sync n/m" indicator. Time is counted;
// the account step is only present when it is needed.
uint8_t stepCount(SyncKind kind, bool needsAccount);

// One-based position of `step` in the same sequence as stepCount(), or 0 for
// Idle/Done/Failed and steps that are not part of `kind`.
uint8_t stepNumber(SyncStep step, SyncKind kind, bool needsAccount);

// The next step in the plan. `interrupted` skips everything that is left so
// the caller can start a cached review.
SyncStep nextStep(SyncKind kind, SyncStep current, bool interrupted);

}  // namespace kk

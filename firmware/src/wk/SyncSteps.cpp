#include "wk/SyncSteps.h"

namespace kk {

bool stepIsOptional(SyncStep step) {
    switch (step) {
        case SyncStep::Account:
        case SyncStep::Level:
        case SyncStep::Srs:
        case SyncStep::Critical:
        case SyncStep::Prefetch:
            return true;
        default:
            return false;
    }
}

uint8_t stepCount(SyncKind kind, bool needsAccount) {
    if (kind == SyncKind::Skip) return 0;
    const uint8_t account = needsAccount ? 1 : 0;
    if (kind == SyncKind::Quick) return static_cast<uint8_t>(4 + account);
    // Full adds Level, Srs, Critical and Prefetch to the four quick steps.
    return static_cast<uint8_t>(8 + account);
}

uint8_t stepNumber(SyncStep step, SyncKind kind, bool needsAccount) {
    uint8_t n = 0;
    switch (step) {
        case SyncStep::Wifi: n = 1; break;
        case SyncStep::Time: n = 2; break;
        case SyncStep::Outbox: n = 3; break;
        case SyncStep::Account: n = 4; break;
        case SyncStep::Summary: n = 5; break;
        case SyncStep::Level: n = 6; break;
        case SyncStep::Srs: n = 7; break;
        case SyncStep::Critical: n = 8; break;
        case SyncStep::Prefetch: n = 9; break;
        default: return 0;
    }
    if (kind == SyncKind::Quick && n > 5) return 0;  // step not in the quick plan
    if (!needsAccount && n > 3) --n;
    return n;
}

SyncStep nextStep(SyncKind kind, SyncStep current, bool interrupted) {
    if (interrupted) return SyncStep::Done;
    switch (current) {
        case SyncStep::Idle: return SyncStep::Wifi;
        case SyncStep::Wifi: return SyncStep::Time;
        case SyncStep::Time: return SyncStep::Outbox;
        case SyncStep::Outbox: return SyncStep::Account;
        case SyncStep::Account: return SyncStep::Summary;
        case SyncStep::Summary:
            return kind == SyncKind::Full ? SyncStep::Level : SyncStep::Done;
        case SyncStep::Level: return SyncStep::Srs;
        case SyncStep::Srs: return SyncStep::Critical;
        case SyncStep::Critical:
            return kind == SyncKind::Full ? SyncStep::Prefetch : SyncStep::Done;
        case SyncStep::Prefetch: return SyncStep::Done;
        case SyncStep::Done:
        case SyncStep::Failed:
            return current;
    }
    return SyncStep::Done;
}

}  // namespace kk

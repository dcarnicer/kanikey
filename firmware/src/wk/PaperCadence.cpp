// PD-04: the PaperDIY sync cadence. See PaperCadence.h.
#include "wk/PaperCadence.h"

namespace kk {

uint32_t cadenceBackoffS(uint32_t failures, const CadenceConfig& cfg) {
    if (failures == 0) return 0;
    uint64_t gap = cfg.minGapS;
    for (uint32_t i = 1; i < failures && gap < cfg.backoffCapS; ++i) gap *= 2;
    return gap < cfg.backoffCapS ? static_cast<uint32_t>(gap) : cfg.backoffCapS;
}

CadenceWake cadenceNextWake(const CadenceInputs& in, const CadenceConfig& cfg,
                            const ReminderRules& quiet, UtcOffsetFn offsetAt) {
    CadenceWake out;
    if (in.nowUtc == 0) return out;  // no clock: no wall time to schedule against
    if (in.batteryPercent >= 0 && in.batteryPercent < cfg.lowPercent) {
        out.reason = CadenceReason::LowBattery;
        return out;
    }

    const uint64_t now = in.nowUtc;
    const bool high = in.batteryPercent >= cfg.midPercent;
    uint64_t at = now + (high ? cfg.periodicHighS : cfg.periodicLowS);
    CadenceReason reason = CadenceReason::Periodic;
    if (in.nextReviewUtc) {
        const uint64_t bucket = static_cast<uint64_t>(in.nextReviewUtc) + cfg.bucketGraceS;
        if (bucket > now && bucket < at) {
            at = bucket;
            reason = CadenceReason::Bucket;
        }
    }

    // The floor: the minimum gap, a back-off after failures, and a rate-limit
    // wait. The largest of the three wins, so a failing device never storms.
    uint64_t gap = cfg.minGapS;
    const uint32_t backoff = cadenceBackoffS(in.failures, cfg);
    if (backoff > gap) gap = backoff;
    if (in.retryAfterS > gap) gap = in.retryAfterS;
    if (at < now + gap) at = now + gap;

    out.at = skipQuietHours(static_cast<uint32_t>(at), quiet, offsetAt);
    out.reason = reason;
    return out;
}

WakePlan planWake(uint32_t reminderAt, const CadenceWake& cadence) {
    WakePlan p;
    if (reminderAt && (!cadence.at || reminderAt <= cadence.at)) {
        p.at = reminderAt;
        p.reason = WakeReason::Reminder;
        return p;
    }
    if (cadence.at) {
        p.at = cadence.at;
        p.reason = cadence.reason == CadenceReason::Bucket ? WakeReason::Bucket
                                                           : WakeReason::Periodic;
        return p;
    }
    if (cadence.reason == CadenceReason::LowBattery) p.reason = WakeReason::LowBattery;
    return p;
}

const char* wakeReasonName(WakeReason reason) {
    switch (reason) {
        case WakeReason::Bucket: return "bucket";
        case WakeReason::Periodic: return "periodic";
        case WakeReason::Reminder: return "reminder";
        case WakeReason::LowBattery: return "lowbatt";
        case WakeReason::None: break;
    }
    return "none";
}

bool timerDue(uint32_t targetUtc, uint32_t nowUtc, uint32_t slackS) {
    if (targetUtc == 0) return false;
    return static_cast<uint64_t>(nowUtc) + slackS >= targetUtc;
}

}  // namespace kk

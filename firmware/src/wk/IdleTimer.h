#pragma once
// Idle-age helpers for the power controller. App.cpp captures `now` once at
// the top of the loop; a sync step runs after that and refreshes
// `gLastInputAt` with millis(), which can be a few milliseconds newer than
// `now`. A plain `now - last` underflows to ~49 days and makes the device look
// idle, blanking the panel in the middle of a sync. Clamp that case to zero.
#include <cstdint>

namespace kk {

// Milliseconds since `last`, clamped to zero when `last` is newer than `now`.
// The signed difference is also wrap-safe for intervals under ~24 days.
inline uint32_t idleAgeMs(uint32_t now, uint32_t last) {
    const int32_t age = static_cast<int32_t>(now - last);
    return age > 0 ? static_cast<uint32_t>(age) : 0;
}

// True when `limitMs` is enabled and at least that much idle time has passed.
inline bool idleDue(uint32_t now, uint32_t last, uint32_t limitMs) {
    return limitMs != 0 && idleAgeMs(now, last) > limitMs;
}

}  // namespace kk

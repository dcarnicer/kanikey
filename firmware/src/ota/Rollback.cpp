#include "ota/Rollback.h"

#include <algorithm>
#include <cstring>

namespace kk::ota {

RollbackDecision rollbackDecision(const RollbackRecord& stored,
                                  std::string_view rolledBackVersion) {
    RollbackDecision out;
    out.record = stored;
    if (rolledBackVersion.empty()) return out;

    if (std::string_view(stored.version) == rolledBackVersion) {
        if (out.record.count < 255) ++out.record.count;
    } else {
        const size_t n = std::min(rolledBackVersion.size(), kRollbackVersionChars);
        std::memcpy(out.record.version, rolledBackVersion.data(), n);
        out.record.version[n] = '\0';
        out.record.count = 1;
    }
    out.blacklist = out.record.count >= 2;
    return out;
}

}  // namespace kk::ota

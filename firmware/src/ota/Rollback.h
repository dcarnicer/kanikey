#pragma once
// Two-strike rule for a rolled-back firmware version. Pure C++ so the decision
// can be tested without NVS or a bootloader.
//
// The first failed first boot of a version keeps it on offer (the rollback may
// have been a power cut); the second blacklists it. A different version starts
// the count again at one.
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kk::ota {

constexpr size_t kRollbackVersionChars = 23;

struct RollbackRecord {
    char version[24] = {};  // the version the count belongs to
    uint8_t count = 0;      // failed first boots of that version
};

struct RollbackDecision {
    RollbackRecord record;  // the record to store
    bool blacklist = false;  // true on the second failed first boot
};

// Applies one rollback of `rolledBackVersion` to the stored record. An empty
// version leaves the record unchanged and never blacklists.
RollbackDecision rollbackDecision(const RollbackRecord& stored,
                                  std::string_view rolledBackVersion);

}  // namespace kk::ota

#pragma once
// Persistent account-scoped critical-item list on LittleFS.
#include <cstdint>
#include <vector>

#include "wk/CriticalItems.h"

namespace kk {

bool criticalLoad(uint32_t accountKey, std::vector<CriticalItem>* out);
bool criticalSave(uint32_t accountKey, const std::vector<CriticalItem>& items);
void criticalClear();

}  // namespace kk

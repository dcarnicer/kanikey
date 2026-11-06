#pragma once
// Persistent apprentice-assignment pool for offline practice modes.
#include <cstdint>
#include <vector>

#include "wk/Practice.h"

namespace kk {

bool practicePoolLoad(uint32_t accountKey, std::vector<Assignment>* out);
bool practicePoolSave(uint32_t accountKey, const std::vector<Assignment>& assignments);
void practicePoolClear();

}  // namespace kk

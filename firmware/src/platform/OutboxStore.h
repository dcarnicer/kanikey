#pragma once
// Persistent outbox on LittleFS: /state/outbox.bin, with a .bak copy.
#include <vector>

#include "wk/Outbox.h"

namespace kk {

bool outboxLoad(std::vector<OutboxItem>& items);
bool outboxSave(const std::vector<OutboxItem>& items);

}  // namespace kk

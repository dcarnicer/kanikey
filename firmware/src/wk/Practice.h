#pragma once
// Pure selection and persistence helpers for local practice.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "wk/Types.h"

namespace kk {

constexpr size_t kPracticePoolMax = 256;
constexpr size_t kPracticeDefaultSize = 10;

// mode: 0 = Apprentice (stages 1-4), 1 = 24 h, 2 = 3 days, 3 = 7 days.
std::vector<Assignment> selectRecentLessons(const std::vector<Assignment>& assignments, int mode,
                                            uint32_t now, size_t limit);
std::vector<Assignment> selectCurrentLevel(const std::vector<Assignment>& assignments,
                                           uint8_t level, size_t limit);

bool encodePracticePool(const std::vector<Assignment>& assignments, uint32_t accountKey,
                        uint32_t savedAt, std::vector<uint8_t>* out);
bool decodePracticePool(const uint8_t* data, size_t size, uint32_t accountKey,
                        std::vector<Assignment>* out);

}  // namespace kk

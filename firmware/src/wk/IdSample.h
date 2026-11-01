#pragma once
// Deterministic sampling of subject IDs for a review batch. Pure C++ so the
// native tests can check the shuffle without an account or a network.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kk {

// Returns at most `limit` IDs from `ids` in a shuffled order. The order is
// deterministic for a given `seed`; every ID appears at most once. When
// `limit >= ids.size()` all IDs are returned (still shuffled).
std::vector<uint32_t> sampleIds(const std::vector<uint32_t>& ids, size_t limit,
                                uint32_t seed);

}  // namespace kk

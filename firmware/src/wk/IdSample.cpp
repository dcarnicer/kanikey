#include "wk/IdSample.h"

#include <algorithm>

namespace kk {

std::vector<uint32_t> sampleIds(const std::vector<uint32_t>& ids, size_t limit,
                                uint32_t seed) {
    std::vector<uint32_t> out = ids;
    if (out.empty() || limit == 0) {
        out.clear();
        return out;
    }
    // xorshift32 instead of std::mt19937: the order is the same on every
    // platform for a given seed, and it costs a few instructions instead of a
    // generator object. The seed comes from the batch, so two sessions differ.
    uint32_t state = seed ? seed : 1u;
    for (size_t i = out.size(); i > 1; --i) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        const size_t j = static_cast<size_t>(state % i);
        std::swap(out[i - 1], out[j]);
    }
    if (out.size() > limit) out.resize(limit);
    return out;
}

}  // namespace kk

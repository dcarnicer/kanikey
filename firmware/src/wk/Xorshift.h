#pragma once
// 32-bit xorshift (Marsaglia, 2003). Four bytes of state, where std::mt19937
// keeps about 2.5 KB (MEM-11). It only orders review batches and draws the
// Random first part, so it needs no cryptographic strength. A zero seed is
// replaced by 1, because xorshift never leaves the all-zero state.
//
// Models UniformRandomBitGenerator, so std::shuffle accepts it directly.
#include <cstdint>
#include <limits>

namespace kk {

class Xorshift32 {
public:
    using result_type = uint32_t;

    explicit Xorshift32(uint32_t seed = 1u) { this->seed(seed); }

    void seed(uint32_t value) { state_ = value ? value : 1u; }

    // Advances the state once and returns the new value (never 0).
    result_type operator()() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }

    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return std::numeric_limits<result_type>::max(); }

private:
    uint32_t state_ = 1u;
};

}  // namespace kk

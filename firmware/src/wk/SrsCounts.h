#pragma once
// SRS distribution for Home. WaniKani's /assignments collection returns a
// top-level total_count for a filtered query, so five tiny requests give the
// Apprentice/Guru/Master/Enlightened/Burned buckets without downloading every
// assignment. Pure C++.
#include <cstdint>

#include "wk/Json.h"

namespace kk {

enum class SrsGroup : uint8_t {
    Apprentice,   // stages 1-4
    Guru,         // stages 5-6
    Master,       // stage 7
    Enlightened,  // stage 8
    Burned,       // stage 9
    Count,
};

struct SrsCounts {
    uint32_t apprentice = 0;
    uint32_t guru = 0;
    uint32_t master = 0;
    uint32_t enlightened = 0;
    uint32_t burned = 0;
    bool valid = false;

    uint32_t total() const;
    uint32_t group(SrsGroup group) const;
    // Moves one assignment between buckets after a successful review; used
    // only when the cached distribution is already valid. The next sync is
    // authoritative and replaces the whole snapshot.
    bool moveStage(uint8_t fromStage, uint8_t toStage);
};

// Reads the top-level total_count of a collection response.
bool parseCollectionTotal(const JsonValue& root, uint32_t* out);

// Maps an assignment srs_stage (0-9) to its Home bucket; 0 maps to Count.
SrsGroup srsGroupForStage(uint8_t stage);

}  // namespace kk

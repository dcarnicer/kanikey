#include "wk/SrsCounts.h"

namespace kk {

namespace {
uint32_t* bucketPtr(SrsCounts* counts, SrsGroup group) {
    switch (group) {
        case SrsGroup::Apprentice: return &counts->apprentice;
        case SrsGroup::Guru: return &counts->guru;
        case SrsGroup::Master: return &counts->master;
        case SrsGroup::Enlightened: return &counts->enlightened;
        case SrsGroup::Burned: return &counts->burned;
        case SrsGroup::Count: break;
    }
    return nullptr;
}
}  // namespace

uint32_t SrsCounts::total() const {
    return apprentice + guru + master + enlightened + burned;
}

uint32_t SrsCounts::group(SrsGroup group) const {
    switch (group) {
        case SrsGroup::Apprentice: return apprentice;
        case SrsGroup::Guru: return guru;
        case SrsGroup::Master: return master;
        case SrsGroup::Enlightened: return enlightened;
        case SrsGroup::Burned: return burned;
        case SrsGroup::Count: break;
    }
    return 0;
}

bool parseCollectionTotal(const JsonValue& root, uint32_t* out) {
    if (!out) return false;
    const JsonValue* total = root.find("total_count");
    if (!total || !total->isNumber()) return false;
    if (total->number < 0 || total->number > 4000000000.0) return false;
    *out = static_cast<uint32_t>(total->number);
    return true;
}

SrsGroup srsGroupForStage(uint8_t stage) {
    if (stage >= 1 && stage <= 4) return SrsGroup::Apprentice;
    if (stage >= 5 && stage <= 6) return SrsGroup::Guru;
    if (stage == 7) return SrsGroup::Master;
    if (stage == 8) return SrsGroup::Enlightened;
    if (stage == 9) return SrsGroup::Burned;
    return SrsGroup::Count;
}

bool SrsCounts::moveStage(uint8_t fromStage, uint8_t toStage) {
    if (!valid) return false;
    const SrsGroup from = srsGroupForStage(fromStage);
    const SrsGroup to = srsGroupForStage(toStage);
    if (from == SrsGroup::Count || to == SrsGroup::Count) return false;
    if (from == to) return true;
    uint32_t* src = bucketPtr(this, from);
    uint32_t* dst = bucketPtr(this, to);
    if (!src || !dst || *src == 0) return false;
    --*src;
    ++*dst;
    return true;
}

}  // namespace kk

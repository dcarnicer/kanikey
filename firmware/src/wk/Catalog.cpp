#include "wk/Catalog.h"

#include <cstring>

#include "wk/Crc32.h"

namespace kk {

namespace {
constexpr uint16_t kVersion = 1;

void put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}
void put32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}
uint16_t get16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t get32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
}  // namespace

bool catalogMetaAccountMatches(const CatalogMeta& meta, uint32_t accountKey) {
    return accountKey != 0 && meta.accountKey == accountKey;
}

size_t catalogEvictIndex(const CatalogEntry* entries, size_t count) {
    if (!entries || count == 0) return count;
    size_t best = 0;
    for (size_t i = 1; i < count; ++i) {
        if (entries[i].lastUsed < entries[best].lastUsed ||
            (entries[i].lastUsed == entries[best].lastUsed &&
             entries[i].subjectId < entries[best].subjectId))
            best = i;
    }
    return best;
}

bool catalogNeedsEvict(size_t count, uint64_t bytes, size_t addBytes) {
    return count >= kCatalogMaxSubjects || bytes + addBytes > kCatalogMaxBytes;
}

void catalogMetaEncode(const CatalogMeta& meta, uint8_t out[kCatalogMetaBytes]) {
    std::memset(out, 0, kCatalogMetaBytes);
    std::memcpy(out, "KKCT", 4);
    put16(out + 4, kVersion);
    put32(out + 6, meta.accountKey);
    put32(out + 10, meta.savedAt);
    put16(out + 14, meta.count);
    put32(out + 16, meta.bytes);
    put32(out + 20, crc32(out, 20));
}

bool catalogMetaDecode(const uint8_t in[kCatalogMetaBytes], CatalogMeta* out) {
    if (!in || !out) return false;
    if (std::memcmp(in, "KKCT", 4) != 0) return false;
    if (get16(in + 4) != kVersion) return false;
    if (get32(in + 20) != crc32(in, 20)) return false;
    const uint16_t count = get16(in + 14);
    const uint32_t bytes = get32(in + 16);
    if (count > kCatalogMaxSubjects || bytes > kCatalogMaxBytes) return false;
    out->accountKey = get32(in + 6);
    out->savedAt = get32(in + 10);
    out->count = count;
    out->bytes = bytes;
    return true;
}

}  // namespace kk

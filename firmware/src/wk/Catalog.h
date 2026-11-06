#pragma once
// Pure metadata and cap/LRU helpers for the microSD subject catalog.
#include <cstddef>
#include <cstdint>

namespace kk {

constexpr size_t kCatalogMaxSubjects = 2000;
constexpr uint64_t kCatalogMaxBytes = 8ull * 1024ull * 1024ull;
constexpr size_t kCatalogSubjectHeaderBytes = 20;
constexpr size_t kCatalogMetaBytes = 24;

struct CatalogEntry {
    uint32_t subjectId = 0;
    uint32_t lastUsed = 0;
};

struct CatalogMeta {
    uint32_t accountKey = 0;
    uint32_t savedAt = 0;
    uint16_t count = 0;
    uint32_t bytes = 0;
};

bool catalogMetaAccountMatches(const CatalogMeta& meta, uint32_t accountKey);
// Picks the smallest lastUsed; ties go to the smallest subject id.
size_t catalogEvictIndex(const CatalogEntry* entries, size_t count);
bool catalogNeedsEvict(size_t count, uint64_t bytes, size_t addBytes);

// KKCT header: magic, version, account key, savedAt, count, bytes, CRC32.
void catalogMetaEncode(const CatalogMeta& meta, uint8_t out[kCatalogMetaBytes]);
bool catalogMetaDecode(const uint8_t in[kCatalogMetaBytes], CatalogMeta* out);

}  // namespace kk

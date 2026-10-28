#pragma once
// Binary serialization for the offline cache. Pure C++: the platform layer
// supplies the bytes, so the format can be tested natively without LittleFS or
// an SD card.
//
// Snapshot file (LittleFS): Home data, user and the cached review batch.
// Subject file (microSD): the subjects referenced by the cached assignments.
//
// Both files carry a magic, a format version, the save time, an account key
// (FNV-1a of the API token), the payload length and a CRC32. A cache written
// for another token is discarded instead of shown.
#include <cstdint>
#include <string_view>
#include <vector>

#include "wk/LevelProgress.h"
#include "wk/Summary.h"
#include "wk/Types.h"
#include "wk/User.h"

namespace kk {

constexpr uint16_t kCacheVersion = 7;
constexpr size_t kCacheHeaderBytes = 24;

struct CacheMeta {
    uint32_t savedAt = 0;     // Unix seconds; 0 when the clock was not trusted
    uint32_t accountKey = 0;  // FNV-1a of the API token
};

struct CacheSnapshot {
    CacheMeta meta;
    Summary summary;
    User user;
    LevelProgress levelProgress;
    std::vector<Assignment> assignments;
};

struct SubjectCache {
    CacheMeta meta;
    std::vector<Subject> subjects;
};

uint32_t cacheAccountKey(std::string_view token);

bool encodeCacheSnapshot(const CacheSnapshot& snapshot, std::vector<uint8_t>* out);
bool decodeCacheSnapshot(const uint8_t* data, size_t size, CacheSnapshot* out);
bool encodeSubjectCache(const SubjectCache& cache, std::vector<uint8_t>* out);
bool decodeSubjectCache(const uint8_t* data, size_t size, SubjectCache* out);

}  // namespace kk

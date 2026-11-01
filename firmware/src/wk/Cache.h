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
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "wk/LevelProgress.h"
#include "wk/SrsCounts.h"
#include "wk/Summary.h"
#include "wk/Types.h"
#include "wk/User.h"

namespace kk {

constexpr uint16_t kCacheVersion = 11;
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
    SrsCounts srs;
    std::vector<Assignment> assignments;
};

struct SubjectCache {
    CacheMeta meta;
    std::vector<Subject> subjects;
};

// Byte interfaces for the streaming subject cache. The platform layer wraps the
// SD file; the encoder and decoder only ever hold a fixed 1 KB block, so a save
// or load never needs the whole payload in one contiguous allocation (FIX-27).
struct ByteSink {
    virtual bool write(const uint8_t* data, size_t size) = 0;
    virtual ~ByteSink() = default;
};

struct ByteSource {
    virtual size_t read(uint8_t* data, size_t size) = 0;
    virtual ~ByteSource() = default;
};

uint32_t cacheAccountKey(std::string_view token);

bool encodeCacheSnapshot(const CacheSnapshot& snapshot, std::vector<uint8_t>* out);
bool decodeCacheSnapshot(const uint8_t* data, size_t size, CacheSnapshot* out);
bool encodeSubjectCache(const SubjectCache& cache, std::vector<uint8_t>* out);
// Same payload without copying the subjects into a SubjectCache.
bool encodeSubjectCache(const CacheMeta& meta, const std::vector<Subject>& subjects,
                        std::vector<uint8_t>* out);
bool decodeSubjectCache(const uint8_t* data, size_t size, SubjectCache* out);

// Streams the subject cache to `sink` with exactly the bytes that
// encodeSubjectCache() produces. Two passes over the subjects count the payload
// length and CRC first, so the 24-byte header can be written before the payload.
// Returns false on a sink failure, an over-long string or too many subjects.
bool writeSubjectCache(const CacheMeta& meta, const std::vector<Subject>& subjects,
                       ByteSink* sink);
// Reads a subject cache of `fileSize` bytes from `source`. On any error *out is
// left untouched.
bool readSubjectCache(ByteSource* source, size_t fileSize, SubjectCache* out);

}  // namespace kk

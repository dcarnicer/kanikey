#include "platform/CacheStore.h"

#include <Arduino.h>
#include <LittleFS.h>

#include <cstring>

namespace kk {

namespace {

constexpr const char* kSnapshotPath = "/state/cache.bin";
constexpr const char* kSnapshotTmp = "/state/cache.tmp";
constexpr const char* kSnapshotBak = "/state/cache.bak";
constexpr const char* kSubjectsPath = "/kanikey/cache/subjects.bin";
constexpr const char* kSubjectsTmp = "/kanikey/cache/subjects.tmp";
constexpr const char* kSubjectsBak = "/kanikey/cache/subjects.bak";
constexpr size_t kMaxSnapshotBytes = 256 * 1024;
constexpr size_t kMaxSubjectsBytes = 1024 * 1024;

bool readFile(fs::FS& fs, const char* path, size_t maxBytes, std::vector<uint8_t>* out) {
    if (!fs.exists(path)) return false;
    File f = fs.open(path, "r");
    if (!f) return false;
    const size_t size = static_cast<size_t>(f.size());
    if (size < kCacheHeaderBytes || size > maxBytes) {
        f.close();
        return false;
    }
    out->resize(size);
    size_t got = 0;
    while (got < size) {
        const size_t n = f.read(out->data() + got, size - got);
        if (n == 0) break;
        got += n;
    }
    f.close();
    if (got != size) {
        out->clear();
        return false;
    }
    return true;
}

bool writeAtomic(fs::FS& fs, const char* path, const char* tmp, const char* bak,
                 const std::vector<uint8_t>& bytes) {
    if (fs.exists(tmp)) fs.remove(tmp);
    File f = fs.open(tmp, "w");
    if (!f) return false;
    const size_t wrote = f.write(bytes.data(), bytes.size());
    f.close();
    if (wrote != bytes.size()) {
        fs.remove(tmp);
        return false;
    }
    if (fs.exists(bak)) fs.remove(bak);
    if (fs.exists(path)) fs.rename(path, bak);
    if (!fs.rename(tmp, path)) {
        fs.remove(tmp);
        return false;
    }
    return true;
}

}  // namespace

bool cacheLoadSnapshot(CacheSnapshot* out) {
    std::vector<uint8_t> bytes;
    if (readFile(LittleFS, kSnapshotPath, kMaxSnapshotBytes, &bytes)) {
        if (decodeCacheSnapshot(bytes.data(), bytes.size(), out)) return true;
        Serial.println("[cache] snapshot CRC/format error");
    }
    bytes.clear();
    if (readFile(LittleFS, kSnapshotBak, kMaxSnapshotBytes, &bytes)) {
        if (decodeCacheSnapshot(bytes.data(), bytes.size(), out)) {
            Serial.println("[cache] recovered snapshot from .bak");
            return true;
        }
    }
    return false;
}

bool cacheSaveSnapshot(const CacheSnapshot& snapshot) {
    std::vector<uint8_t> bytes;
    if (!encodeCacheSnapshot(snapshot, &bytes)) {
        Serial.println("[cache] snapshot encode failed");
        return false;
    }
    LittleFS.mkdir("/state");
    if (!writeAtomic(LittleFS, kSnapshotPath, kSnapshotTmp, kSnapshotBak, bytes)) {
        Serial.println("[cache] snapshot write failed");
        return false;
    }
    return true;
}

void cacheClearSnapshot() {
    if (LittleFS.exists(kSnapshotPath)) LittleFS.remove(kSnapshotPath);
    if (LittleFS.exists(kSnapshotTmp)) LittleFS.remove(kSnapshotTmp);
    if (LittleFS.exists(kSnapshotBak)) LittleFS.remove(kSnapshotBak);
}

bool cacheLoadSubjects(fs::FS& sd, SubjectCache* out) {
    std::vector<uint8_t> bytes;
    if (readFile(sd, kSubjectsPath, kMaxSubjectsBytes, &bytes)) {
        if (decodeSubjectCache(bytes.data(), bytes.size(), out)) return true;
        Serial.println("[cache] subject CRC/format error");
    }
    bytes.clear();
    if (readFile(sd, kSubjectsBak, kMaxSubjectsBytes, &bytes)) {
        if (decodeSubjectCache(bytes.data(), bytes.size(), out)) {
            Serial.println("[cache] recovered subjects from .bak");
            return true;
        }
    }
    return false;
}

bool cacheSaveSubjects(fs::FS& sd, const SubjectCache& cache) {
    std::vector<uint8_t> bytes;
    if (!encodeSubjectCache(cache, &bytes)) {
        Serial.println("[cache] subjects encode failed");
        return false;
    }
    sd.mkdir("/kanikey");
    sd.mkdir("/kanikey/cache");
    if (!writeAtomic(sd, kSubjectsPath, kSubjectsTmp, kSubjectsBak, bytes)) {
        Serial.println("[cache] subjects write failed");
        return false;
    }
    return true;
}

void cacheClearSubjects(fs::FS& sd) {
    if (sd.exists(kSubjectsPath)) sd.remove(kSubjectsPath);
    if (sd.exists(kSubjectsTmp)) sd.remove(kSubjectsTmp);
    if (sd.exists(kSubjectsBak)) sd.remove(kSubjectsBak);
}

}  // namespace kk

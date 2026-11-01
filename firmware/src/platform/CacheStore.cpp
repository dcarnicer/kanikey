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

// Copies `from` to `to` in 512 B chunks. Returns false when a file cannot be
// opened, a write is short or the byte count does not match; the caller keeps
// the destination file out of use in that case.
bool copyFile(fs::FS& fs, const char* from, const char* to) {
    File src = fs.open(from, "r");
    File dst = fs.open(to, "w");
    bool ok = src && dst;
    const size_t expected = src ? src.size() : 0;
    size_t total = 0;
    uint8_t buf[512];
    while (ok) {
        const size_t n = src.read(buf, sizeof buf);
        if (n == 0) break;
        if (dst.write(buf, n) != n) {
            ok = false;
            break;
        }
        total += n;
    }
    if (src) src.close();
    if (dst) dst.close();
    return ok && total == expected;
}

bool writeAtomic(fs::FS& fs, const char* path, const char* tmp, const char* bak,
                 const std::vector<uint8_t>& bytes) {
    // "w" truncates a tmp file left behind by a crashed save, so no exists() +
    // remove() runs first: on this core both log an error when the file is
    // absent (FIX-26). The remove below only touches a tmp that exists.
    File f = fs.open(tmp, "w");
    if (!f) return false;
    const size_t wrote = f.write(bytes.data(), bytes.size());
    f.close();
    if (wrote != bytes.size()) {
        fs.remove(tmp);
        return false;
    }
    // Copy the old file instead of renaming it away, as OutboxStore does: a
    // power cut before the last rename then leaves either the previous file
    // (main intact) or the new one, never neither. LittleFS rename replaces
    // the destination in one step.
    if (fs.exists(path) && !copyFile(fs, path, bak)) {
        fs.remove(bak);
        fs.remove(tmp);
        return false;
    }
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
    return cacheSaveSubjects(sd, cache.meta, cache.subjects);
}

bool cacheSaveSubjects(fs::FS& sd, const CacheMeta& meta, const std::vector<Subject>& subjects) {
    std::vector<uint8_t> bytes;
    if (!encodeSubjectCache(meta, subjects, &bytes)) {
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

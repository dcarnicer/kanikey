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

// Streams the subject cache through a File. Both directions hold a 1 KB block
// at a time, so a save or load never needs the whole payload in one allocation
// (FIX-27).
class FileSink : public ByteSink {
public:
    explicit FileSink(File& file) : file_(file) {}

    bool write(const uint8_t* data, size_t size) override {
        if (size == 0) return true;
        if (file_.write(data, size) != size) return false;
        written_ += size;
        return true;
    }
    size_t written() const { return written_; }

private:
    File& file_;
    size_t written_ = 0;
};

class FileSource : public ByteSource {
public:
    explicit FileSource(File& file) : file_(file) {}
    size_t read(uint8_t* data, size_t size) override { return file_.read(data, size); }

private:
    File& file_;
};

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

// Installs a closed tmp file over `path`, keeping the previous file as `bak`.
// On LittleFS the old file is copied rather than renamed away, as OutboxStore
// does: rename replaces the destination in one step, so a power cut leaves
// either the previous file or the new one, never neither.
bool installTmp(fs::FS& fs, const char* path, const char* tmp, const char* bak) {
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

// FAT (the SD card) refuses to rename onto an existing file, so a copy would
// not save the remove; it would also need a second open file on a card mounted
// with two slots, one of them held by the glyph pack. main moves to .bak, then
// tmp to main. A power cut between the two renames leaves .bak, which the
// loader falls back to.
bool installTmpFat(fs::FS& fs, const char* path, const char* tmp, const char* bak) {
    if (fs.exists(bak)) fs.remove(bak);
    if (fs.exists(path) && !fs.rename(path, bak)) {
        fs.remove(tmp);
        return false;
    }
    if (!fs.rename(tmp, path)) {
        fs.rename(bak, path);
        fs.remove(tmp);
        return false;
    }
    return true;
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
    return installTmp(fs, path, tmp, bak);
}

void logSubjectCache(const char* what, size_t subjects, size_t bytes, uint32_t ms) {
    char line[96];
    snprintf(line, sizeof line, "[cache] subjects %s: %u subjects, %u bytes, %u ms", what,
             static_cast<unsigned>(subjects), static_cast<unsigned>(bytes),
             static_cast<unsigned>(ms));
    Serial.println(line);
}

// Opens and streams one subject cache file. Leaves *out untouched on failure.
bool loadSubjectsFile(fs::FS& fs, const char* path, SubjectCache* out, size_t* bytes) {
    if (!fs.exists(path)) return false;
    File f = fs.open(path, "r");
    if (!f) return false;
    const size_t size = static_cast<size_t>(f.size());
    if (size < kCacheHeaderBytes || size > kMaxSubjectsBytes) {
        f.close();
        return false;
    }
    FileSource source(f);
    const bool ok = readSubjectCache(&source, size, out);
    f.close();
    if (ok && bytes) *bytes = size;
    return ok;
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
    const uint32_t started = millis();
    size_t bytes = 0;
    if (loadSubjectsFile(sd, kSubjectsPath, out, &bytes)) {
        logSubjectCache("loaded", out->subjects.size(), bytes, millis() - started);
        return true;
    }
    Serial.println("[cache] subject CRC/format error");
    bytes = 0;
    if (loadSubjectsFile(sd, kSubjectsBak, out, &bytes)) {
        Serial.println("[cache] recovered subjects from .bak");
        logSubjectCache("loaded", out->subjects.size(), bytes, millis() - started);
        return true;
    }
    return false;
}

bool cacheSaveSubjects(fs::FS& sd, const SubjectCache& cache) {
    return cacheSaveSubjects(sd, cache.meta, cache.subjects);
}

bool cacheSaveSubjects(fs::FS& sd, const CacheMeta& meta, const std::vector<Subject>& subjects) {
    const uint32_t started = millis();
    sd.mkdir("/kanikey");
    sd.mkdir("/kanikey/cache");
    // "w" truncates a tmp left by a crashed save (see writeAtomic).
    File f = sd.open(kSubjectsTmp, "w");
    if (!f) {
        Serial.println("[cache] subjects write failed");
        return false;
    }
    FileSink sink(f);
    const bool encoded = writeSubjectCache(meta, subjects, &sink);
    f.close();
    if (!encoded) {
        sd.remove(kSubjectsTmp);
        Serial.println("[cache] subjects encode failed");
        return false;
    }
    if (!installTmpFat(sd, kSubjectsPath, kSubjectsTmp, kSubjectsBak)) {
        Serial.println("[cache] subjects write failed");
        return false;
    }
    logSubjectCache("saved", subjects.size(), sink.written(), millis() - started);
    return true;
}

void cacheClearSubjects(fs::FS& sd) {
    if (sd.exists(kSubjectsPath)) sd.remove(kSubjectsPath);
    if (sd.exists(kSubjectsTmp)) sd.remove(kSubjectsTmp);
    if (sd.exists(kSubjectsBak)) sd.remove(kSubjectsBak);
}

}  // namespace kk

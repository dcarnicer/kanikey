#include "platform/SubjectCatalog.h"

#include <Arduino.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <vector>

#include "wk/Crc32.h"
#include "wk/SubjectCodec.h"

namespace kk {

namespace {
constexpr const char* kDir = "/kanikey/cache/catalog";
constexpr const char* kMetaPath = "/kanikey/cache/catalog/meta.bin";
constexpr const char* kMetaTmp = "/kanikey/cache/catalog/meta.tmp";
constexpr const char* kMetaBak = "/kanikey/cache/catalog/meta.bak";
constexpr const char* kSubjectMagic = "KKSB";
constexpr uint16_t kVersion = 1;

struct CatalogState {
    bool open = false;
    uint32_t accountKey = 0;
    CatalogMeta meta;
};

CatalogState gCatalog;

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

uint32_t nowSeconds() {
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    return now >= 1700000000u ? now : 0;
}

void subjectPath(uint32_t id, char* out, size_t n) {
    snprintf(out, n, "%s/%lu.kkc", kDir, static_cast<unsigned long>(id));
}

bool parseSubjectName(const char* name, uint32_t* id) {
    if (!name || !id) return false;
    const char* base = std::strrchr(name, '/');
    base = base ? base + 1 : name;
    uint32_t value = 0;
    size_t digits = 0;
    while (base[digits] >= '0' && base[digits] <= '9') {
        value = value * 10u + static_cast<uint32_t>(base[digits] - '0');
        ++digits;
    }
    if (digits == 0 || std::strcmp(base + digits, ".kkc") != 0) return false;
    *id = value;
    return value != 0;
}

bool readMeta(fs::FS& sd, CatalogMeta* out) {
    File file = sd.open(kMetaPath, "r");
    if (!file) return false;
    uint8_t bytes[kCatalogMetaBytes];
    size_t got = 0;
    while (got < sizeof bytes) {
        const int n = file.read(bytes + got, sizeof bytes - got);
        if (n <= 0) break;
        got += static_cast<size_t>(n);
    }
    file.close();
    return got == sizeof bytes && catalogMetaDecode(bytes, out);
}

bool writeMeta(fs::FS& sd, const CatalogMeta& meta) {
    uint8_t bytes[kCatalogMetaBytes];
    catalogMetaEncode(meta, bytes);
    File file = sd.open(kMetaTmp, "w");
    if (!file) return false;
    const bool written = file.write(bytes, sizeof bytes) == sizeof bytes;
    file.close();
    if (!written) {
        sd.remove(kMetaTmp);
        return false;
    }
    if (sd.exists(kMetaBak)) sd.remove(kMetaBak);
    if (sd.exists(kMetaPath) && !sd.rename(kMetaPath, kMetaBak)) {
        sd.remove(kMetaTmp);
        return false;
    }
    if (!sd.rename(kMetaTmp, kMetaPath)) {
        if (sd.exists(kMetaBak)) sd.rename(kMetaBak, kMetaPath);
        sd.remove(kMetaTmp);
        return false;
    }
    return true;
}

bool listIds(fs::FS& sd, std::vector<uint32_t>* ids) {
    ids->clear();
    File dir = sd.open(kDir);
    if (!dir) return false;
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory()) {
            uint32_t id = 0;
            if (parseSubjectName(entry.name(), &id)) ids->push_back(id);
        }
        entry.close();
    }
    dir.close();
    return true;
}

struct SubjectHeader {
    uint32_t lastUsed = 0;
    uint16_t payloadBytes = 0;
    uint32_t crc = 0;
};

bool readSubjectHeader(fs::FS& sd, uint32_t id, SubjectHeader* out) {
    char path[64];
    subjectPath(id, path, sizeof path);
    File file = sd.open(path, "r");
    if (!file) return false;
    uint8_t header[kCatalogSubjectHeaderBytes];
    size_t got = 0;
    while (got < sizeof header) {
        const int n = file.read(header + got, sizeof header - got);
        if (n <= 0) break;
        got += static_cast<size_t>(n);
    }
    file.close();
    if (got != sizeof header) return false;
    if (std::memcmp(header, kSubjectMagic, 4) != 0) return false;
    if (get16(header + 4) != kVersion) return false;
    if (get32(header + 6) != gCatalog.accountKey) return false;
    out->lastUsed = get32(header + 10);
    out->payloadBytes = get16(header + 14);
    out->crc = get32(header + 16);
    return true;
}

bool clearFiles(fs::FS& sd) {
    std::vector<uint32_t> ids;
    if (!listIds(sd, &ids)) return false;
    for (uint32_t id : ids) {
        char path[64];
        subjectPath(id, path, sizeof path);
        sd.remove(path);
    }
    if (sd.exists(kMetaPath)) sd.remove(kMetaPath);
    if (sd.exists(kMetaTmp)) sd.remove(kMetaTmp);
    if (sd.exists(kMetaBak)) sd.remove(kMetaBak);
    return true;
}

struct EvictEntry {
    uint32_t subjectId = 0;
    uint32_t lastUsed = 0;
    size_t bytes = 0;
};

}  // namespace

bool catalogOpen(fs::FS& sd, uint32_t accountKey) {
    if (accountKey == 0) return false;
    if (gCatalog.open && gCatalog.accountKey == accountKey) return true;
    gCatalog = CatalogState{};
    sd.mkdir("/kanikey");
    sd.mkdir("/kanikey/cache");
    sd.mkdir(kDir);
    CatalogMeta meta;
    if (readMeta(sd, &meta) && catalogMetaAccountMatches(meta, accountKey)) {
        gCatalog.open = true;
        gCatalog.accountKey = accountKey;
        gCatalog.meta = meta;
        Serial.printf("[catalog] %u subjects, %u bytes\n", static_cast<unsigned>(meta.count),
                      static_cast<unsigned>(meta.bytes));
        return true;
    }
    clearFiles(sd);
    gCatalog.open = true;
    gCatalog.accountKey = accountKey;
    gCatalog.meta = CatalogMeta{};
    gCatalog.meta.accountKey = accountKey;
    if (!writeMeta(sd, gCatalog.meta)) {
        gCatalog = CatalogState{};
        return false;
    }
    Serial.printf("[catalog] created for account %08x\n", static_cast<unsigned>(accountKey));
    return true;
}

bool catalogReady() { return gCatalog.open; }
size_t catalogCount() { return gCatalog.open ? gCatalog.meta.count : 0; }
uint32_t catalogBytes() { return gCatalog.open ? gCatalog.meta.bytes : 0; }

bool catalogHas(fs::FS& sd, uint32_t subjectId) {
    if (!gCatalog.open || subjectId == 0) return false;
    char path[64];
    subjectPath(subjectId, path, sizeof path);
    return sd.exists(path);
}

bool catalogLoad(fs::FS& sd, uint32_t subjectId, Subject* out) {
    if (!gCatalog.open || subjectId == 0 || !out) return false;
    char path[64];
    subjectPath(subjectId, path, sizeof path);
    File file = sd.open(path, "r");
    if (!file) return false;
    uint8_t header[kCatalogSubjectHeaderBytes];
    size_t got = 0;
    while (got < sizeof header) {
        const int n = file.read(header + got, sizeof header - got);
        if (n <= 0) break;
        got += static_cast<size_t>(n);
    }
    if (got != sizeof header || std::memcmp(header, kSubjectMagic, 4) != 0 ||
        get16(header + 4) != kVersion || get32(header + 6) != gCatalog.accountKey) {
        file.close();
        return false;
    }
    const uint16_t payloadBytes = get16(header + 14);
    const uint32_t wantCrc = get32(header + 16);
    std::vector<uint8_t> payload(payloadBytes);
    size_t readBytes = 0;
    while (readBytes < payloadBytes) {
        const int n = file.read(payload.data() + readBytes, payloadBytes - readBytes);
        if (n <= 0) break;
        readBytes += static_cast<size_t>(n);
    }
    file.close();
    if (readBytes != payloadBytes || crc32(payload.data(), payload.size()) != wantCrc) return false;
    if (!subjectDecodePayload(payload.data(), payload.size(), out)) return false;

    const uint32_t now = nowSeconds();
    if (now) {
        File touch = sd.open(path, "r+");
        if (touch) {
            uint8_t stamp[4];
            put32(stamp, now);
            if (touch.seek(10)) touch.write(stamp, sizeof stamp);
            touch.close();
        }
    }
    return true;
}

bool catalogStore(fs::FS& sd, uint32_t subjectId, const Subject& subject) {
    if (!gCatalog.open || subjectId == 0) return false;
    std::vector<uint8_t> payload;
    if (!subjectEncodePayload(subject, &payload) || payload.size() > 0xFFFFu) {
        Serial.println("[catalog] subject encode failed");
        return false;
    }
    char path[64];
    subjectPath(subjectId, path, sizeof path);
    size_t oldBytes = 0;
    if (sd.exists(path)) {
        File old = sd.open(path, "r");
        if (old) {
            oldBytes = static_cast<size_t>(old.size());
            old.close();
        }
    }
    const size_t fileBytes = kCatalogSubjectHeaderBytes + payload.size();
    uint8_t header[kCatalogSubjectHeaderBytes];
    std::memcpy(header, kSubjectMagic, 4);
    put16(header + 4, kVersion);
    put32(header + 6, gCatalog.accountKey);
    put32(header + 10, nowSeconds());
    put16(header + 14, static_cast<uint16_t>(payload.size()));
    put32(header + 16, crc32(payload.data(), payload.size()));

    File file = sd.open(path, "w");
    if (!file) return false;
    bool ok = file.write(header, sizeof header) == sizeof header;
    if (ok && !payload.empty()) ok = file.write(payload.data(), payload.size()) == payload.size();
    file.close();
    if (!ok) {
        sd.remove(path);
        return false;
    }

    if (oldBytes == 0 && gCatalog.meta.count < 0xFFFF) ++gCatalog.meta.count;
    gCatalog.meta.bytes = static_cast<uint32_t>(
        gCatalog.meta.bytes > oldBytes ? gCatalog.meta.bytes - oldBytes + fileBytes : fileBytes);
    gCatalog.meta.savedAt = nowSeconds();
    if (!writeMeta(sd, gCatalog.meta)) {
        Serial.println("[catalog] meta write failed");
        return false;
    }
    if (catalogNeedsEvict(gCatalog.meta.count, gCatalog.meta.bytes, 0))
        catalogPruneToCap(sd);
    return true;
}

bool catalogPruneToCap(fs::FS& sd) {
    if (!gCatalog.open) return false;
    if (!catalogNeedsEvict(gCatalog.meta.count, gCatalog.meta.bytes, 0)) return true;
    std::vector<uint32_t> ids;
    if (!listIds(sd, &ids)) return false;
    std::vector<EvictEntry> entries;
    entries.reserve(ids.size());
    for (uint32_t id : ids) {
        SubjectHeader header;
        if (!readSubjectHeader(sd, id, &header)) continue;
        entries.push_back({id, header.lastUsed,
                           kCatalogSubjectHeaderBytes + static_cast<size_t>(header.payloadBytes)});
    }
    std::sort(entries.begin(), entries.end(), [](const EvictEntry& a, const EvictEntry& b) {
        if (a.lastUsed != b.lastUsed) return a.lastUsed < b.lastUsed;
        return a.subjectId < b.subjectId;
    });
    for (const EvictEntry& entry : entries) {
        if (!catalogNeedsEvict(gCatalog.meta.count, gCatalog.meta.bytes, 0)) break;
        char path[64];
        subjectPath(entry.subjectId, path, sizeof path);
        if (!sd.remove(path)) continue;
        if (gCatalog.meta.count) --gCatalog.meta.count;
        gCatalog.meta.bytes =
            gCatalog.meta.bytes > entry.bytes ? gCatalog.meta.bytes - entry.bytes : 0;
        Serial.printf("[catalog] evicted %lu\n",
                      static_cast<unsigned long>(entry.subjectId));
    }
    gCatalog.meta.savedAt = nowSeconds();
    return writeMeta(sd, gCatalog.meta);
}

void catalogClear(fs::FS& sd) {
    clearFiles(sd);
    gCatalog = CatalogState{};
    Serial.println("[catalog] cleared");
}

}  // namespace kk

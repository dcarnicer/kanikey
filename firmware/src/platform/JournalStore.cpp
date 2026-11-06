#include "platform/JournalStore.h"

#include <LittleFS.h>

#include <algorithm>
#include <cstring>

#include "wk/Crc32.h"

namespace kk {

namespace {
constexpr const char* kPath = "/state/journal.bin";
constexpr const char* kMagic = "KKJR";
constexpr uint16_t kVersion = 1;

struct JournalState {
    bool open = false;
    uint32_t accountKey = 0;
    uint32_t next = 0;
    uint32_t count = 0;
};

JournalState gJournal;

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

bool readExact(File& file, uint8_t* data, size_t n) {
    size_t done = 0;
    while (done < n) {
        const int r = file.read(data + done, n - done);
        if (r <= 0) return false;
        done += static_cast<size_t>(r);
    }
    return true;
}

void makeHeader(uint8_t* header) {
    std::memset(header, 0, kJournalHeaderBytes);
    std::memcpy(header, kMagic, 4);
    put16(header + 4, kVersion);
    put16(header + 6, static_cast<uint16_t>(kJournalRecordBytes));
    put16(header + 8, static_cast<uint16_t>(kJournalRecordsPerBlock));
    put16(header + 10, static_cast<uint16_t>(kJournalBlocks));
    put32(header + 12, gJournal.accountKey);
    put32(header + 16, gJournal.next);
    put32(header + 20, gJournal.count);
    put32(header + 28, crc32(header, 28));
}

bool parseHeader(const uint8_t* header, uint32_t accountKey) {
    if (std::memcmp(header, kMagic, 4) != 0) return false;
    if (get16(header + 4) != kVersion) return false;
    if (get16(header + 6) != kJournalRecordBytes) return false;
    if (get16(header + 8) != kJournalRecordsPerBlock) return false;
    if (get16(header + 10) != kJournalBlocks) return false;
    if (get32(header + 28) != crc32(header, 28)) return false;
    if (get32(header + 12) != accountKey) return false;
    const uint32_t next = get32(header + 16);
    const uint32_t count = get32(header + 20);
    if (next >= kJournalMaxRecords || count > kJournalMaxRecords) return false;
    gJournal.accountKey = accountKey;
    gJournal.next = next;
    gJournal.count = count;
    return true;
}

uint32_t blockOffset(uint32_t index) {
    return kJournalHeaderBytes + (index / kJournalRecordsPerBlock) * kJournalBlockBytes;
}

bool writeHeader(File& file) {
    uint8_t header[kJournalHeaderBytes];
    makeHeader(header);
    return file.seek(0) && file.write(header, kJournalHeaderBytes) == kJournalHeaderBytes;
}

bool createJournal(uint32_t accountKey) {
    LittleFS.mkdir("/state");
    File file = LittleFS.open(kPath, "w");
    if (!file) return false;
    gJournal = JournalState{};
    gJournal.accountKey = accountKey;
    bool ok = writeHeader(file);
    uint8_t zero[512] = {};
    size_t remaining = kJournalBlocks * kJournalBlockBytes;
    while (ok && remaining > 0) {
        const size_t n = std::min<size_t>(remaining, sizeof zero);
        ok = file.write(zero, n) == n;
        remaining -= n;
    }
    file.close();
    if (!ok) {
        LittleFS.remove(kPath);
        gJournal = JournalState{};
        return false;
    }
    gJournal.open = true;
    Serial.printf("[journal] created for account %08x\n", static_cast<unsigned>(accountKey));
    return true;
}
}  // namespace

bool journalOpen(uint32_t accountKey, bool create) {
    if (accountKey == 0) return false;
    if (gJournal.open && gJournal.accountKey == accountKey) return true;
    gJournal = JournalState{};
    File file = LittleFS.open(kPath, "r");
    if (file) {
        uint8_t header[kJournalHeaderBytes];
        const bool gotHeader = readExact(file, header, sizeof header);
        file.close();
        if (gotHeader && parseHeader(header, accountKey)) {
            gJournal.open = true;
            Serial.printf("[journal] %u records\n", static_cast<unsigned>(gJournal.count));
            return true;
        }
    }
    if (!create) return false;
    return createJournal(accountKey);
}

bool journalReady() { return gJournal.open; }
uint32_t journalCount() { return gJournal.open ? gJournal.count : 0; }

uint32_t journalAppend(const JournalRecord& record) {
    if (!gJournal.open) return 0;
    const uint32_t index = gJournal.next;
    const uint32_t offset = blockOffset(index) + journalSlot(index) * kJournalRecordBytes;
    uint8_t encoded[kJournalRecordBytes];
    journalEncodeRecord(record, encoded);

    File file = LittleFS.open(kPath, "r+");
    if (!file) return 0;
    bool ok = file.seek(offset) && file.write(encoded, sizeof encoded) == sizeof encoded;
    if (ok) {
        // Refresh the block trailer after every append. A power cut before this
        // write costs only the current block, never the whole file.
        const uint32_t base = blockOffset(index);
        uint8_t block[kJournalBlockBytes];
        ok = file.seek(base) && readExact(file, block, sizeof block);
        if (ok) {
            put32(block + kJournalRecordsPerBlock * kJournalRecordBytes,
                  index / kJournalRecordsPerBlock);
            put32(block + kJournalRecordsPerBlock * kJournalRecordBytes + 4,
                  crc32(block, kJournalRecordsPerBlock * kJournalRecordBytes));
            ok = file.seek(base + kJournalRecordsPerBlock * kJournalRecordBytes) &&
                 file.write(block + kJournalRecordsPerBlock * kJournalRecordBytes, 8) == 8;
        }
    }
    if (ok) {
        const uint32_t oldNext = gJournal.next;
        const uint32_t oldCount = gJournal.count;
        gJournal.next = journalNextIndex(index);
        if (gJournal.count < kJournalMaxRecords) ++gJournal.count;
        ok = writeHeader(file);
        if (!ok) {
            gJournal.next = oldNext;
            gJournal.count = oldCount;
        }
    }
    file.close();
    if (!ok) {
        Serial.println("[journal] append failed");
        return 0;
    }
    return index + 1;
}

bool journalPatchSrsAfter(uint32_t handle, uint8_t stage) {
    if (!gJournal.open || handle == 0 || handle > kJournalMaxRecords) return false;
    const uint32_t index = handle - 1;
    if (gJournal.count < kJournalMaxRecords && index >= gJournal.count) return false;
    const uint32_t base = blockOffset(index);
    File file = LittleFS.open(kPath, "r+");
    if (!file) return false;
    uint8_t block[kJournalBlockBytes];
    bool ok = file.seek(base) && readExact(file, block, sizeof block);
    JournalRecord records[kJournalRecordsPerBlock];
    uint32_t seq = 0;
    ok = ok && journalDecodeBlock(block, records, &seq);
    if (ok) {
        journalPatchSrsAfter(&records[journalSlot(index)], stage);
        journalEncodeBlock(records, seq, block);
        ok = file.seek(base) && file.write(block, kJournalBlockBytes) == kJournalBlockBytes;
    }
    file.close();
    if (!ok) Serial.println("[journal] SRS patch failed");
    return ok;
}

bool journalReadAt(uint32_t index, JournalRecord* out) {
    if (!gJournal.open || index >= gJournal.count || !out) return false;
    const uint32_t offset = blockOffset(index) + journalSlot(index) * kJournalRecordBytes;
    File file = LittleFS.open(kPath, "r");
    if (!file) return false;
    uint8_t encoded[kJournalRecordBytes];
    const bool ok = file.seek(offset) && readExact(file, encoded, sizeof encoded) &&
                    journalDecodeRecord(encoded, out);
    file.close();
    return ok;
}

bool journalForEach(JournalVisitor visitor, void* user) {
    if (!gJournal.open || !visitor || gJournal.count == 0) return true;
    File file = LittleFS.open(kPath, "r");
    if (!file) return false;
    uint8_t block[kJournalBlockBytes];
    JournalRecord records[kJournalRecordsPerBlock];
    bool ok = true;
    for (uint32_t blockIndex = 0; blockIndex * kJournalRecordsPerBlock < gJournal.count;
         ++blockIndex) {
        const uint32_t base = blockOffset(blockIndex * kJournalRecordsPerBlock);
        if (!file.seek(base) || !readExact(file, block, sizeof block)) {
            ok = false;
            break;
        }
        uint32_t seq = 0;
        if (!journalDecodeBlock(block, records, &seq)) {
            Serial.printf("[journal] bad block %u; stopping\n",
                          static_cast<unsigned>(blockIndex));
            break;
        }
        const uint32_t start = blockIndex * kJournalRecordsPerBlock;
        const uint32_t count = std::min<uint32_t>(kJournalRecordsPerBlock, gJournal.count - start);
        for (uint32_t i = 0; i < count; ++i)
            if (!visitor(records[i], user)) {
                file.close();
                return true;
            }
    }
    file.close();
    return ok;
}

void journalClear() {
    LittleFS.remove(kPath);
    gJournal = JournalState{};
    Serial.println("[journal] cleared");
}

}  // namespace kk

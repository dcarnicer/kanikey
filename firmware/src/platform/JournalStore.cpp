#include "platform/JournalStore.h"

#include <Arduino.h>
#include <LittleFS.h>

#include <algorithm>
#include <cstring>

#include "wk/Crc32.h"

namespace kk {

namespace {
constexpr const char* kPath = "/state/journal.bin";
constexpr const char* kTmp = "/state/journal.tmp";
// Records move in 16-record chunks (256 B) for reads, rewrites and migration.
constexpr uint32_t kChunkRecords = 16;
constexpr size_t kChunkBytes = kChunkRecords * kJournalRecordBytes;

struct JournalState {
    bool open = false;
    // An append failed or the file ends in a torn record: rewrite it before the
    // next append so new records never follow a partial one.
    bool needsRewrite = false;
    uint32_t accountKey = 0;
    uint32_t base = 0;   // ordinal of record 0; a handle is ordinal + 1
    uint32_t count = 0;  // complete records in the file
};

JournalState gJournal;

bool readExact(File& file, uint8_t* data, size_t n) {
    size_t done = 0;
    while (done < n) {
        const int r = file.read(data + done, n - done);
        if (r <= 0) return false;
        done += static_cast<size_t>(r);
    }
    return true;
}

// Record i lives right after the header and the i records before it.
uint32_t recordOffset(uint32_t index) {
    return static_cast<uint32_t>(kJournalHeaderBytes) + index * static_cast<uint32_t>(kJournalRecordBytes);
}

bool writeHeader(File& file, uint32_t accountKey, uint32_t base) {
    uint8_t header[kJournalHeaderBytes];
    journalEncodeHeader(accountKey, base, header);
    return file.write(header, sizeof header) == sizeof header;
}

// Writes the header and records [drop, count) to kTmp, then renames kTmp over
// the journal. LittleFS rename replaces the target in one step: a power cut
// leaves the old file or the new one, never a half-written journal.
bool rewriteFile(uint32_t drop) {
    if (drop > gJournal.count) return false;
    File src = LittleFS.open(kPath, "r");
    if (!src) return false;
    File dst = LittleFS.open(kTmp, "w");
    bool ok = dst && writeHeader(dst, gJournal.accountKey, gJournal.base + drop);
    uint8_t chunk[kChunkBytes];
    uint32_t index = drop;
    uint32_t left = gJournal.count - drop;
    while (ok && left > 0) {
        const uint32_t n = std::min<uint32_t>(left, kChunkRecords);
        const size_t bytes = static_cast<size_t>(n) * kJournalRecordBytes;
        ok = src.seek(recordOffset(index)) && readExact(src, chunk, bytes) &&
             dst.write(chunk, bytes) == bytes;
        index += n;
        left -= n;
    }
    src.close();
    if (dst) dst.close();
    if (!ok || !LittleFS.rename(kTmp, kPath)) {
        LittleFS.remove(kTmp);
        return false;
    }
    gJournal.base += drop;
    gJournal.count -= drop;
    gJournal.needsRewrite = false;
    return true;
}

bool createJournal(uint32_t accountKey) {
    LittleFS.mkdir("/state");
    File file = LittleFS.open(kTmp, "w");
    const bool ok = file && writeHeader(file, accountKey, 0);
    if (file) file.close();
    if (!ok || !LittleFS.rename(kTmp, kPath)) {
        LittleFS.remove(kTmp);
        return false;
    }
    gJournal = JournalState{};
    gJournal.open = true;
    gJournal.accountKey = accountKey;
    Serial.printf("[journal] created for account %08x\n", static_cast<unsigned>(accountKey));
    return true;
}

// Copies a v1 ring into a v2 file in review order (oldest first). Blocks whose
// CRC fails are skipped, as the v1 reader stopped at them. Runs once per file.
bool migrateV1(const JournalHeader& h, uint32_t accountKey) {
    if (h.perBlock != kJournalRecordsPerBlock || h.blocks != kJournalBlocks) return false;
    const uint32_t next = h.field16;
    const uint32_t count = h.field20;
    if (next >= kJournalMaxRecords || count > kJournalMaxRecords) return false;
    Serial.printf("[journal] migrating v1 file: %u records\n", static_cast<unsigned>(count));
    File src = LittleFS.open(kPath, "r");
    File dst = LittleFS.open(kTmp, "w");
    bool ok = src && dst && writeHeader(dst, accountKey, 0);
    uint8_t block[kJournalBlockBytes];
    uint8_t chunk[kChunkBytes];
    size_t fill = 0;
    uint32_t loaded = 0xFFFFFFFFu;
    bool blockOk = false;
    uint32_t written = 0;
    uint32_t skipped = 0;
    for (uint32_t i = 0; ok && i < count; ++i) {
        const uint32_t slot = journalV1OldestSlot(next, count, i);
        const uint32_t blockIndex = slot / static_cast<uint32_t>(kJournalRecordsPerBlock);
        if (blockIndex != loaded) {
            loaded = blockIndex;
            blockOk = src.seek(static_cast<uint32_t>(kJournalHeaderBytes) +
                               blockIndex * static_cast<uint32_t>(kJournalBlockBytes)) &&
                      readExact(src, block, sizeof block) && journalBlockValid(block);
            if (!blockOk) Serial.printf("[journal] v1 block %u unreadable; dropped\n",
                                        static_cast<unsigned>(blockIndex));
        }
        if (!blockOk) {
            ++skipped;
            continue;
        }
        const size_t inBlock = (slot % kJournalRecordsPerBlock) * kJournalRecordBytes;
        std::memcpy(chunk + fill, block + inBlock, kJournalRecordBytes);
        fill += kJournalRecordBytes;
        ++written;
        if (fill == kChunkBytes) {
            ok = dst.write(chunk, fill) == fill;
            fill = 0;
        }
    }
    if (ok && fill > 0) ok = dst.write(chunk, fill) == fill;
    if (src) src.close();
    if (dst) dst.close();
    if (!ok || !LittleFS.rename(kTmp, kPath)) {
        LittleFS.remove(kTmp);
        Serial.println("[journal] v1 migration failed");
        return false;
    }
    gJournal = JournalState{};
    gJournal.open = true;
    gJournal.accountKey = accountKey;
    gJournal.count = written;
    Serial.printf("[journal] migrated to v2: %u records, %u skipped\n",
                  static_cast<unsigned>(written), static_cast<unsigned>(skipped));
    return true;
}
}  // namespace

bool journalOpen(uint32_t accountKey, bool create) {
    if (accountKey == 0) return false;
    if (gJournal.open && gJournal.accountKey == accountKey) return true;
    gJournal = JournalState{};
    // A tmp file left by an interrupted rewrite or migration is never the journal.
    if (LittleFS.exists(kTmp)) LittleFS.remove(kTmp);
    File file = LittleFS.open(kPath, "r");
    if (file) {
        uint8_t raw[kJournalHeaderBytes];
        JournalHeader header;
        const bool gotHeader =
            readExact(file, raw, sizeof raw) && journalDecodeHeader(raw, &header);
        const uint64_t bytes = file.size();
        file.close();
        if (gotHeader && header.accountKey == accountKey) {
            if (header.version == kJournalFormatV2) {
                gJournal.open = true;
                gJournal.accountKey = accountKey;
                gJournal.base = header.field16;
                gJournal.count = journalRecordsInFile(bytes);
                gJournal.needsRewrite = journalTornTail(bytes);
                Serial.printf("[journal] %u records\n", static_cast<unsigned>(gJournal.count));
                return true;
            }
            if (header.version == kJournalFormatV1 && migrateV1(header, accountKey)) return true;
        }
    }
    if (!create) return false;
    return createJournal(accountKey);
}

bool journalReady() { return gJournal.open; }
uint32_t journalCount() { return gJournal.open ? gJournal.count : 0; }

uint32_t journalAppend(const JournalRecord& record) {
    if (!gJournal.open) return 0;
    // A full file drops its oldest batch first (rare: once per kJournalMaxRecords -
    // kJournalKeepRecords reviews). A failed append or torn tail also rewrites.
    const uint32_t drop = journalCompactDrop(gJournal.count);
    if (drop > 0 || gJournal.needsRewrite) {
        const uint32_t started = millis();
        if (!rewriteFile(drop)) {
            Serial.println("[journal] rewrite failed");
            return 0;
        }
        Serial.printf("[journal] rewrote %u records, dropped %u, %u ms\n",
                      static_cast<unsigned>(gJournal.count), static_cast<unsigned>(drop),
                      static_cast<unsigned>(millis() - started));
    }
    uint8_t encoded[kJournalRecordBytes];
    journalEncodeRecord(record, encoded);
    // The write starts exactly at the end of the file, so LittleFS only extends
    // the last block: no tail copy.
    File file = LittleFS.open(kPath, "r+");
    const bool ok = file && file.seek(recordOffset(gJournal.count)) &&
                    file.write(encoded, sizeof encoded) == sizeof encoded;
    if (file) file.close();
    if (!ok) {
        gJournal.needsRewrite = true;
        Serial.println("[journal] append failed");
        return 0;
    }
    const uint32_t ordinal = gJournal.base + gJournal.count;
    ++gJournal.count;
    return ordinal + 1;
}

bool journalPatchSrsAfter(uint32_t handle, uint8_t stage) {
    if (!gJournal.open || handle == 0) return false;
    const uint32_t ordinal = handle - 1;
    // Handles of records a compaction dropped no longer exist.
    if (ordinal < gJournal.base || ordinal - gJournal.base >= gJournal.count) return false;
    const uint32_t index = ordinal - gJournal.base;
    File file = LittleFS.open(kPath, "r+");
    if (!file) return false;
    uint8_t encoded[kJournalRecordBytes];
    JournalRecord record;
    bool ok = file.seek(recordOffset(index)) && readExact(file, encoded, sizeof encoded) &&
              journalDecodeRecord(encoded, &record);
    if (ok) {
        journalPatchSrsAfter(&record, stage);
        journalEncodeRecord(record, encoded);
        // Only bytes 12-13 (the SRS nibbles and flags) change.
        ok = file.seek(recordOffset(index) + 12) && file.write(encoded + 12, 2) == 2;
    }
    file.close();
    if (!ok) Serial.println("[journal] SRS patch failed");
    return ok;
}

bool journalReadAt(uint32_t index, JournalRecord* out) {
    if (!gJournal.open || index >= gJournal.count || !out) return false;
    File file = LittleFS.open(kPath, "r");
    if (!file) return false;
    uint8_t encoded[kJournalRecordBytes];
    const bool ok = file.seek(recordOffset(index)) && readExact(file, encoded, sizeof encoded) &&
                    journalDecodeRecord(encoded, out);
    file.close();
    return ok;
}

bool journalForEach(JournalVisitor visitor, void* user) {
    if (!gJournal.open || !visitor || gJournal.count == 0) return true;
    File file = LittleFS.open(kPath, "r");
    if (!file) return false;
    uint8_t chunk[kChunkBytes];
    bool ok = true;
    uint32_t index = 0;
    while (ok && index < gJournal.count) {
        const uint32_t n = std::min<uint32_t>(gJournal.count - index, kChunkRecords);
        ok = file.seek(recordOffset(index)) &&
             readExact(file, chunk, static_cast<size_t>(n) * kJournalRecordBytes);
        for (uint32_t i = 0; ok && i < n; ++i) {
            JournalRecord record;
            journalDecodeRecord(chunk + i * kJournalRecordBytes, &record);
            if (!visitor(record, user)) {
                file.close();
                return true;
            }
        }
        index += n;
    }
    file.close();
    return ok;
}

void journalClear() {
    LittleFS.remove(kPath);
    LittleFS.remove(kTmp);
    gJournal = JournalState{};
    Serial.println("[journal] cleared");
}

}  // namespace kk

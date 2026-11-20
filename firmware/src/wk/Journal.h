#pragma once
// Device-local review journal: completed reviews survive reboot and feed the
// Stats history. Pure C++; persistence is platform/JournalStore.
#include <cstddef>
#include <cstdint>

namespace kk {

constexpr size_t kJournalRecordBytes = 16;
constexpr size_t kJournalRecordsPerBlock = 32;
constexpr size_t kJournalBlocks = 125;
constexpr size_t kJournalMaxRecords = kJournalRecordsPerBlock * kJournalBlocks;
constexpr size_t kJournalBlockBytes = kJournalRecordsPerBlock * kJournalRecordBytes + 8;
constexpr size_t kJournalHeaderBytes = 32;

struct JournalRecord {
    uint32_t completedAt = 0;  // UTC epoch; 0 when the clock was not trusted
    uint32_t subjectId = 0;
    uint32_t assignmentId = 0;
    uint8_t srsBefore = 0;
    uint8_t srsAfter = 0;
    bool srsAfterKnown = false;
    bool offline = false;
    bool practice = false;
    // REV-04: 'C' marked correct, 'A' asked again later, 0 none. Local only.
    char wrongAnswerOverride = 0;
    bool lessonStarted = false;
    uint8_t incorrectMeaning = 0;
    uint8_t incorrectReading = 0;
};

void journalEncodeRecord(const JournalRecord& record, uint8_t out[kJournalRecordBytes]);
bool journalDecodeRecord(const uint8_t in[kJournalRecordBytes], JournalRecord* out);

// A block is 32 records plus a trailer { seq, crc32(records) }.
void journalEncodeBlock(const JournalRecord* records, uint32_t seq,
                        uint8_t out[kJournalBlockBytes]);
bool journalDecodeBlock(const uint8_t in[kJournalBlockBytes], JournalRecord* records,
                        uint32_t* seq);

uint32_t journalNextIndex(uint32_t index);
uint32_t journalSlot(uint32_t index);
void journalPatchSrsAfter(JournalRecord* record, uint8_t stage);

// Format v2 (the device file since the append-only change): a 32-byte header
// written once per file, then 16-byte records appended in review order. A
// rewrite (compaction or a torn-tail repair) copies the newest records to a
// new file, so nothing is patched in place mid-file: LittleFS copies the whole
// tail after any mid-file write, which made the v1 ring cost seconds per review.
// The v1 ring above is read only to migrate an existing file.
constexpr uint16_t kJournalFormatV1 = 1;
constexpr uint16_t kJournalFormatV2 = 2;
// When the file holds kJournalMaxRecords, the oldest records go in one batch
// and the newest kJournalKeepRecords stay.
constexpr size_t kJournalKeepRecords = 3000;

// Fields shared by v1 and v2 headers. v1: field16 = next index, field20 =
// record count. v2: field16 = ordinal of the first record in the file; the
// ordinal of record i is field16 + i, so a handle survives a compaction.
struct JournalHeader {
    uint16_t version = 0;
    uint16_t recordBytes = 0;
    uint16_t perBlock = 0;  // v1 only
    uint16_t blocks = 0;    // v1 only
    uint32_t accountKey = 0;
    uint32_t field16 = 0;
    uint32_t field20 = 0;
};

void journalEncodeHeader(uint32_t accountKey, uint32_t baseOrdinal,
                         uint8_t out[kJournalHeaderBytes]);
// False on a bad magic, a bad CRC or a record size other than 16 bytes.
bool journalDecodeHeader(const uint8_t in[kJournalHeaderBytes], JournalHeader* out);
// Complete records after the header; a partial trailing record does not count.
uint32_t journalRecordsInFile(uint64_t fileBytes);
// True when the file size is not header + whole records (a torn append).
bool journalTornTail(uint64_t fileBytes);
// Oldest records to drop before the next append: 0 until the file is full.
uint32_t journalCompactDrop(uint32_t count);
// The v1 slot holding the i-th oldest record (0 = oldest).
uint32_t journalV1OldestSlot(uint32_t next, uint32_t count, uint32_t i);
// CRC check only, for v1 blocks that are read but not decoded.
bool journalBlockValid(const uint8_t in[kJournalBlockBytes]);

}  // namespace kk

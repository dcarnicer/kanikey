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
    bool wrongAnswerOverride = false;
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

}  // namespace kk

#include "wk/Journal.h"

#include <algorithm>
#include <cstring>

#include "wk/Crc32.h"

namespace kk {

namespace {
constexpr uint8_t kFlagOffline = 1u << 0;
constexpr uint8_t kFlagPractice = 1u << 1;
constexpr uint8_t kFlagWrongOverride = 1u << 2;
constexpr uint8_t kFlagSrsAfterKnown = 1u << 3;
constexpr uint8_t kFlagLessonStarted = 1u << 4;
// REV-04: bit 2 marks an override, bit 5 selects A over C. Bit 2 alone stays
// readable as the old boolean value.
constexpr uint8_t kFlagWrongOverrideAskAgain = 1u << 5;
constexpr uint8_t kUnknownStage = 0x0F;

void put32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

uint32_t get32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
}  // namespace

void journalEncodeRecord(const JournalRecord& record, uint8_t out[kJournalRecordBytes]) {
    put32(out, record.completedAt);
    put32(out + 4, record.subjectId);
    put32(out + 8, record.assignmentId);
    const uint8_t after = record.srsAfterKnown ? (record.srsAfter & 0x0F) : kUnknownStage;
    out[12] = static_cast<uint8_t>((record.srsBefore & 0x0F) | (after << 4));
    uint8_t flags = 0;
    if (record.offline) flags |= kFlagOffline;
    if (record.practice) flags |= kFlagPractice;
    if (record.wrongAnswerOverride == 'C') flags |= kFlagWrongOverride;
    if (record.wrongAnswerOverride == 'A')
        flags |= kFlagWrongOverride | kFlagWrongOverrideAskAgain;
    if (record.srsAfterKnown) flags |= kFlagSrsAfterKnown;
    if (record.lessonStarted) flags |= kFlagLessonStarted;
    out[13] = flags;
    out[14] = record.incorrectMeaning;
    out[15] = record.incorrectReading;
}

bool journalDecodeRecord(const uint8_t in[kJournalRecordBytes], JournalRecord* out) {
    if (!out || !in) return false;
    JournalRecord record;
    record.completedAt = get32(in);
    record.subjectId = get32(in + 4);
    record.assignmentId = get32(in + 8);
    record.srsBefore = in[12] & 0x0F;
    const uint8_t after = in[12] >> 4;
    record.srsAfterKnown = (in[13] & kFlagSrsAfterKnown) != 0 && after != kUnknownStage;
    record.srsAfter = record.srsAfterKnown ? after : 0;
    record.offline = (in[13] & kFlagOffline) != 0;
    record.practice = (in[13] & kFlagPractice) != 0;
    record.wrongAnswerOverride =
        (in[13] & kFlagWrongOverride) == 0
            ? 0
            : ((in[13] & kFlagWrongOverrideAskAgain) ? 'A' : 'C');
    record.lessonStarted = (in[13] & kFlagLessonStarted) != 0;
    record.incorrectMeaning = in[14];
    record.incorrectReading = in[15];
    *out = record;
    return true;
}

void journalEncodeBlock(const JournalRecord* records, uint32_t seq,
                        uint8_t out[kJournalBlockBytes]) {
    std::memset(out, 0, kJournalBlockBytes);
    if (records) {
        for (size_t i = 0; i < kJournalRecordsPerBlock; ++i)
            journalEncodeRecord(records[i], out + i * kJournalRecordBytes);
    }
    put32(out + kJournalRecordsPerBlock * kJournalRecordBytes, seq);
    put32(out + kJournalRecordsPerBlock * kJournalRecordBytes + 4,
          crc32(out, kJournalRecordsPerBlock * kJournalRecordBytes));
}

bool journalDecodeBlock(const uint8_t in[kJournalBlockBytes], JournalRecord* records,
                        uint32_t* seq) {
    if (!in || !records) return false;
    const uint32_t want = get32(in + kJournalRecordsPerBlock * kJournalRecordBytes + 4);
    if (crc32(in, kJournalRecordsPerBlock * kJournalRecordBytes) != want) return false;
    for (size_t i = 0; i < kJournalRecordsPerBlock; ++i)
        journalDecodeRecord(in + i * kJournalRecordBytes, &records[i]);
    if (seq) *seq = get32(in + kJournalRecordsPerBlock * kJournalRecordBytes);
    return true;
}

uint32_t journalNextIndex(uint32_t index) {
    return index + 1u >= kJournalMaxRecords ? 0 : index + 1u;
}

uint32_t journalSlot(uint32_t index) { return index % kJournalRecordsPerBlock; }

void journalPatchSrsAfter(JournalRecord* record, uint8_t stage) {
    if (!record) return;
    record->srsAfter = stage & 0x0F;
    record->srsAfterKnown = true;
}

namespace {
constexpr size_t kHeaderMagicBytes = 4;
constexpr uint32_t kHeaderCrcAt = 28;
}  // namespace

void journalEncodeHeader(uint32_t accountKey, uint32_t baseOrdinal,
                         uint8_t out[kJournalHeaderBytes]) {
    std::memset(out, 0, kJournalHeaderBytes);
    std::memcpy(out, "KKJR", kHeaderMagicBytes);
    out[4] = static_cast<uint8_t>(kJournalFormatV2);
    out[5] = static_cast<uint8_t>(kJournalFormatV2 >> 8);
    out[6] = static_cast<uint8_t>(kJournalRecordBytes);
    out[7] = static_cast<uint8_t>(kJournalRecordBytes >> 8);
    put32(out + 12, accountKey);
    put32(out + 16, baseOrdinal);
    put32(out + kHeaderCrcAt, crc32(out, kHeaderCrcAt));
}

bool journalDecodeHeader(const uint8_t in[kJournalHeaderBytes], JournalHeader* out) {
    if (!in || !out) return false;
    if (std::memcmp(in, "KKJR", kHeaderMagicBytes) != 0) return false;
    if (get32(in + kHeaderCrcAt) != crc32(in, kHeaderCrcAt)) return false;
    JournalHeader h;
    h.version = static_cast<uint16_t>(in[4] | (in[5] << 8));
    h.recordBytes = static_cast<uint16_t>(in[6] | (in[7] << 8));
    h.perBlock = static_cast<uint16_t>(in[8] | (in[9] << 8));
    h.blocks = static_cast<uint16_t>(in[10] | (in[11] << 8));
    h.accountKey = get32(in + 12);
    h.field16 = get32(in + 16);
    h.field20 = get32(in + 20);
    if (h.recordBytes != kJournalRecordBytes) return false;
    if (h.version != kJournalFormatV1 && h.version != kJournalFormatV2) return false;
    *out = h;
    return true;
}

uint32_t journalRecordsInFile(uint64_t fileBytes) {
    if (fileBytes < kJournalHeaderBytes) return 0;
    const uint64_t records = (fileBytes - kJournalHeaderBytes) / kJournalRecordBytes;
    return records > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<uint32_t>(records);
}

bool journalTornTail(uint64_t fileBytes) {
    if (fileBytes < kJournalHeaderBytes) return true;
    return (fileBytes - kJournalHeaderBytes) % kJournalRecordBytes != 0;
}

uint32_t journalCompactDrop(uint32_t count) {
    if (count < kJournalMaxRecords) return 0;
    return count - static_cast<uint32_t>(kJournalKeepRecords);
}

uint32_t journalV1OldestSlot(uint32_t next, uint32_t count, uint32_t i) {
    // Until the ring wraps, slot order is review order; after it wraps the
    // oldest record is the one the next append would overwrite (`next`).
    if (count < kJournalMaxRecords) return i;
    return (next + i) % static_cast<uint32_t>(kJournalMaxRecords);
}

bool journalBlockValid(const uint8_t in[kJournalBlockBytes]) {
    if (!in) return false;
    const uint32_t want = get32(in + kJournalRecordsPerBlock * kJournalRecordBytes + 4);
    return crc32(in, kJournalRecordsPerBlock * kJournalRecordBytes) == want;
}

}  // namespace kk

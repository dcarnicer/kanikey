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
    if (record.wrongAnswerOverride) flags |= kFlagWrongOverride;
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
    record.wrongAnswerOverride = (in[13] & kFlagWrongOverride) != 0;
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

}  // namespace kk

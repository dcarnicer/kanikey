#pragma once
// Persistent review journal on LittleFS: /state/journal.bin (format v2, an
// append-only log; see wk/Journal.h). One account key owns the file. Appends
// write only at the end of the file, so one review costs one small append.
// Pure record/header code is in wk/Journal.h.
#include <cstdint>

#include "wk/Journal.h"

namespace kk {

// Opens the journal for `accountKey`. With create=true a missing/corrupt file
// is recreated; with create=false the call only reads an existing journal.
// A v1 file of the same account is migrated to v2 on first open. False when
// LittleFS is unavailable or the account key is empty.
bool journalOpen(uint32_t accountKey, bool create = true);
bool journalReady();
uint32_t journalCount();

// Appends one record and returns a handle (ordinal + 1), or 0 on failure. A
// handle stays valid until a compaction drops its record.
uint32_t journalAppend(const JournalRecord& record);
bool journalPatchSrsAfter(uint32_t handle, uint8_t stage);
bool journalReadAt(uint32_t index, JournalRecord* out);

// Visits records oldest first. The visitor returns false to stop early; the
// function still returns true.
using JournalVisitor = bool (*)(const JournalRecord& record, void* user);
bool journalForEach(JournalVisitor visitor, void* user);

void journalClear();

}  // namespace kk

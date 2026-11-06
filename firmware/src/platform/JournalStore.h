#pragma once
// Persistent review journal on LittleFS: /state/journal.bin. One account key
// owns the file; blocks carry their own CRC so a torn write loses at most the
// active block. Pure record/block code is in wk/Journal.h.
#include <cstdint>

#include "wk/Journal.h"

namespace kk {

// Opens the journal for `accountKey`. With create=true a missing/corrupt file
// is recreated; with create=false the call only reads an existing journal.
// False when LittleFS is unavailable or the account key is empty.
bool journalOpen(uint32_t accountKey, bool create = true);
bool journalReady();
uint32_t journalCount();

// Appends one record and returns a handle (index + 1), or 0 on failure.
uint32_t journalAppend(const JournalRecord& record);
bool journalPatchSrsAfter(uint32_t handle, uint8_t stage);
bool journalReadAt(uint32_t index, JournalRecord* out);

// Visits records in ring order. Stops at the first bad block. The visitor
// returns false to stop early; the function still returns true.
using JournalVisitor = bool (*)(const JournalRecord& record, void* user);
bool journalForEach(JournalVisitor visitor, void* user);

void journalClear();

}  // namespace kk

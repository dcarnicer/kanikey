#pragma once
// Session cache and SD mount: the snapshot, the lazy review batch and the
// cached summary the Home screen reads.
#include <cstddef>
#include <cstdint>

namespace kk::app {

void clearLevelUpNotice();
uint32_t currentAccountKey();
uint32_t effectiveNow();
void applySummary(uint32_t now);
void pruneBatchToSummary(uint32_t now);
void saveCacheSnapshot();
// True when the subjects reached the card too.
bool saveCacheAll();
// After a fresh batch fetch: its subjects were allocated while the TLS buffers
// were live, scattered between them (a review then had 46 KB free with a 10 KB
// largest block, too little for R, 2026-11-20). Drops the batch and reloads it
// from the card, which lays it out contiguously. Call only after a good save.
void compactCachedBatch();
bool loadCache();
bool loadCachedBatch();
void evictCachedBatch(bool save);
// Drops the completed assignment from the summary and batch. save=false leaves
// the snapshot write to the caller (the review feedback defers it).
void markCacheCompleted(uint32_t subjectId, uint32_t assignmentId, bool save = true);
bool mountSd();
bool cacheIsStale();
void cacheStatusLine(char* out, size_t n);

}  // namespace kk::app

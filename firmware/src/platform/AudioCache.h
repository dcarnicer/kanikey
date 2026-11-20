#pragma once
// Account-scoped vocabulary audio cache on the microSD:
// /kanikey/audio/<subject>-<voice>.<mp3|ogg> plus index.bin.
#include <FS.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "wk/AudioIndex.h"
#include "wk/AudioPlan.h"
#include "wk/Types.h"

namespace kk {

bool audioCacheOpen(fs::FS& sd, uint32_t accountKey);
// AUD-02 batch prefetch: downloads the MP3 clip (preferred voice, then the other
// voice; never OGG) of each vocabulary subject not on the card yet. Stops early
// when `budgetMs` is spent or the largest free block drops below
// `minLargestHeap`. Streams to the card in 4 KB chunks, keeps the index and the
// size cap, and logs one `[audio] batch prefetch` line. Call audioCacheOpen()
// first.
struct AudioPrefetchStats {
    size_t wanted = 0;      // MP3 clips not on the card when the batch arrived
    size_t downloaded = 0;
    uint64_t bytes = 0;
    uint32_t ms = 0;
    const char* stopped = nullptr;  // "budget" or "heap" when the batch ended early
};
AudioPrefetchStats audioCachePrefetch(fs::FS& sd, const std::vector<Subject>& subjects,
                                      uint8_t voice, uint32_t budgetMs, size_t minLargestHeap);
size_t audioCacheCount();
uint64_t audioCacheBytes();
void audioCacheClear(fs::FS& sd);

// AUD-03: the MP3-only selection for one subject. Call audioCacheOpen() first;
// a closed cache returns an applicable but unplayable plan.
AudioPlan audioCachePlan(uint32_t subjectId, SubjectType type, uint8_t voice);
// `/kanikey/audio/<subject>-<voice>.<ext>` for an index entry.
bool audioCachePath(const AudioEntry& entry, char* out, size_t n);

}  // namespace kk

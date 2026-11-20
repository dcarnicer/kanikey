#pragma once
// Account-scoped vocabulary audio cache on the microSD:
// /kanikey/audio/<subject>-<voice>.<mp3|ogg> plus index.bin.
#include <FS.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "wk/AudioIndex.h"
#include "wk/AudioPlan.h"
#include "wk/Types.h"

namespace kk {

bool audioCacheOpen(fs::FS& sd, uint32_t accountKey);

// AUD-02: true when the MP3 clip for this subject and voice is on the card
// (index entry and file). Call audioCacheOpen() first.
bool audioCacheHasMp3(fs::FS& sd, uint32_t subjectId, uint8_t voice);

// AUD-02 queue step: downloads one MP3 clip into the cache. Streams the body to
// the card in 4 KB chunks, drops a stale index entry for the same subject and
// voice, evicts to the 50 MB cap and rewrites the index. Returns the stored
// bytes, or 0 with `error` set. The caller checks the heap and the link first;
// `handshakeSec` bounds the TLS handshake. Call audioCacheOpen() first.
uint32_t audioCacheFetchClip(fs::FS& sd, uint32_t subjectId, uint8_t voice,
                             const std::string& url, uint32_t handshakeSec,
                             std::string* error);
size_t audioCacheCount();
uint64_t audioCacheBytes();
void audioCacheClear(fs::FS& sd);

// AUD-03: the MP3-only selection for one subject. Call audioCacheOpen() first;
// a closed cache returns an applicable but unplayable plan.
AudioPlan audioCachePlan(uint32_t subjectId, SubjectType type, uint8_t voice);
// `/kanikey/audio/<subject>-<voice>.<ext>` for an index entry.
bool audioCachePath(const AudioEntry& entry, char* out, size_t n);

}  // namespace kk

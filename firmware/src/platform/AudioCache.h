#pragma once
// Account-scoped vocabulary audio cache on the microSD:
// /kanikey/audio/<subject>-<voice>.<mp3|ogg> plus index.bin.
#include <FS.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "wk/AudioIndex.h"
#include "wk/Types.h"

namespace kk {

bool audioCacheOpen(fs::FS& sd, uint32_t accountKey);
bool audioCacheSyncSubjects(fs::FS& sd, const std::vector<Subject>& subjects, uint8_t voice,
                            size_t maxClips);
size_t audioCacheCount();
uint64_t audioCacheBytes();
void audioCacheClear(fs::FS& sd);

}  // namespace kk

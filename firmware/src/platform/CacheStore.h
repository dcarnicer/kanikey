#pragma once
// Persistent cache files: the Home/batch snapshot on LittleFS and the subject
// payload on the microSD card. Both use temp + rename with a .bak copy.
#include <FS.h>

#include "wk/Cache.h"

namespace kk {

bool cacheLoadSnapshot(CacheSnapshot* out);
bool cacheSaveSnapshot(const CacheSnapshot& snapshot);
void cacheClearSnapshot();

bool cacheLoadSubjects(fs::FS& sd, SubjectCache* out);
bool cacheSaveSubjects(fs::FS& sd, const SubjectCache& cache);
void cacheClearSubjects(fs::FS& sd);

}  // namespace kk

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
// Encodes the vector directly; avoids copying all Subject strings. The batch's
// component records go in the same file (KKCS v14).
bool cacheSaveSubjects(fs::FS& sd, const CacheMeta& meta, const std::vector<Subject>& subjects,
                       const std::vector<SubjectRef>& components);
bool cacheSaveSubjects(fs::FS& sd, const CacheMeta& meta, const std::vector<Subject>& subjects);
void cacheClearSubjects(fs::FS& sd);

}  // namespace kk

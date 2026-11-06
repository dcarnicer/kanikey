#pragma once
// Account-scoped subject catalog on the microSD card:
// /kanikey/cache/catalog/<id>.kkc, one subject per file, plus meta.bin.
#include <FS.h>

#include <cstddef>
#include <cstdint>

#include "wk/Catalog.h"
#include "wk/Types.h"

namespace kk {

bool catalogOpen(fs::FS& sd, uint32_t accountKey);
bool catalogReady();
size_t catalogCount();
uint32_t catalogBytes();
bool catalogHas(fs::FS& sd, uint32_t subjectId);
bool catalogLoad(fs::FS& sd, uint32_t subjectId, Subject* out);
bool catalogStore(fs::FS& sd, uint32_t subjectId, const Subject& subject);
bool catalogPruneToCap(fs::FS& sd);
void catalogClear(fs::FS& sd);

}  // namespace kk

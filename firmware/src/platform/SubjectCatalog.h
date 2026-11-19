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
// REV-08: reads only the id/type/level prefix of a catalog file. One 26-byte
// read, no LRU touch and no CRC, so a level order can resolve hundreds of due
// subjects without writing to the card. False when the file is missing,
// belongs to another account or is too short.
bool catalogLevel(fs::FS& sd, uint32_t subjectId, uint8_t* level);
bool catalogLoad(fs::FS& sd, uint32_t subjectId, Subject* out);
bool catalogStore(fs::FS& sd, uint32_t subjectId, const Subject& subject);
bool catalogPruneToCap(fs::FS& sd);
void catalogClear(fs::FS& sd);

}  // namespace kk

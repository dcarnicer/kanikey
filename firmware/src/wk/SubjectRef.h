#pragma once
// Compact record for a review batch's component subjects (MEM-20). The Tab
// detail card's components page draws only the glyph and the meaning of each
// component, so the batch keeps one fixed record per component instead of a
// full Subject (384 B inline plus strings, and no heap here). Pure C++: no
// Arduino headers, so the native tests cover it.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "wk/Types.h"

namespace kk {

// characters: at most four 3-byte CJK code points plus the NUL; a longer value
// is cut on a UTF-8 boundary. meaning: the card shows the meaning in about
// 116 px and cuts it to fit, so 27 bytes plus the NUL keeps every shown line.
constexpr size_t kSubjectRefCharsBytes = 12;
constexpr size_t kSubjectRefMeaningBytes = 28;

// 45 bytes of fields, padded to 48 by the 4-byte id. No level, reading or
// mnemonics: the components card does not draw them.
struct SubjectRef {
    uint32_t id = 0;
    SubjectType type = SubjectType::Kanji;
    char characters[kSubjectRefCharsBytes] = {};  // "" for a radical with no character
    char meaning[kSubjectRefMeaningBytes] = {};   // first accepted meaning, else the first
};

// Copies `text` into a `capacity`-byte field and NUL-terminates it. A value that
// does not fit is cut at the last whole UTF-8 code point. Returns the bytes
// written (without the NUL). `capacity` must be at least 1.
size_t copyUtf8Truncated(const std::string& text, char* out, size_t capacity);

// The record for one subject: its id and type, its characters and its meaning
// as the components card shows it.
SubjectRef subjectRefFromSubject(const Subject& s);

// The ids a review batch must fetch as components: each id referenced by the
// reviewed subjects' componentSubjectIds once, skipping ids that are already
// full subjects in `reviewed`.
void collectComponentIds(const std::vector<Subject>& reviewed, std::vector<uint32_t>* out);

// Converts one fetched page of component subjects into records, appending to
// *refs. A subject whose id is in `reviewed` or already in *refs is skipped. The
// page's Subject objects are not kept: the caller can drop the page after this.
void appendComponentRefs(const std::vector<Subject>& page, const std::vector<Subject>& reviewed,
                         std::vector<SubjectRef>* refs);

// The record for `id`, or nullptr when the batch has none.
const SubjectRef* findSubjectRef(const std::vector<SubjectRef>& refs, uint32_t id);

}  // namespace kk

#include "wk/SubjectRef.h"

#include <algorithm>
#include <cstring>

namespace kk {

size_t copyUtf8Truncated(const std::string& text, char* out, size_t capacity) {
    if (capacity == 0) return 0;
    size_t n = text.size() < capacity - 1 ? text.size() : capacity - 1;
    if (n < text.size()) {
        // A continuation byte at the cut means the cut is inside a code point:
        // back up to its lead byte so the field ends on a whole character.
        while (n > 0 && (static_cast<uint8_t>(text[n]) & 0xC0) == 0x80) --n;
    }
    std::memcpy(out, text.data(), n);
    out[n] = '\0';
    return n;
}

SubjectRef subjectRefFromSubject(const Subject& s) {
    SubjectRef ref;
    ref.id = s.id;
    ref.type = s.type;
    copyUtf8Truncated(s.characters, ref.characters, sizeof ref.characters);
    // Same choice as subjectMainMeaning(): the first accepted meaning, else the
    // first one, so the card shows what it showed when the Subject was kept.
    const std::string* meaning = nullptr;
    for (const Meaning& m : s.meanings) {
        if (m.accepted) {
            meaning = &m.text;
            break;
        }
    }
    if (!meaning && !s.meanings.empty()) meaning = &s.meanings[0].text;
    if (meaning) copyUtf8Truncated(*meaning, ref.meaning, sizeof ref.meaning);
    return ref;
}

void collectComponentIds(const std::vector<Subject>& reviewed, std::vector<uint32_t>* out) {
    out->clear();
    for (const Subject& s : reviewed) {
        for (uint32_t id : s.componentSubjectIds) {
            if (std::find(out->begin(), out->end(), id) != out->end()) continue;
            if (std::any_of(reviewed.begin(), reviewed.end(),
                            [id](const Subject& x) { return x.id == id; }))
                continue;
            out->push_back(id);
        }
    }
}

void appendComponentRefs(const std::vector<Subject>& page, const std::vector<Subject>& reviewed,
                         std::vector<SubjectRef>* refs) {
    for (const Subject& s : page) {
        if (findSubjectRef(*refs, s.id)) continue;
        if (std::any_of(reviewed.begin(), reviewed.end(),
                        [&s](const Subject& x) { return x.id == s.id; }))
            continue;
        refs->push_back(subjectRefFromSubject(s));
    }
}

const SubjectRef* findSubjectRef(const std::vector<SubjectRef>& refs, uint32_t id) {
    for (const SubjectRef& r : refs)
        if (r.id == id) return &r;
    return nullptr;
}

}  // namespace kk

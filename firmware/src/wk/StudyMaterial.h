#pragma once
// WaniKani study materials: the user's meaning/reading notes and meaning
// synonyms. They are merged into the cached Subject so the answer checker and
// the detail cards can use them offline. Pure C++.
#include <cstdint>
#include <string>
#include <vector>

#include "wk/Json.h"
#include "wk/Types.h"

namespace kk {

struct StudyMaterial {
    uint32_t subjectId = 0;
    std::string meaningNote;
    std::string readingNote;
    std::vector<std::string> meaningSynonyms;
    bool hidden = false;
};

std::vector<StudyMaterial> parseStudyMaterials(const JsonValue& root);

// Copies notes and synonyms into matching subjects. Hidden materials are
// skipped, and duplicate synonyms are ignored. Returns the subjects changed.
size_t applyStudyMaterials(const std::vector<StudyMaterial>& materials,
                           std::vector<Subject>* subjects);

}  // namespace kk

#pragma once
// Fetch GET /study_materials for the subjects in a review or lesson batch and
// merge the user's synonyms and notes into those subjects. A missing
// study-materials permission must not block the batch.
#include <Arduino.h>

#include <cstdint>
#include <vector>

#include "wk/StudyMaterial.h"
#include "wk/Types.h"

namespace kk {

std::vector<StudyMaterial> fetchStudyMaterials(const String& token,
                                               const std::vector<uint32_t>& subjectIds,
                                               size_t batch = 10);

// Fetches materials for the subjects and applies them. Returns how many
// subjects were changed; failures are logged and leave the subjects untouched.
size_t attachStudyMaterials(const String& token, std::vector<Subject>* subjects);

}  // namespace kk

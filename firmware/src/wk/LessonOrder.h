#pragma once
// WaniKani lesson ordering: level first, then lesson_position. Pure C++.
#include <cstdint>
#include <vector>

#include "wk/Types.h"

namespace kk {

struct LessonCandidate {
    uint32_t assignmentId = 0;
    uint32_t subjectId = 0;
    uint16_t lessonPosition = 0;
    uint8_t level = 0;  // 0 = unknown; kept last
    SubjectType type = SubjectType::Kanji;
};

// Sorts in place by level, lesson_position, assignment id and subject id.
// Unknown levels (0) go after every known level.
void sortLessonCandidates(std::vector<LessonCandidate>* candidates);

}  // namespace kk

#pragma once
// Lesson ordering: WaniKani's level/lesson_position order (the default), a
// type-grouped order and an interleaved order. Pure C++.
#include <cstdint>
#include <vector>

#include "wk/Types.h"

namespace kk {

// L2 "Lesson order" values as stored in NVS (`ls.order`).
enum class LessonOrder : uint8_t {
    WaniKani = 0,    // level, then lesson_position
    TypeGroups = 1,  // radicals, then kanji, then vocabulary
    Interleave = 2,  // round-robin radicals, kanji and vocabulary
};

LessonOrder lessonOrderFromValue(int value);

struct LessonCandidate {
    uint32_t assignmentId = 0;
    uint32_t subjectId = 0;
    uint16_t lessonPosition = 0;
    uint8_t level = 0;  // 0 = unknown; kept last
    SubjectType type = SubjectType::Kanji;
};

// Sorts in place by level, lesson_position, assignment id and subject id.
// Unknown levels (0) go after every known level. This is the WaniKani order.
void sortLessonCandidates(std::vector<LessonCandidate>* candidates);

// Same, using the L2 order. Type groups and the interleave both keep the
// WaniKani order inside each type.
void sortLessonCandidates(std::vector<LessonCandidate>* candidates, LessonOrder order);

}  // namespace kk

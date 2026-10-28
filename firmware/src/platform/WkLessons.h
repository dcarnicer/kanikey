#pragma once
// Fetch a small lesson batch and start an assignment. Lessons use a few
// subjects at a time because their mnemonics make the JSON much larger.
#include <Arduino.h>

#include <cstdint>
#include <vector>

#include "wk/Assignment.h"
#include "wk/SubjectParser.h"

namespace kk {

struct LessonBatch {
    bool ok = false;
    String error;
    std::vector<Assignment> assignments;
    std::vector<Subject> subjects;
};

LessonBatch fetchLessonBatch(const String& token, const std::vector<uint32_t>& lessonSubjectIds,
                             size_t limit = 5);
// Fetch plain subjects by id (lesson components), without mnemonics/details.
std::vector<Subject> fetchSubjectsByIds(const String& token, const std::vector<uint32_t>& ids,
                                        size_t batch = 10);
bool startAssignment(const String& token, uint32_t assignmentId, uint32_t startedAt);

}  // namespace kk

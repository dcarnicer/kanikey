#pragma once
// Fetch a small lesson batch and start an assignment. Lessons use a few
// subjects at a time because their mnemonics make the JSON much larger.
// Study materials (synonyms and notes) are merged into the subjects.
#include <Arduino.h>

#include <cstdint>
#include <vector>

#include "wk/Assignment.h"
#include "wk/SubjectParser.h"

namespace kk {

struct LessonBatch {
    bool ok = false;
    int httpCode = 0;
    String error;
    std::vector<Assignment> assignments;
    std::vector<Subject> subjects;
};

// Lesson assignments for a candidate pool. The caller may pass up to ~60
// summary ids; the request is split into pages so the 32 KB body cap and the
// TLS heap block are respected.
struct LessonAssignments {
    bool ok = false;
    int httpCode = 0;
    String error;
    std::vector<Assignment> assignments;
};
LessonAssignments fetchLessonAssignments(const String& token,
                                         const std::vector<uint32_t>& lessonSubjectIds,
                                         size_t maxCandidates = 60);

struct AssignmentBatch {
    bool ok = false;
    int httpCode = 0;
    String error;
    std::vector<Assignment> assignments;
};
// Assignments for arbitrary subject ids, including started/hidden/burned ones.
AssignmentBatch fetchAssignmentsForSubjects(const String& token,
                                            const std::vector<uint32_t>& subjectIds,
                                            size_t batch = 30);

struct SubjectLevel {
    uint32_t subjectId = 0;
    uint8_t level = 0;
    uint16_t lessonPosition = 0;
};

// Reads only the id, level and lesson_position of each subject, in pages of
// 10, and drops the parsed subjects before the next request. Used to order
// lesson candidates.
struct SubjectLevels {
    bool ok = false;
    int httpCode = 0;
    String error;
    std::vector<SubjectLevel> levels;
};
SubjectLevels fetchSubjectLevels(const String& token, const std::vector<uint32_t>& subjectIds);

// Fetches the full subjects, study materials and component data for the
// already chosen and ordered lesson assignments.
LessonBatch fetchLessonDetails(const String& token, std::vector<Assignment> assignments);

// Fetch plain subjects by id (lesson/review components), without details.
std::vector<Subject> fetchSubjectsByIds(const String& token, const std::vector<uint32_t>& ids,
                                        size_t batch = 10);
// Returns the HTTP status code (0 when no response was received).
int startAssignment(const String& token, uint32_t assignmentId, uint32_t startedAt);

}  // namespace kk

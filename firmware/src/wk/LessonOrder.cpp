#include "wk/LessonOrder.h"

#include <algorithm>

namespace kk {

namespace {

uint16_t levelRank(uint8_t level) {
    return level == 0 ? 0xFFFF : level;
}

}  // namespace

void sortLessonCandidates(std::vector<LessonCandidate>* candidates) {
    if (!candidates) return;
    std::sort(candidates->begin(), candidates->end(),
              [](const LessonCandidate& a, const LessonCandidate& b) {
                  const uint16_t la = levelRank(a.level);
                  const uint16_t lb = levelRank(b.level);
                  if (la != lb) return la < lb;
                  if (a.lessonPosition != b.lessonPosition)
                      return a.lessonPosition < b.lessonPosition;
                  if (a.assignmentId != b.assignmentId)
                      return a.assignmentId < b.assignmentId;
                  return a.subjectId < b.subjectId;
              });
}

}  // namespace kk

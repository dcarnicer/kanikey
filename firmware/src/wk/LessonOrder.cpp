#include "wk/LessonOrder.h"

#include <algorithm>
#include <utility>

namespace kk {

namespace {

uint16_t levelRank(uint8_t level) {
    return level == 0 ? 0xFFFF : level;
}

bool wanikaniLess(const LessonCandidate& a, const LessonCandidate& b) {
    const uint16_t la = levelRank(a.level);
    const uint16_t lb = levelRank(b.level);
    if (la != lb) return la < lb;
    if (a.lessonPosition != b.lessonPosition)
        return a.lessonPosition < b.lessonPosition;
    if (a.assignmentId != b.assignmentId) return a.assignmentId < b.assignmentId;
    return a.subjectId < b.subjectId;
}

uint8_t typeRank(SubjectType type) {
    switch (type) {
        case SubjectType::Radical:
            return 0;
        case SubjectType::Kanji:
            return 1;
        case SubjectType::Vocabulary:
        case SubjectType::KanaVocabulary:
            return 2;
    }
    return 2;
}

}  // namespace

LessonOrder lessonOrderFromValue(int value) {
    switch (value) {
        case 1:
            return LessonOrder::TypeGroups;
        case 2:
            return LessonOrder::Interleave;
        default:
            return LessonOrder::WaniKani;
    }
}

void sortLessonCandidates(std::vector<LessonCandidate>* candidates) {
    sortLessonCandidates(candidates, LessonOrder::WaniKani);
}

void sortLessonCandidates(std::vector<LessonCandidate>* candidates, LessonOrder order) {
    if (!candidates) return;
    std::sort(candidates->begin(), candidates->end(), wanikaniLess);

    if (order == LessonOrder::WaniKani) return;

    // Both alternatives keep the WaniKani order inside a type. A stable sort
    // by the type rank is the grouped order; the interleave walks the three
    // groups round-robin instead.
    std::stable_sort(candidates->begin(), candidates->end(),
                     [](const LessonCandidate& a, const LessonCandidate& b) {
                         return typeRank(a.type) < typeRank(b.type);
                     });
    if (order == LessonOrder::TypeGroups) return;

    std::vector<LessonCandidate> groups[3];
    for (const LessonCandidate& candidate : *candidates)
        groups[typeRank(candidate.type)].push_back(candidate);

    std::vector<LessonCandidate> out;
    out.reserve(candidates->size());
    size_t next[3] = {0, 0, 0};
    bool any = true;
    while (any) {
        any = false;
        for (size_t group = 0; group < 3; ++group) {
            if (next[group] >= groups[group].size()) continue;
            out.push_back(std::move(groups[group][next[group]++]));
            any = true;
        }
    }
    *candidates = std::move(out);
}

}  // namespace kk

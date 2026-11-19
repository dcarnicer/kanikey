#pragma once
// Fetch a small, due review batch: assignments for the summary's subject IDs,
// then their subjects in tiny batches so the DOM stays inside the heap budget.
// Study materials (synonyms and notes) are merged into the subjects.
#include <Arduino.h>

#include <cstdint>
#include <vector>

#include "wk/Assignment.h"
#include "wk/SubjectParser.h"

namespace kk {

struct ReviewBatch {
    bool ok = false;
    int httpCode = 0;
    String error;
    std::vector<Assignment> assignments;
    std::vector<Subject> subjects;
    uint32_t fetchedAt = 0;
};

// REV-08: the assignment metadata for every due subject in the summary, used to
// order the frozen due set before the batch limit. Requests are split into
// pages that fit the 32 KB body buffer and share one keep-alive session.
struct DueAssignments {
    bool ok = false;
    int httpCode = 0;
    String error;
    std::vector<Assignment> assignments;
    size_t requests = 0;
    size_t bytes = 0;
    size_t pageSize = 0;  // final page size after a truncation fallback
};
DueAssignments fetchDueAssignments(const String& token,
                                   const std::vector<uint32_t>& dueSubjectIds, uint32_t now);

ReviewBatch fetchReviewBatch(const String& token, const std::vector<uint32_t>& dueSubjectIds,
                             uint32_t now, size_t limit = 20);

}  // namespace kk

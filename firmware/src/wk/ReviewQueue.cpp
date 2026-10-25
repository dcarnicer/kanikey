#include "wk/ReviewQueue.h"

#include <algorithm>
#include <random>

namespace kk {

namespace {

const Subject* findSubject(const std::vector<Subject>& subjects, uint32_t id) {
    for (const Subject& s : subjects)
        if (s.id == id) return &s;
    return nullptr;
}

bool due(const Assignment& a, uint32_t now) {
    return !a.hidden && !a.pendingReview && a.srsStage >= 1 && a.srsStage <= 8 &&
           a.availableAt != 0 && a.availableAt <= now;
}

}  // namespace

void ReviewQueue::build(const std::vector<Assignment>& assignments,
                        const std::vector<Subject>& subjects, uint32_t now, size_t limit,
                        uint32_t seed) {
    queue_.clear();
    completed_.clear();
    part_ = ReviewPart::None;

    std::vector<SessionItem> items;
    for (const Assignment& a : assignments) {
        if (!due(a, now)) continue;
        const Subject* s = findSubject(subjects, a.subjectId);
        if (!s) continue;
        SessionItem it;
        it.assignmentId = a.id;
        it.subjectId = a.subjectId;
        it.type = a.type;
        it.needsMeaning = true;
        it.needsReading = s->needsReading();
        items.push_back(it);
    }

    std::mt19937 rng(seed);
    std::shuffle(items.begin(), items.end(), rng);
    if (limit > 0 && items.size() > limit) items.resize(limit);

    for (const SessionItem& it : items) queue_.push_back(it);
    if (!queue_.empty()) prepareItem(queue_.front());
}

void ReviewQueue::setPart(SessionItem& item) {
    if (item.needsMeaning && !item.meaningDone) {
        part_ = ReviewPart::Meaning;
    } else if (item.needsReading && !item.readingDone) {
        part_ = ReviewPart::Reading;
    } else {
        part_ = ReviewPart::None;
    }
}

void ReviewQueue::prepareItem(SessionItem& item) {
    item.meaningAttempted = false;
    item.readingAttempted = false;
    setPart(item);
}

void ReviewQueue::requeueFront() {
    if (queue_.empty()) return;
    SessionItem item = queue_.front();
    queue_.pop_front();
    prepareItem(item);
    const size_t pos = std::min(kRequeueGap, queue_.size());
    queue_.insert(queue_.begin() + static_cast<std::ptrdiff_t>(pos), item);
    if (!queue_.empty()) setPart(queue_.front());
}

AnswerOutcome ReviewQueue::answer(AnswerStatus status) {
    AnswerOutcome out;
    if (queue_.empty() || part_ == ReviewPart::None) return out;

    SessionItem& item = queue_.front();
    if (status == AnswerStatus::Warning) {
        // No count, no advance: the UI shows the warning and retries.
        return out;
    }

    const bool meaning = part_ == ReviewPart::Meaning;
    if (status == AnswerStatus::Incorrect) {
        out.counted = true;
        if (meaning) {
            if (item.incorrectMeaning < 0xFFFF) ++item.incorrectMeaning;
            item.meaningAttempted = true;
        } else {
            if (item.incorrectReading < 0xFFFF) ++item.incorrectReading;
            item.readingAttempted = true;
        }

        // Ask the other part first, as WaniKani does, then bring the item back.
        const bool otherPending = meaning ? (item.needsReading && !item.readingDone)
                                          : (item.needsMeaning && !item.meaningDone);
        const bool otherAttempted = meaning ? item.readingAttempted : item.meaningAttempted;
        if (otherPending && !otherAttempted) {
            part_ = meaning ? ReviewPart::Reading : ReviewPart::Meaning;
            out.advanced = true;
            return out;
        }
        requeueFront();
        out.advanced = true;
        out.requeued = true;
        return out;
    }

    // Correct.
    if (meaning) {
        item.meaningDone = true;
        item.meaningAttempted = true;
    } else {
        item.readingDone = true;
        item.readingAttempted = true;
    }
    const bool allDone = (!item.needsMeaning || item.meaningDone) &&
                         (!item.needsReading || item.readingDone);
    if (allDone) {
        completed_.push_back(item);
        queue_.pop_front();
        if (!queue_.empty()) setPart(queue_.front());
        out.advanced = true;
        out.completed = true;
        return out;
    }

    // Move to the part that still needs an answer.
    if (item.needsMeaning && !item.meaningDone)
        part_ = ReviewPart::Meaning;
    else if (item.needsReading && !item.readingDone)
        part_ = ReviewPart::Reading;
    out.advanced = true;
    return out;
}

void ReviewQueue::skip() {
    if (queue_.empty()) return;
    queue_.pop_front();
    if (!queue_.empty()) setPart(queue_.front());
}

}  // namespace kk

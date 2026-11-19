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

bool autoAdvanceDue(uint32_t delayMs, uint32_t elapsedMs) {
    return delayMs > 0 && elapsedMs >= delayMs;
}

void ReviewQueue::build(const std::vector<Assignment>& assignments,
                        const std::vector<Subject>& subjects, uint32_t now, size_t limit,
                        uint32_t seed) {
    queue_.clear();
    completed_.clear();
    part_ = ReviewPart::None;
    lastIncorrectAssignmentId_ = 0;
    lastIncorrectSubjectId_ = 0;
    lastIncorrectPart_ = ReviewPart::None;

    std::vector<SessionItem> items;
    for (const Assignment& a : assignments) {
        if (!due(a, now)) continue;
        const Subject* s = findSubject(subjects, a.subjectId);
        if (!s) continue;
        SessionItem it;
        it.assignmentId = a.id;
        it.subjectId = a.subjectId;
        it.type = a.type;
        it.level = s->level;
        it.srsStage = a.srsStage;
        it.needsMeaning = true;
        it.needsReading = s->needsReading();
        items.push_back(it);
    }

    rng_.seed(seed);
    std::shuffle(items.begin(), items.end(), rng_);
    if (lowerLevelsFirst_) {
        std::stable_sort(items.begin(), items.end(),
                         [](const SessionItem& a, const SessionItem& b) {
                             return a.level < b.level;
                         });
    }
    if (!criticalSubjectIds_.empty()) {
        std::stable_partition(items.begin(), items.end(), [this](const SessionItem& item) {
            return std::find(criticalSubjectIds_.begin(), criticalSubjectIds_.end(),
                             item.subjectId) != criticalSubjectIds_.end();
        });
    }
    if (limit > 0 && items.size() > limit) items.resize(limit);
    criticalSubjectIds_.clear();

    for (const SessionItem& it : items) queue_.push_back(it);
    if (!queue_.empty()) prepareItem(queue_.front());
}

void ReviewQueue::buildPractice(const std::vector<Subject>& subjects, size_t limit,
                                uint32_t seed) {
    std::vector<const Subject*> pointers;
    pointers.reserve(subjects.size());
    for (const Subject& s : subjects) pointers.push_back(&s);
    buildPractice(pointers, limit, seed);
}

void ReviewQueue::buildPractice(const std::vector<const Subject*>& subjects, size_t limit,
                                uint32_t seed) {
    queue_.clear();
    completed_.clear();
    part_ = ReviewPart::None;
    lastIncorrectAssignmentId_ = 0;
    lastIncorrectSubjectId_ = 0;
    lastIncorrectPart_ = ReviewPart::None;
    meaningFirst_ = true;
    backToBack_ = true;
    criticalSubjectIds_.clear();

    std::vector<SessionItem> items;
    items.reserve(subjects.size());
    for (const Subject* s : subjects) {
        if (!s) continue;
        SessionItem it;
        it.assignmentId = 0;
        it.subjectId = s->id;
        it.type = s->type;
        it.level = s->level;
        it.srsStage = 0;
        it.needsMeaning = true;
        it.needsReading = s->needsReading();
        items.push_back(it);
    }

    rng_.seed(seed);
    std::shuffle(items.begin(), items.end(), rng_);
    if (limit > 0 && items.size() > limit) items.resize(limit);

    for (const SessionItem& it : items) queue_.push_back(it);
    if (!queue_.empty()) prepareItem(queue_.front());
}

void ReviewQueue::setPart(SessionItem& item) {
    if (meaningFirst_) {
        if (item.needsMeaning && !item.meaningDone) {
            part_ = ReviewPart::Meaning;
        } else if (item.needsReading && !item.readingDone) {
            part_ = ReviewPart::Reading;
        } else {
            part_ = ReviewPart::None;
        }
    } else {
        if (item.needsReading && !item.readingDone) {
            part_ = ReviewPart::Reading;
        } else if (item.needsMeaning && !item.meaningDone) {
            part_ = ReviewPart::Meaning;
        } else {
            part_ = ReviewPart::None;
        }
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

void ReviewQueue::interleaveFront() {
    if (queue_.empty()) return;
    SessionItem item = queue_.front();
    queue_.pop_front();
    // Never the very front while another item exists: with N items left the
    // item goes to one of positions 1..N. A single-item queue puts it back at
    // the front so the other part is still asked.
    const size_t pos = queue_.empty() ? 0 : 1 + (rng_() % queue_.size());
    queue_.insert(queue_.begin() + static_cast<std::ptrdiff_t>(pos), item);
    setPart(queue_.front());
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
        // REV-04: remember what to undo before the item is advanced/requeued.
        lastIncorrectAssignmentId_ = item.assignmentId;
        lastIncorrectSubjectId_ = item.subjectId;
        lastIncorrectPart_ = part_;
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
            if (backToBack_ || queue_.size() == 1) {
                // On, or a single-item queue that must not spin: ask the other
                // part of the same item next.
                part_ = meaning ? ReviewPart::Reading : ReviewPart::Meaning;
            } else {
                // REV-09 off: let another item ask next; the failed part comes
                // back later with the same attempted flag.
                interleaveFront();
            }
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

    // The other part is still pending and was already answered wrong: bring
    // the item back a few positions later instead of asking the failed part
    // right after the feedback showed it.
    const bool otherPending = meaning ? (item.needsReading && !item.readingDone)
                                      : (item.needsMeaning && !item.meaningDone);
    const bool otherAttempted = meaning ? item.readingAttempted : item.meaningAttempted;
    if (otherPending && otherAttempted) {
        requeueFront();
        out.advanced = true;
        out.requeued = true;
        return out;
    }

    // REV-09 off: give the next item a turn and keep the part just answered.
    if (!backToBack_) {
        interleaveFront();
        out.advanced = true;
        return out;
    }

    // Move to the part that still needs an answer.
    setPart(item);
    out.advanced = true;
    return out;
}

void ReviewQueue::skip() {
    if (queue_.empty()) return;
    queue_.pop_front();
    lastIncorrectAssignmentId_ = 0;
    lastIncorrectSubjectId_ = 0;
    lastIncorrectPart_ = ReviewPart::None;
    if (!queue_.empty()) setPart(queue_.front());
}

bool ReviewQueue::undoLastIncorrect() {
    if (lastIncorrectPart_ == ReviewPart::None) return false;

    // Reviews identify an item by its assignment; practice items all carry
    // assignmentId 0, so fall back to the subject id there.
    auto found = queue_.end();
    for (auto it = queue_.begin(); it != queue_.end(); ++it) {
        if (lastIncorrectAssignmentId_ != 0) {
            if (it->assignmentId == lastIncorrectAssignmentId_) {
                found = it;
                break;
            }
        } else if (it->subjectId == lastIncorrectSubjectId_) {
            found = it;
            break;
        }
    }
    if (found == queue_.end()) {
        // The item completed, was wrapped up away or skipped meanwhile.
        lastIncorrectAssignmentId_ = 0;
        lastIncorrectSubjectId_ = 0;
        lastIncorrectPart_ = ReviewPart::None;
        return false;
    }

    SessionItem item = *found;
    queue_.erase(found);
    if (lastIncorrectPart_ == ReviewPart::Meaning) {
        if (item.incorrectMeaning > 0) --item.incorrectMeaning;
        item.meaningAttempted = false;
    } else {
        if (item.incorrectReading > 0) --item.incorrectReading;
        item.readingAttempted = false;
    }
    queue_.push_front(item);
    part_ = lastIncorrectPart_;
    lastIncorrectAssignmentId_ = 0;
    lastIncorrectSubjectId_ = 0;
    lastIncorrectPart_ = ReviewPart::None;
    return true;
}

void ReviewQueue::askAgain() { requeueFront(); }

size_t ReviewQueue::wrapUp() {
    if (queue_.empty()) return 0;
    // The current item stays even when it is untouched; behind it only items
    // already in progress survive. The current part is left alone so the
    // question on screen does not change.
    std::deque<SessionItem> kept;
    kept.push_back(queue_.front());
    for (size_t i = 1; i < queue_.size(); ++i) {
        const SessionItem& item = queue_[i];
        if ((item.needsMeaning && item.meaningDone) ||
            (item.needsReading && item.readingDone))
            kept.push_back(item);
    }
    queue_.swap(kept);
    return queue_.size();
}

}  // namespace kk

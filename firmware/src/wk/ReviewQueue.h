#pragma once
// The review session queue: freeze a batch of due assignments, ask meaning
// then reading, requeue mistakes a few items later, and complete an item only
// when every required part is correct. Pure C++, no Arduino.
#include <cstdint>
#include <deque>
#include <vector>

#include "wk/AnswerChecker.h"
#include "wk/Types.h"

namespace kk {

enum class ReviewPart : uint8_t { None, Meaning, Reading };

struct SessionItem {
    uint32_t assignmentId = 0;
    uint32_t subjectId = 0;
    SubjectType type = SubjectType::Kanji;
    uint8_t level = 0;
    bool needsMeaning = true;
    bool needsReading = false;
    bool meaningDone = false;
    bool readingDone = false;
    bool meaningAttempted = false;
    bool readingAttempted = false;
    uint16_t incorrectMeaning = 0;
    uint16_t incorrectReading = 0;
};

struct AnswerOutcome {
    bool counted = false;     // an incorrect answer was recorded
    bool advanced = false;    // the current part changed or the item moved
    bool completed = false;   // all required parts are correct
    bool requeued = false;    // the item went back into the queue
};

class ReviewQueue {
public:
    // `assignments` need srsStage 1..8 and availableAt <= now. Subjects are
    // matched by id; an assignment without a subject is skipped. `limit` 0
    // means every due item; otherwise at most that many are frozen.
    void build(const std::vector<Assignment>& assignments, const std::vector<Subject>& subjects,
               uint32_t now, size_t limit = 20, uint32_t seed = 0x9E3779B9u);

    bool empty() const { return queue_.empty(); }
    size_t remaining() const { return queue_.size(); }
    size_t completedCount() const { return completed_.size(); }
    const std::deque<SessionItem>& queue() const { return queue_; }
    const std::vector<SessionItem>& completed() const { return completed_; }

    const SessionItem* current() const { return queue_.empty() ? nullptr : &queue_.front(); }
    ReviewPart part() const { return part_; }

    // Order of the two parts within an item. Call before build(); defaults to
    // meaning first. Reading-first is a user preference.
    void setMeaningFirst(bool meaningFirst) { meaningFirst_ = meaningFirst; }

    // WaniKani `reviews_presentation_order`. Call before build(); shuffled is
    // the default. lower_levels_first keeps the shuffle inside each level.
    void setLowerLevelsFirst(bool lowerLevelsFirst) { lowerLevelsFirst_ = lowerLevelsFirst; }
    bool lowerLevelsFirst() const { return lowerLevelsFirst_; }

    // Applies one answer to the current part. Warning leaves the item and the
    // part untouched (the UI retries the same answer). Incorrect increments the
    // counter and either switches to the other part or requeues the item.
    AnswerOutcome answer(AnswerStatus status);

    // Drop the current item (the user skipped it). It is not submitted.
    void skip();

    // Requeue gap: mistakes return this many items later.
    static constexpr size_t kRequeueGap = 4;

private:
    void setPart(SessionItem& item);
    void prepareItem(SessionItem& item);
    void requeueFront();

    std::deque<SessionItem> queue_;
    std::vector<SessionItem> completed_;
    ReviewPart part_ = ReviewPart::None;
    bool meaningFirst_ = true;
    bool lowerLevelsFirst_ = false;
};

}  // namespace kk

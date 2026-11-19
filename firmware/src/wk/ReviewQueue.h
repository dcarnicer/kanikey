#pragma once
// The review session queue: freeze a batch of due assignments, ask meaning
// then reading, requeue mistakes a few items later, and complete an item only
// when every required part is correct. Pure C++, no Arduino.
#include <cstdint>
#include <deque>
#include <random>
#include <vector>

#include "wk/AnswerChecker.h"
#include "wk/Types.h"

namespace kk {

enum class ReviewPart : uint8_t { None, Meaning, Reading };

// REV-06: true when an auto-advance delay is set and the feedback frame has
// been visible for at least that long. A zero delay never advances.
bool autoAdvanceDue(uint32_t delayMs, uint32_t elapsedMs);

struct SessionItem {
    uint32_t assignmentId = 0;
    uint32_t subjectId = 0;
    SubjectType type = SubjectType::Kanji;
    uint8_t level = 0;
    uint8_t srsStage = 0;
    bool needsMeaning = true;
    bool needsReading = false;
    bool meaningDone = false;
    bool readingDone = false;
    bool meaningAttempted = false;
    bool readingAttempted = false;
    uint16_t incorrectMeaning = 0;
    uint16_t incorrectReading = 0;
    // REV-04: 'C' when the user marked a wrong part correct, 'A' when they
    // asked it again; 0 otherwise. Local-only journal metadata.
    char wrongAnswerOverride = 0;
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

    // Local practice over lesson subjects: no due filter, assignmentId 0 and
    // meaning first. Wrong parts requeue exactly like a review. `limit` 0 means
    // every subject.
    void buildPractice(const std::vector<Subject>& subjects, size_t limit = 0,
                       uint32_t seed = 0x9E3779B9u);
    // Same without copying the lesson batch's subjects.
    void buildPractice(const std::vector<const Subject*>& subjects, size_t limit = 0,
                       uint32_t seed = 0x9E3779B9u);

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

    // REV-09: on keeps both parts of one item together; off interleaves the
    // next item after each answered part. Call before build(); defaults to on.
    void setBackToBack(bool backToBack) { backToBack_ = backToBack; }

    // WaniKani `reviews_presentation_order`. Call before build(); shuffled is
    // the default. lower_levels_first keeps the shuffle inside each level.
    void setLowerLevelsFirst(bool lowerLevelsFirst) { lowerLevelsFirst_ = lowerLevelsFirst; }
    bool lowerLevelsFirst() const { return lowerLevelsFirst_; }

    // Critical subjects move before the rest, preserving the normal order
    // inside both groups. Call before build(); the list is cleared by build.
    void setCriticalSubjectIds(const std::vector<uint32_t>& ids) {
        criticalSubjectIds_ = ids;
    }

    // Applies one answer to the current part. Warning leaves the item and the
    // part untouched (the UI retries the same answer). Incorrect increments the
    // counter and either switches to the other part or requeues the item. A
    // correct part also requeues the item when the other part was answered
    // wrong already, so the failed part is not asked immediately.
    AnswerOutcome answer(AnswerStatus status);

    // REV-04: undo the last incorrect answer (when its item is still queued).
    // The matching counter is decremented, that part's attempted flag cleared
    // and the item moved back to the front with the failed part selected.
    // Returns false when no incorrect answer is recorded or the item is gone.
    bool undoLastIncorrect();

    // REV-04: ask the front item again later, with no mistake and both parts
    // unanswered again. Use after undoLastIncorrect().
    void askAgain();

    // REV-04: tag the front item for the local journal ('C' or 'A').
    void setWrongAnswerOverride(char overrideValue) {
        if (!queue_.empty()) queue_.front().wrongAnswerOverride = overrideValue;
    }

    // Drop the current item (the user skipped it). It is not submitted.
    void skip();

    // REV-05: keep the current item plus queued items with one part done and
    // drop untouched items. The current part is unchanged. Returns remaining().
    size_t wrapUp();

    // Requeue gap: mistakes return this many items later.
    static constexpr size_t kRequeueGap = 4;

private:
    void setPart(SessionItem& item);
    void prepareItem(SessionItem& item);
    void requeueFront();
    // REV-09: move the front item to a random position after at least one
    // other item, without resetting its attempted flags.
    void interleaveFront();

    std::deque<SessionItem> queue_;
    std::vector<SessionItem> completed_;
    ReviewPart part_ = ReviewPart::None;
    // REV-04: the last Incorrect answer, kept until it is undone or replaced.
    uint32_t lastIncorrectAssignmentId_ = 0;
    uint32_t lastIncorrectSubjectId_ = 0;
    ReviewPart lastIncorrectPart_ = ReviewPart::None;
    bool meaningFirst_ = true;
    bool lowerLevelsFirst_ = false;
    bool backToBack_ = true;
    std::mt19937 rng_{0x9E3779B9u};
    std::vector<uint32_t> criticalSubjectIds_;
};

}  // namespace kk

#include "wk/ReviewQueue.h"

#include <algorithm>
#include <numeric>

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

// R3: radicals, then kanji, then vocabulary/kanji-vocabulary.
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

uint8_t findLevel(const std::vector<ReviewLevel>& levels, uint32_t subjectId) {
    for (const ReviewLevel& entry : levels)
        if (entry.subjectId == subjectId) return entry.level;
    return 0;
}

// The fields the R2 comparators need, shared by selectDueSubjectIds() and
// build() so the selection and the in-batch order cannot drift.
struct OrderKey {
    uint8_t level = 0;
    uint8_t srsStage = 0;
    uint32_t availableAt = 0;
    SubjectType type = SubjectType::Kanji;
    uint32_t assignmentId = 0;
};

int primaryCompare(const OrderKey& a, const OrderKey& b, ReviewOrderStrategy strategy,
                   uint8_t currentLevel) {
    switch (strategy) {
        case ReviewOrderStrategy::LowestLevelFirst: {
            // Unknown level (0) sorts after every real WaniKani level.
            const uint8_t al = a.level ? a.level : 0xFF;
            const uint8_t bl = b.level ? b.level : 0xFF;
            if (al != bl) return al < bl ? -1 : 1;
            return 0;
        }
        case ReviewOrderStrategy::CurrentLevelFirst: {
            // 0 = the current level, 1 = other known levels, 2 = unknown.
            const auto group = [currentLevel](uint8_t level) -> uint8_t {
                if (level == 0) return 2;
                if (currentLevel != 0 && level == currentLevel) return 0;
                return 1;
            };
            const uint8_t ag = group(a.level);
            const uint8_t bg = group(b.level);
            if (ag != bg) return ag < bg ? -1 : 1;
            if (a.level != b.level) return a.level < b.level ? -1 : 1;
            return 0;
        }
        case ReviewOrderStrategy::SrsAscending:
            if (a.srsStage != b.srsStage) return a.srsStage < b.srsStage ? -1 : 1;
            return 0;
        case ReviewOrderStrategy::SrsDescending:
            if (a.srsStage != b.srsStage) return a.srsStage > b.srsStage ? -1 : 1;
            return 0;
        case ReviewOrderStrategy::OldestAvailableFirst:
            if (a.availableAt != b.availableAt)
                return a.availableAt < b.availableAt ? -1 : 1;
            return 0;
        case ReviewOrderStrategy::Account:
        case ReviewOrderStrategy::Random:
            return 0;
    }
    return 0;
}

// Primary order, then R3 type grouping, then the assignment id so a fetched
// page in server order gets the same order as the selected id list.
bool orderBefore(const OrderKey& a, const OrderKey& b, ReviewOrderStrategy strategy,
                 uint8_t currentLevel, bool typeOrder) {
    const int primary = primaryCompare(a, b, strategy, currentLevel);
    if (primary != 0) return primary < 0;
    if (typeOrder) {
        const uint8_t ar = typeRank(a.type);
        const uint8_t br = typeRank(b.type);
        if (ar != br) return ar < br;
    }
    return a.assignmentId < b.assignmentId;
}

OrderKey keyOf(const SessionItem& item) {
    OrderKey key;
    key.level = item.level;
    key.srsStage = item.srsStage;
    key.availableAt = item.availableAt;
    key.type = item.type;
    key.assignmentId = item.assignmentId;
    return key;
}

}  // namespace

ReviewOrderStrategy reviewOrderStrategyFromValue(int value) {
    if (value < 0 || value > static_cast<int>(ReviewOrderStrategy::OldestAvailableFirst))
        return ReviewOrderStrategy::Account;
    return static_cast<ReviewOrderStrategy>(value);
}

const char* reviewOrderStrategyName(ReviewOrderStrategy strategy) {
    switch (strategy) {
        case ReviewOrderStrategy::Account:
            return "account";
        case ReviewOrderStrategy::Random:
            return "random";
        case ReviewOrderStrategy::LowestLevelFirst:
            return "lowest_level_first";
        case ReviewOrderStrategy::CurrentLevelFirst:
            return "current_level_first";
        case ReviewOrderStrategy::SrsAscending:
            return "srs_ascending";
        case ReviewOrderStrategy::SrsDescending:
            return "srs_descending";
        case ReviewOrderStrategy::OldestAvailableFirst:
            return "oldest_available_first";
    }
    return "account";
}

FirstPart firstPartFromValue(int value) {
    switch (value) {
        case 1:
            return FirstPart::Meaning;
        case 0:
            return FirstPart::Reading;
        case 2:
            return FirstPart::Random;
        default:
            return FirstPart::Random;
    }
}

std::vector<uint32_t> selectDueSubjectIds(const std::vector<Assignment>& assignments,
                                          const std::vector<ReviewLevel>& levels,
                                          ReviewOrderStrategy strategy, uint8_t currentLevel,
                                          bool typeOrder, uint32_t now, size_t limit,
                                          uint32_t seed) {
    const bool usesLevels = strategy == ReviewOrderStrategy::LowestLevelFirst ||
                            strategy == ReviewOrderStrategy::CurrentLevelFirst;
    // Indices into the caller's assignment vector: the due metadata stays where
    // it is, so the extra copy is ~5 bytes per due item on the device.
    std::vector<size_t> rows;
    std::vector<uint8_t> rowLevels;
    rows.reserve(assignments.size());
    rowLevels.reserve(assignments.size());
    for (size_t i = 0; i < assignments.size(); ++i) {
        const Assignment& a = assignments[i];
        if (!due(a, now)) continue;
        rows.push_back(i);
        rowLevels.push_back(usesLevels ? findLevel(levels, a.subjectId) : 0);
    }

    const auto key = [&assignments, &rows, &rowLevels](size_t row) {
        const Assignment& a = assignments[rows[row]];
        OrderKey out;
        out.level = rowLevels[row];
        out.srsStage = a.srsStage;
        out.availableAt = a.availableAt;
        out.type = a.type;
        out.assignmentId = a.id;
        return out;
    };

    std::vector<size_t> order(rows.size());
    std::iota(order.begin(), order.end(), size_t{0});
    if (strategy == ReviewOrderStrategy::Random) {
        // A 4-byte xorshift state here: a local std::mt19937 was ~2.5 KB of
        // stack on the sync path (MEM-11, MEM-12).
        Xorshift32 rng(seed);
        std::shuffle(order.begin(), order.end(), rng);
        if (typeOrder) {
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                return typeRank(key(a).type) < typeRank(key(b).type);
            });
        }
    } else if (strategy == ReviewOrderStrategy::Account) {
        // The caller keeps the account's summary/sample order; only R3 is
        // applied here (build() owns the Account shuffle).
        if (typeOrder) {
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                return typeRank(key(a).type) < typeRank(key(b).type);
            });
        }
    } else {
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return orderBefore(key(a), key(b), strategy, currentLevel, typeOrder);
        });
    }

    const size_t count = limit == 0 ? order.size() : std::min(limit, order.size());
    std::vector<uint32_t> ids;
    ids.reserve(count);
    for (size_t i = 0; i < count; ++i)
        ids.push_back(assignments[rows[order[i]]].subjectId);
    return ids;
}

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
        it.availableAt = a.availableAt;
        it.needsMeaning = true;
        it.needsReading = s->needsReading();
        items.push_back(it);
    }

    rng_.seed(seed);
    if (orderStrategy_ == ReviewOrderStrategy::Account) {
        // Today's account preference: shuffle, optionally lower levels first
        // with the shuffle kept inside each level (R2/R3 default path).
        std::shuffle(items.begin(), items.end(), rng_);
        if (lowerLevelsFirst_) {
            std::stable_sort(items.begin(), items.end(),
                             [this](const SessionItem& a, const SessionItem& b) {
                                 if (a.level != b.level) return a.level < b.level;
                                 if (typeOrder_)
                                     return typeRank(a.type) < typeRank(b.type);
                                 return false;
                             });
        } else if (typeOrder_) {
            std::stable_sort(items.begin(), items.end(),
                             [](const SessionItem& a, const SessionItem& b) {
                                 return typeRank(a.type) < typeRank(b.type);
                             });
        }
    } else if (orderStrategy_ == ReviewOrderStrategy::Random) {
        std::shuffle(items.begin(), items.end(), rng_);
        if (typeOrder_) {
            std::stable_sort(items.begin(), items.end(),
                             [](const SessionItem& a, const SessionItem& b) {
                                 return typeRank(a.type) < typeRank(b.type);
                             });
        }
    } else {
        // The selected ids may come back in server order: re-apply the same
        // comparator instead of trusting the fetched order.
        std::stable_sort(items.begin(), items.end(),
                         [this](const SessionItem& a, const SessionItem& b) {
                             return orderBefore(keyOf(a), keyOf(b), orderStrategy_,
                                                currentLevel_, typeOrder_);
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

    // R4: drawn in the final queue order, after the limit and critical split.
    for (SessionItem& it : items) {
        assignFirstPart(it, firstPart_);
        queue_.push_back(it);
    }
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

    // The lesson batch quiz is always random per item (WaniKani's lesson quiz).
    for (SessionItem& it : items) {
        assignFirstPart(it, FirstPart::Random);
        queue_.push_back(it);
    }
    if (!queue_.empty()) prepareItem(queue_.front());
}

void ReviewQueue::assignFirstPart(SessionItem& item, FirstPart mode) {
    switch (mode) {
        case FirstPart::Meaning:
            item.readingFirst = false;
            break;
        case FirstPart::Reading:
            item.readingFirst = true;
            break;
        case FirstPart::Random:
            // The top bit of the xorshift32 output is the best-mixed one; a
            // fixed seed gives the same split every run.
            item.readingFirst = (rng_() >> 31) != 0;
            break;
    }
}

void ReviewQueue::setPart(SessionItem& item) {
    // The part that is still owed. The first part is item.readingFirst, so a
    // requeue asks whichever parts remain, in the same fixed order.
    const bool meaningPending = item.needsMeaning && !item.meaningDone;
    const bool readingPending = item.needsReading && !item.readingDone;
    if (item.readingFirst) {
        if (readingPending) {
            part_ = ReviewPart::Reading;
        } else if (meaningPending) {
            part_ = ReviewPart::Meaning;
        } else {
            part_ = ReviewPart::None;
        }
    } else {
        if (meaningPending) {
            part_ = ReviewPart::Meaning;
        } else if (readingPending) {
            part_ = ReviewPart::Reading;
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
    queue_.erase(queue_.begin());
    prepareItem(item);
    const size_t pos = std::min(kRequeueGap, queue_.size());
    queue_.insert(queue_.begin() + static_cast<std::ptrdiff_t>(pos), item);
    if (!queue_.empty()) setPart(queue_.front());
}

void ReviewQueue::interleaveFront() {
    if (queue_.empty()) return;
    SessionItem item = queue_.front();
    queue_.erase(queue_.begin());
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
        queue_.erase(queue_.begin());
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
    queue_.erase(queue_.begin());
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
    queue_.insert(queue_.begin(), item);
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
    std::vector<SessionItem> kept;
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

#pragma once
// The lesson batch flow: which list rows are in the batch, which card and page
// the browser is on, and the plan for the one confirmed start. Pure C++, no
// Arduino; nothing here talks to WaniKani.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "wk/Types.h"

namespace kk {

// What follows the last lesson card. L3 "Required" runs the batch quiz;
// "Skip" goes straight to the start confirmation.
enum class LessonNextStep : uint8_t { Quiz, Start };
LessonNextStep lessonNextStep(bool quizRequired);

// The list selection and the card/page cursor. The selected rows are kept in
// list order and never exceed the batch size the list was opened with.
class LessonBrowse {
public:
    // Selects the first min(count, maxSelected) rows.
    void begin(size_t count, size_t maxSelected);

    // Adds or removes a row. Returns false when the row is not selectable
    // because the batch is full; removing a selected row always works.
    bool toggle(size_t row, size_t maxSelected);

    size_t count() const { return count_; }
    size_t cursor() const { return cursor_; }
    void setCursor(size_t row);

    const std::vector<size_t>& selected() const { return selected_; }
    size_t selectedCount() const { return selected_.size(); }
    // Map a position in the selected batch to the row in the full list.
    // Returns count() when the position is out of range.
    size_t batchIndex(size_t position) const;

    size_t card() const { return card_; }
    void setCard(size_t position);

    size_t page() const { return page_; }
    size_t pageCount() const { return pageCount_; }
    void setPageCount(size_t pages);
    bool onLastPage() const { return page_ + 1 >= pageCount_; }

    // Advances one page, or one card when already on the last page. Returns
    // true when the card changed.
    bool nextPageOrCard();
    // Goes back one page, or one card when already on the first page. Returns
    // true when the card changed.
    bool previous();

    // True when the cursor is on the last page of the last selected card.
    bool atEnd() const;

    // "2/5 p1/3": card position, batch size, page position, page count.
    void header(char* buf, size_t n) const;

private:
    size_t count_ = 0;
    size_t cursor_ = 0;
    size_t card_ = 0;
    size_t page_ = 0;
    size_t pageCount_ = 1;
    std::vector<size_t> selected_;
};

enum class LessonStartResult : uint8_t {
    Started,         // 2xx
    AlreadyStarted,  // 422: started elsewhere, counts as done
    Auth,            // 401/403: the token cannot start this batch
    Failed,          // anything else (including 0, no response)
};
LessonStartResult classifyStartResult(int httpCode);

struct LessonStartItem {
    enum class State : uint8_t { Pending, Started, Failed };
    size_t batchIndex = 0;
    uint32_t assignmentId = 0;
    uint32_t subjectId = 0;
    State state = State::Pending;
};

// The selected items in list order. The controller walks nextPending() so a
// later item is not started once one failed; the rest of the batch is kept.
class LessonStartPlan {
public:
    void build(const std::vector<Assignment>& assignments,
               const std::vector<size_t>& selected);

    bool empty() const { return items_.empty(); }
    size_t size() const { return items_.size(); }
    size_t startedCount() const;
    size_t failedCount() const;
    size_t unstartedCount() const;
    bool allStarted() const;
    const std::vector<LessonStartItem>& items() const { return items_; }

    // First item still Pending, or nullptr when none is left.
    const LessonStartItem* nextPending() const;
    void markStarted(size_t position);
    void markFailed(size_t position);
    // Retry: failed items become pending again; started items stay started.
    void resetFailed();

private:
    std::vector<LessonStartItem> items_;
};

}  // namespace kk

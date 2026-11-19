#include "wk/LessonSession.h"

#include <algorithm>
#include <cstdio>

namespace kk {

LessonNextStep lessonNextStep(bool quizRequired) {
    return quizRequired ? LessonNextStep::Quiz : LessonNextStep::Start;
}

void LessonBrowse::begin(size_t count, size_t maxSelected) {
    count_ = count;
    cursor_ = 0;
    card_ = 0;
    page_ = 0;
    pageCount_ = 1;
    selected_.clear();
    const size_t n = std::min(count, maxSelected);
    selected_.reserve(n);
    for (size_t i = 0; i < n; ++i) selected_.push_back(i);
}

bool LessonBrowse::toggle(size_t row, size_t maxSelected) {
    if (row >= count_) return false;
    const auto it = std::lower_bound(selected_.begin(), selected_.end(), row);
    if (it != selected_.end() && *it == row) {
        selected_.erase(it);
        if (card_ >= selected_.size()) card_ = selected_.empty() ? 0 : selected_.size() - 1;
        if (page_ >= pageCount_) page_ = pageCount_ - 1;
        return true;
    }
    if (selected_.size() >= maxSelected) return false;
    selected_.insert(it, row);
    return true;
}

void LessonBrowse::setCursor(size_t row) {
    cursor_ = count_ == 0 ? 0 : std::min(row, count_ - 1);
}

size_t LessonBrowse::batchIndex(size_t position) const {
    return position < selected_.size() ? selected_[position] : count_;
}

void LessonBrowse::setCard(size_t position) {
    card_ = selected_.empty() ? 0 : std::min(position, selected_.size() - 1);
    page_ = 0;
}

void LessonBrowse::setPageCount(size_t pages) {
    pageCount_ = std::max<size_t>(1, pages);
    if (page_ >= pageCount_) page_ = pageCount_ - 1;
}

bool LessonBrowse::nextPageOrCard() {
    if (page_ + 1 < pageCount_) {
        ++page_;
        return false;
    }
    if (card_ + 1 < selected_.size()) {
        ++card_;
        page_ = 0;
        return true;
    }
    return false;
}

bool LessonBrowse::previous() {
    if (page_ > 0) {
        --page_;
        return false;
    }
    if (card_ > 0) {
        --card_;
        page_ = 0;
        return true;
    }
    return false;
}

bool LessonBrowse::atEnd() const {
    if (selected_.empty()) return true;
    return card_ + 1 >= selected_.size() && onLastPage();
}

void LessonBrowse::header(char* buf, size_t n) const {
    if (!buf || n == 0) return;
    const unsigned pos = selected_.empty() ? 0 : static_cast<unsigned>(card_ + 1);
    snprintf(buf, n, "%u/%u p%u/%u", pos, static_cast<unsigned>(selected_.size()),
             static_cast<unsigned>(page_ + 1), static_cast<unsigned>(pageCount_));
}

LessonStartResult classifyStartResult(int httpCode) {
    if (httpCode >= 200 && httpCode < 300) return LessonStartResult::Started;
    if (httpCode == 422) return LessonStartResult::AlreadyStarted;
    if (httpCode == 401 || httpCode == 403) return LessonStartResult::Auth;
    return LessonStartResult::Failed;
}

void LessonStartPlan::build(const std::vector<Assignment>& assignments,
                            const std::vector<size_t>& selected) {
    items_.clear();
    items_.reserve(selected.size());
    for (size_t index : selected) {
        if (index >= assignments.size()) continue;
        LessonStartItem item;
        item.batchIndex = index;
        item.assignmentId = assignments[index].id;
        item.subjectId = assignments[index].subjectId;
        items_.push_back(item);
    }
}

size_t LessonStartPlan::startedCount() const {
    return static_cast<size_t>(std::count_if(items_.begin(), items_.end(),
                                             [](const LessonStartItem& item) {
                                                 return item.state ==
                                                        LessonStartItem::State::Started;
                                             }));
}

size_t LessonStartPlan::failedCount() const {
    return static_cast<size_t>(std::count_if(items_.begin(), items_.end(),
                                             [](const LessonStartItem& item) {
                                                 return item.state ==
                                                        LessonStartItem::State::Failed;
                                             }));
}

size_t LessonStartPlan::unstartedCount() const {
    return static_cast<size_t>(std::count_if(items_.begin(), items_.end(),
                                             [](const LessonStartItem& item) {
                                                 return item.state !=
                                                        LessonStartItem::State::Started;
                                             }));
}

bool LessonStartPlan::allStarted() const {
    return !items_.empty() && startedCount() == items_.size();
}

const LessonStartItem* LessonStartPlan::nextPending() const {
    for (const LessonStartItem& item : items_)
        if (item.state == LessonStartItem::State::Pending) return &item;
    return nullptr;
}

void LessonStartPlan::markStarted(size_t position) {
    if (position < items_.size()) items_[position].state = LessonStartItem::State::Started;
}

void LessonStartPlan::markFailed(size_t position) {
    if (position < items_.size()) items_[position].state = LessonStartItem::State::Failed;
}

void LessonStartPlan::resetFailed() {
    for (LessonStartItem& item : items_)
        if (item.state == LessonStartItem::State::Failed)
            item.state = LessonStartItem::State::Pending;
}

}  // namespace kk

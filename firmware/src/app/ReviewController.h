#pragma once
// The review session: queue, answer feedback, detail card and summary.
#include <cstdint>

#include "app/AppState.h"
#include "wk/ReviewResponse.h"
#include "wk/Types.h"

namespace kk::app {

const kk::Subject* subjectById(uint32_t id);
int detailPageCount(const kk::Subject& s);
void showReview();
void showReviewDetail();
void reviewDetailKey(const tgc::KeyEvent& e);
void showReviewSummary();
void checkCurrent();
void advanceFeedback();
void endReview();
void startReview();
bool reviewKey(const tgc::KeyEvent& e);
void applyReviewUpdate(const kk::ReviewUpdate& update, uint32_t pendingAssignment);

}  // namespace kk::app

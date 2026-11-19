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
void reviewSummaryKey(const tgc::KeyEvent& e);
void checkCurrent();
void advanceFeedback();
bool reviewAutoAdvance(uint32_t now);
// send=false ends the session without submitting pending reviews (SPEED-06).
void endReview(bool send = true);
void startReview();
bool reviewKey(const tgc::KeyEvent& e);
void applyReviewUpdate(const kk::ReviewUpdate& update);

}  // namespace kk::app

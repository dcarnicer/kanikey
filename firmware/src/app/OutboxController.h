#pragma once
// The review outbox: queueing, sending, reconciling and the attention screen.
#include <cstddef>
#include <cstdint>

#include "app/AppState.h"

namespace kk::app {

bool outboxHasAssignment(uint32_t assignmentId);
size_t outboxFreeSlots();
void saveOutbox();
// REV-10 latency: queueReviewLocal() and journalPractice() update memory at
// once and queue their file writes; the writes run in flushPendingReviewWrites()
// after the feedback frame has been presented (see OutboxController.cpp for the
// order). deferCacheSnapshot() queues the snapshot save the same way.
bool queueReviewLocal(const kk::SessionItem& item);
void deferCacheSnapshot();
void flushPendingReviewWrites();
// Called by showReview() after it presented a feedback frame.
void feedbackFramePresented();
// Early call sites: run the queued writes only once the feedback frame is on screen.
void flushReviewWritesAfterFrame();
// True while queued writes wait for their feedback frame (syncAdvance waits a loop).
bool reviewWritesWaitForFrame();
// loop(): after the draw and the e-paper push.
void flushReviewWritesAfterDraw();
// Sends one queued record now. False when it did not reach WaniKani (offline,
// gate or rejection); the caller's send loop stops on the first failure.
bool sendQueuedReview(uint32_t seq);
void flushOutbox();
void journalPractice(const kk::SessionItem& item);
void openOutbox();
void outboxKey(const tgc::KeyEvent& e);
void drawOutbox();

}  // namespace kk::app

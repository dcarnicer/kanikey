#pragma once
// The review outbox: queueing, sending, reconciling and the attention screen.
#include <cstddef>
#include <cstdint>

#include "app/AppState.h"

namespace kk::app {

bool outboxHasAssignment(uint32_t assignmentId);
size_t outboxFreeSlots();
void saveOutbox();
bool queueReviewLocal(const kk::SessionItem& item);
// Sends one queued record now. False when it did not reach WaniKani (offline,
// gate or rejection); the caller's send loop stops on the first failure.
bool sendQueuedReview(uint32_t seq);
void flushOutbox();
void journalPractice(const kk::SessionItem& item);
void openOutbox();
void outboxKey(const tgc::KeyEvent& e);
void drawOutbox();

}  // namespace kk::app

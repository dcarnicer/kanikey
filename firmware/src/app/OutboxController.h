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
void sendQueuedReview(uint32_t seq);
void flushOutbox();
void journalPractice(const kk::SessionItem& item);
void openOutbox();
void outboxKey(const tgc::KeyEvent& e);
void drawOutbox();

}  // namespace kk::app

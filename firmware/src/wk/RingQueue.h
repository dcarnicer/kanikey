#pragma once
// Index arithmetic for a fixed-capacity ring queue of events. Pure C++ so the
// native tests can check the order the board key queue delivers after an
// overflow: the marker for lost events must be the newest element, not the
// slot of the element that was just dropped (FIX-24 a).
#include <cstddef>

namespace kk {

// Returns the index a newly appended element takes and updates `head` and
// `count`. When the ring already holds `length` elements the oldest one is
// dropped first (head advances, count stays at `length`), so the index that is
// returned holds the newest element after the call.
// `length` must be positive; `head` in [0, length), `count` in [0, length].
inline int ringAppendIndex(int* head, int* count, int length) {
    if (*count == length) {
        *head = (*head + 1) % length;
        --*count;
    }
    const int slot = (*head + *count) % length;
    ++*count;
    return slot;
}

}  // namespace kk

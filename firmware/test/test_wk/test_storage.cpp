// FIX-24 (a): the board key queue drops the oldest event on overflow and the
// "events lost" marker becomes the newest one, so the main loop hears about
// the loss as the last event it reads. The ring arithmetic lives in
// wk/RingQueue.h; these tests model push()/nextKeyEvent() with plain ints.
#include <unity.h>

#include "wk/RingQueue.h"

using namespace kk;

namespace {

constexpr int kLen = 4;
constexpr int kMarker = -1;  // stands in for KeyEvent{Key::None, false}

// BoardCardputerAdv.cpp push(): the newest event takes ringAppendIndex(), and a
// full ring gets the marker instead of the event that just arrived.
void push(int* ring, int* head, int* count, int value) {
    const bool lost = *count == kLen;
    ring[ringAppendIndex(head, count, kLen)] = lost ? kMarker : value;
}

// BoardCardputerAdv.cpp nextKeyEvent().
int pop(int* ring, int* head, int* count) {
    const int value = ring[*head];
    *head = (*head + 1) % kLen;
    --*count;
    return value;
}

}  // namespace

static void test_key_queue_keeps_order() {
    int ring[kLen] = {};
    int head = 0;
    int count = 0;
    for (int i = 1; i <= 3; ++i) push(ring, &head, &count, i);
    TEST_ASSERT_EQUAL(3, count);
    TEST_ASSERT_EQUAL(1, pop(ring, &head, &count));
    TEST_ASSERT_EQUAL(2, pop(ring, &head, &count));
    TEST_ASSERT_EQUAL(3, pop(ring, &head, &count));
    TEST_ASSERT_EQUAL(0, count);
}

static void test_key_queue_overflow_drops_only_the_oldest() {
    int ring[kLen] = {};
    int head = 0;
    int count = 0;
    for (int i = 1; i <= 4; ++i) push(ring, &head, &count, i);
    push(ring, &head, &count, 5);
    // The marker replaced 1; 2, 3 and 4 are still queued in order.
    TEST_ASSERT_EQUAL(kLen, count);
    TEST_ASSERT_EQUAL(2, pop(ring, &head, &count));
    TEST_ASSERT_EQUAL(3, pop(ring, &head, &count));
    TEST_ASSERT_EQUAL(4, pop(ring, &head, &count));
}

static void test_key_queue_marker_is_the_newest_event() {
    int ring[kLen] = {};
    int head = 0;
    int count = 0;
    for (int i = 1; i <= 4; ++i) push(ring, &head, &count, i);
    push(ring, &head, &count, 5);
    int last = 0;
    for (int i = 0; i < kLen; ++i) last = pop(ring, &head, &count);
    TEST_ASSERT_EQUAL(kMarker, last);
    TEST_ASSERT_EQUAL(0, count);
}

static void test_key_queue_wraps_without_overflow() {
    int ring[kLen] = {};
    int head = 0;
    int count = 0;
    for (int i = 1; i <= 4; ++i) push(ring, &head, &count, i);
    pop(ring, &head, &count);
    pop(ring, &head, &count);
    push(ring, &head, &count, 5);
    push(ring, &head, &count, 6);
    TEST_ASSERT_EQUAL(4, count);
    TEST_ASSERT_EQUAL(3, pop(ring, &head, &count));
    TEST_ASSERT_EQUAL(4, pop(ring, &head, &count));
    TEST_ASSERT_EQUAL(5, pop(ring, &head, &count));
    TEST_ASSERT_EQUAL(6, pop(ring, &head, &count));
}

static void test_key_queue_single_slot_keeps_the_marker() {
    int ring[1] = {};
    int head = 0;
    int count = 0;
    ring[ringAppendIndex(&head, &count, 1)] = 7;
    TEST_ASSERT_EQUAL(1, count);
    // A one-slot ring is always full on the next push: the marker replaces the
    // pending event instead of the push being lost.
    const bool lost = count == 1;
    ring[ringAppendIndex(&head, &count, 1)] = lost ? kMarker : 8;
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL(kMarker, ring[head]);
}

void run_storage_tests() {
    RUN_TEST(test_key_queue_keeps_order);
    RUN_TEST(test_key_queue_overflow_drops_only_the_oldest);
    RUN_TEST(test_key_queue_marker_is_the_newest_event);
    RUN_TEST(test_key_queue_wraps_without_overflow);
    RUN_TEST(test_key_queue_single_slot_keeps_the_marker);
}

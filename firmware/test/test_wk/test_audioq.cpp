// AUD-02 download queue: bound, dedupe, failure limit, account reset, and the
// drain decision (idle, link, spacing and heap rules).
#include <unity.h>

#include <string>

#include "wk/AudioQueue.h"

using namespace kk;

namespace {

AudioDrainState readyState() {
    AudioDrainState s;
    s.queued = true;
    s.screenIdle = true;
    s.sessionActive = false;
    s.syncRunning = false;
    s.wifiUp = true;
    s.sdOk = true;
    s.tokenUsable = true;
    s.msSinceAttempt = kAudioDrainNever;
    s.spacingMs = kAudioDrainSpacingMs;
    return s;
}

}  // namespace

static void test_audioq_enqueue_dedupes_by_subject_and_voice() {
    AudioQueue q;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioQueue::Add::Added),
                          static_cast<int>(q.enqueue(10, 0, "https://a/1.mp3", "audio/mpeg")));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioQueue::Add::Duplicate),
                          static_cast<int>(q.enqueue(10, 0, "https://a/1.mp3", "audio/mpeg")));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioQueue::Add::Added),
                          static_cast<int>(q.enqueue(10, 1, "https://a/2.mp3", "audio/mpeg")));
    TEST_ASSERT_EQUAL_UINT32(2, q.size());
    TEST_ASSERT_TRUE(q.contains(10, 0));
    TEST_ASSERT_TRUE(q.contains(10, 1));
    TEST_ASSERT_FALSE(q.contains(11, 0));
}

static void test_audioq_bounded_at_max() {
    AudioQueue q;
    for (uint32_t id = 1; id <= kAudioQueueMax; ++id)
        TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioQueue::Add::Added),
                              static_cast<int>(q.enqueue(id, 0, "https://a/x.mp3", "audio/mpeg")));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioQueue::Add::Full),
                          static_cast<int>(q.enqueue(99, 0, "https://a/x.mp3", "audio/mpeg")));
    TEST_ASSERT_EQUAL_UINT32(kAudioQueueMax, q.size());
}

static void test_audioq_fifo_and_pop() {
    AudioQueue q;
    q.enqueue(1, 0, "https://a/1.mp3", "audio/mpeg");
    q.enqueue(2, 0, "https://a/2.mp3", "audio/mpeg");
    TEST_ASSERT_EQUAL_UINT32(1, q.front().subjectId);
    TEST_ASSERT_EQUAL_STRING("https://a/1.mp3", q.front().url.c_str());
    q.popFront();
    TEST_ASSERT_EQUAL_UINT32(2, q.front().subjectId);
    q.popFront();
    TEST_ASSERT_TRUE(q.empty());
    q.popFront();  // popping an empty queue is a no-op
    TEST_ASSERT_TRUE(q.empty());
}

static void test_audioq_failure_moves_back_then_drops() {
    AudioQueue q;
    q.enqueue(1, 0, "https://a/1.mp3", "audio/mpeg");
    q.enqueue(2, 0, "https://a/2.mp3", "audio/mpeg");
    TEST_ASSERT_FALSE(q.failFront());  // A fails once: moves behind B
    TEST_ASSERT_EQUAL_UINT32(2, q.front().subjectId);
    TEST_ASSERT_FALSE(q.failFront());  // B fails once: moves behind A
    TEST_ASSERT_EQUAL_UINT32(1, q.front().subjectId);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kAudioQueueMaxFailures), 2);
    TEST_ASSERT_TRUE(q.failFront());   // A's second failure: dropped
    TEST_ASSERT_EQUAL_UINT32(1, q.size());
    TEST_ASSERT_EQUAL_UINT32(2, q.front().subjectId);
    TEST_ASSERT_FALSE(q.contains(1, 0));
}

static void test_audioq_failed_entry_can_requeue_after_drop() {
    AudioQueue q;
    q.enqueue(1, 0, "https://a/1.mp3", "audio/mpeg");
    q.failFront();
    q.failFront();  // dropped
    TEST_ASSERT_TRUE(q.empty());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioQueue::Add::Added),
                          static_cast<int>(q.enqueue(1, 0, "https://a/1.mp3", "audio/mpeg")));
    TEST_ASSERT_EQUAL_UINT32(0, q.front().failures);
}

static void test_audioq_account_change_empties() {
    AudioQueue q;
    q.useAccount(5);
    q.enqueue(1, 0, "https://a/1.mp3", "audio/mpeg");
    q.useAccount(5);
    TEST_ASSERT_EQUAL_UINT32(1, q.size());
    q.useAccount(6);
    TEST_ASSERT_TRUE(q.empty());
    TEST_ASSERT_EQUAL_UINT32(6, q.accountKey());
}

static void test_audioq_drain_allowed_when_all_hold() {
    TEST_ASSERT_TRUE(audioDrainAllowed(readyState()));
    AudioDrainState s = readyState();
    s.msSinceAttempt = kAudioDrainSpacingMs;  // exactly the spacing is enough
    TEST_ASSERT_TRUE(audioDrainAllowed(s));
}

static void test_audioq_drain_blocked_by_each_rule() {
    AudioDrainState s = readyState();
    s.queued = false;
    TEST_ASSERT_FALSE(audioDrainAllowed(s));
    s = readyState(); s.screenIdle = false;
    TEST_ASSERT_FALSE(audioDrainAllowed(s));
    s = readyState(); s.sessionActive = true;  // WkClient keeps the TLS state
    TEST_ASSERT_FALSE(audioDrainAllowed(s));
    s = readyState(); s.syncRunning = true;
    TEST_ASSERT_FALSE(audioDrainAllowed(s));
    s = readyState(); s.wifiUp = false;
    TEST_ASSERT_FALSE(audioDrainAllowed(s));
    s = readyState(); s.sdOk = false;
    TEST_ASSERT_FALSE(audioDrainAllowed(s));
    s = readyState(); s.tokenUsable = false;  // blocked or rate limited
    TEST_ASSERT_FALSE(audioDrainAllowed(s));
}

static void test_audioq_drain_spacing_holds_until_passed() {
    AudioDrainState s = readyState();
    s.msSinceAttempt = kAudioDrainSpacingMs - 1;
    TEST_ASSERT_FALSE(audioDrainAllowed(s));
    s.spacingMs = kAudioDrainRefusedSpacingMs;
    s.msSinceAttempt = kAudioDrainSpacingMs;
    TEST_ASSERT_FALSE(audioDrainAllowed(s));  // a refusal waits longer
    s.msSinceAttempt = kAudioDrainRefusedSpacingMs;
    TEST_ASSERT_TRUE(audioDrainAllowed(s));
}

static void test_audioq_heap_thresholds() {
    TEST_ASSERT_TRUE(audioDrainHeapOk(kAudioDrainMinLargest, kAudioDrainMinFree));
    TEST_ASSERT_FALSE(audioDrainHeapOk(kAudioDrainMinLargest - 1, 200 * 1024));
    TEST_ASSERT_FALSE(audioDrainHeapOk(200 * 1024, kAudioDrainMinFree - 1));
    // The 2026-11-20 failure state: 23.5 KB largest, 33 KB free.
    TEST_ASSERT_FALSE(audioDrainHeapOk(23 * 1024 + 512, 33 * 1024));
}

void run_audioq_tests() {
    RUN_TEST(test_audioq_enqueue_dedupes_by_subject_and_voice);
    RUN_TEST(test_audioq_bounded_at_max);
    RUN_TEST(test_audioq_fifo_and_pop);
    RUN_TEST(test_audioq_failure_moves_back_then_drops);
    RUN_TEST(test_audioq_failed_entry_can_requeue_after_drop);
    RUN_TEST(test_audioq_account_change_empties);
    RUN_TEST(test_audioq_drain_allowed_when_all_hold);
    RUN_TEST(test_audioq_drain_blocked_by_each_rule);
    RUN_TEST(test_audioq_drain_spacing_holds_until_passed);
    RUN_TEST(test_audioq_heap_thresholds);
}

// AUD-02 download queue: bound, dedupe, failure limit, account reset, fixed
// storage and the URL length limit; the drain decision (idle, link, spacing and
// heap rules); the refill rule and the per-voice URL choice.
#include <unity.h>

#include <string>
#include <vector>

#include "wk/AudioPlan.h"
#include "wk/AudioQueue.h"
#include "wk/Types.h"

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

AudioClip clip(const char* url, uint8_t voice, const char* pronunciation,
               const char* contentType = "audio/mpeg") {
    AudioClip c;
    c.url = url;
    c.contentType = contentType;
    c.voice = voice;
    c.pronunciation = pronunciation;
    return c;
}

}  // namespace

static void test_audioq_enqueue_dedupes_by_subject_and_voice() {
    AudioQueue q;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioQueue::Add::Added),
                          static_cast<int>(q.enqueue(10, 0, "https://a/1.mp3")));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioQueue::Add::Duplicate),
                          static_cast<int>(q.enqueue(10, 0, "https://a/1.mp3")));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioQueue::Add::Added),
                          static_cast<int>(q.enqueue(10, 1, "https://a/2.mp3")));
    TEST_ASSERT_EQUAL_UINT32(2, q.size());
    TEST_ASSERT_TRUE(q.contains(10, 0));
    TEST_ASSERT_TRUE(q.contains(10, 1));
    TEST_ASSERT_FALSE(q.contains(11, 0));
}

static void test_audioq_bounded_at_max() {
    AudioQueue q;
    for (uint32_t id = 1; id <= kAudioQueueMax; ++id)
        TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioQueue::Add::Added),
                              static_cast<int>(q.enqueue(id, 0, "https://a/x.mp3")));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioQueue::Add::Full),
                          static_cast<int>(q.enqueue(99, 0, "https://a/x.mp3")));
    TEST_ASSERT_EQUAL_UINT32(kAudioQueueMax, q.size());
    // A pop frees one slot, and it is reused.
    q.popFront();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioQueue::Add::Added),
                          static_cast<int>(q.enqueue(99, 0, "https://a/x.mp3")));
    TEST_ASSERT_EQUAL_UINT32(kAudioQueueMax, q.size());
}

static void test_audioq_fifo_and_pop() {
    AudioQueue q;
    q.enqueue(1, 0, "https://a/1.mp3");
    q.enqueue(2, 0, "https://a/2.mp3");
    TEST_ASSERT_EQUAL_UINT32(1, q.front().subjectId);
    TEST_ASSERT_EQUAL_STRING("https://a/1.mp3", q.front().url);
    q.popFront();
    TEST_ASSERT_EQUAL_UINT32(2, q.front().subjectId);
    TEST_ASSERT_EQUAL_STRING("https://a/2.mp3", q.front().url);
    q.popFront();
    TEST_ASSERT_TRUE(q.empty());
    q.popFront();  // popping an empty queue is a no-op
    TEST_ASSERT_TRUE(q.empty());
}

static void test_audioq_failure_moves_back_then_drops() {
    AudioQueue q;
    q.enqueue(1, 0, "https://a/1.mp3");
    q.enqueue(2, 0, "https://a/2.mp3");
    TEST_ASSERT_FALSE(q.failFront());  // A fails once: moves behind B
    TEST_ASSERT_EQUAL_UINT32(2, q.front().subjectId);
    TEST_ASSERT_FALSE(q.failFront());  // B fails once: moves behind A
    TEST_ASSERT_EQUAL_UINT32(1, q.front().subjectId);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kAudioQueueMaxFailures), 2);
    TEST_ASSERT_TRUE(q.failFront());   // A's second failure: dropped
    TEST_ASSERT_EQUAL_UINT32(1, q.size());
    TEST_ASSERT_EQUAL_UINT32(2, q.front().subjectId);
    TEST_ASSERT_EQUAL_STRING("https://a/2.mp3", q.front().url);
    TEST_ASSERT_FALSE(q.contains(1, 0));
}

static void test_audioq_failed_entry_can_requeue_after_drop() {
    AudioQueue q;
    q.enqueue(1, 0, "https://a/1.mp3");
    q.failFront();
    q.failFront();  // dropped
    TEST_ASSERT_TRUE(q.empty());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioQueue::Add::Added),
                          static_cast<int>(q.enqueue(1, 0, "https://a/1.mp3")));
    TEST_ASSERT_EQUAL_UINT32(0, q.front().failures);
}

static void test_audioq_account_change_empties() {
    AudioQueue q;
    q.useAccount(5);
    q.enqueue(1, 0, "https://a/1.mp3");
    q.useAccount(5);
    TEST_ASSERT_EQUAL_UINT32(1, q.size());
    q.useAccount(6);
    TEST_ASSERT_TRUE(q.empty());
    TEST_ASSERT_EQUAL_UINT32(6, q.accountKey());
}

// The fixed storage keeps each URL whole through pops, failures and reuse of a
// slot, with different lengths in the same queue.
static void test_audioq_fixed_storage_keeps_each_url() {
    AudioQueue q;
    const std::string urls[3] = {
        std::string("https://a/") + std::string(60, 'x') + ".mp3",
        "https://a/short.mp3",
        std::string("https://a/") + std::string(90, 'y') + ".mp3",
    };
    for (uint32_t i = 0; i < 3; ++i)
        TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioQueue::Add::Added),
                              static_cast<int>(q.enqueue(i + 1, 0, urls[i])));
    q.failFront();  // first moves to the back: 2, 3, 1
    TEST_ASSERT_EQUAL_STRING(urls[1].c_str(), q.front().url);
    q.popFront();   // 3, 1
    TEST_ASSERT_EQUAL_STRING(urls[2].c_str(), q.front().url);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioQueue::Add::Added),
                          static_cast<int>(q.enqueue(7, 1, "https://a/new.mp3")));
    q.popFront();   // 1, 7
    TEST_ASSERT_EQUAL_STRING(urls[0].c_str(), q.front().url);
    TEST_ASSERT_EQUAL_UINT32(1, q.front().subjectId);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(urls[0].size()), q.front().urlLen);
    q.popFront();
    TEST_ASSERT_EQUAL_STRING("https://a/new.mp3", q.front().url);
    TEST_ASSERT_EQUAL_UINT8(1, q.front().voice);
}

// The storage is inside the object (no heap): a queue of the maximum size is a
// few kilobytes, which is what the static instance costs at boot.
static void test_audioq_storage_is_bounded_in_size() {
    TEST_ASSERT_LESS_OR_EQUAL(kAudioQueueMax * (kAudioUrlMax + 16) + 64, sizeof(AudioQueue));
}

// A URL is kept up to kAudioUrlMax bytes. A longer one is refused, not cut, and
// the queue does not change.
static void test_audioq_refuses_url_longer_than_the_slot() {
    AudioQueue q;
    const std::string atLimit(kAudioUrlMax, 'u');
    const std::string tooLong(kAudioUrlMax + 1, 'v');
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioQueue::Add::Added),
                          static_cast<int>(q.enqueue(1, 0, atLimit)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioQueue::Add::TooLong),
                          static_cast<int>(q.enqueue(2, 0, tooLong)));
    TEST_ASSERT_FALSE(q.contains(2, 0));
    TEST_ASSERT_EQUAL_UINT32(1, q.size());
    TEST_ASSERT_EQUAL_STRING(atLimit.c_str(), q.front().url);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(kAudioUrlMax), q.front().urlLen);
}

// A refused URL does not take a slot even when the queue is otherwise full, and
// a full queue still reports Full for a URL that fits.
static void test_audioq_too_long_does_not_take_a_slot() {
    AudioQueue q;
    for (uint32_t id = 1; id <= kAudioQueueMax; ++id)
        q.enqueue(id, 0, "https://a/x.mp3");
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioQueue::Add::Full),
                          static_cast<int>(q.enqueue(99, 0, "https://a/x.mp3")));
    q.popFront();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioQueue::Add::TooLong),
                          static_cast<int>(q.enqueue(99, 0, std::string(kAudioUrlMax + 1, 'z'))));
    TEST_ASSERT_EQUAL_UINT32(kAudioQueueMax - 1, q.size());
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
    // Measured on the MEM-02 core: the drain waited here after one clip.
    TEST_ASSERT_TRUE(audioDrainHeapOk(34804, 139304));
}

// AUD-02 refill: only a requested refill, with the batch in RAM, the card up
// and a usable token, queues anything. Each condition alone is not enough.
static void test_audioq_refill_rule() {
    TEST_ASSERT_TRUE(audioRefillWanted(true, true, true, true));
    TEST_ASSERT_FALSE(audioRefillWanted(false, true, true, true));
    TEST_ASSERT_FALSE(audioRefillWanted(true, false, true, true));
    TEST_ASSERT_FALSE(audioRefillWanted(true, true, false, true));
    TEST_ASSERT_FALSE(audioRefillWanted(true, true, true, false));
}

// The clip queued for the preferred voice is its own URL; when that voice has
// none, the other voice's URL is queued, and only when neither has one is
// nothing queued.
static void test_audioq_queue_choice_own_voice_then_other() {
    const std::string both[2] = {"https://a/f.mp3", "https://a/m.mp3"};
    const std::string femaleOnly[2] = {"https://a/f.mp3", ""};
    const std::string maleOnly[2] = {"", "https://a/m.mp3"};
    const std::string none[2] = {"", ""};
    uint8_t voice = 9;
    TEST_ASSERT_TRUE(audioQueueChoice(both, 0, &voice));
    TEST_ASSERT_EQUAL_UINT8(0, voice);
    TEST_ASSERT_TRUE(audioQueueChoice(both, 1, &voice));
    TEST_ASSERT_EQUAL_UINT8(1, voice);
    TEST_ASSERT_TRUE(audioQueueChoice(maleOnly, 0, &voice));
    TEST_ASSERT_EQUAL_UINT8(1, voice);
    TEST_ASSERT_TRUE(audioQueueChoice(femaleOnly, 1, &voice));
    TEST_ASSERT_EQUAL_UINT8(0, voice);
    TEST_ASSERT_FALSE(audioQueueChoice(none, 0, &voice));
    TEST_ASSERT_FALSE(audioQueueChoice(both, 0, nullptr));
}

// Per voice, the clip that speaks the primary reading wins over the first MP3 of
// the voice. Each voice is chosen on its own (a male clip with the primary reading
// beats a female one without it, for the male slot only).
static void test_audioq_subject_urls_primary_reading_per_voice() {
    Subject s;
    s.id = 500;
    s.type = SubjectType::Vocabulary;
    s.readings = {{"ちかぢか", true, true, ReadingType::Onyomi},
                  {"きんきん", false, true, ReadingType::Unknown}};
    s.audios = {clip("https://a/f-kin.mp3", 0, "きんきん"),
                clip("https://a/f-chika.mp3", 0, "ちかぢか"),
                clip("https://a/m-kin.mp3", 1, "きんきん"),
                clip("https://a/m-chika.mp3", 1, "ちかぢか")};
    audioChooseSubjectUrls(s);
    TEST_ASSERT_EQUAL_STRING("https://a/f-chika.mp3", s.audioUrl[0].c_str());
    TEST_ASSERT_EQUAL_STRING("https://a/m-chika.mp3", s.audioUrl[1].c_str());

    // The male voice has only the other reading: it still gets its clip, and the
    // female voice keeps the primary one.
    s.audios = {clip("https://a/f-chika.mp3", 0, "ちかぢか"),
                clip("https://a/m-kin.mp3", 1, "きんきん")};
    audioChooseSubjectUrls(s);
    TEST_ASSERT_EQUAL_STRING("https://a/f-chika.mp3", s.audioUrl[0].c_str());
    TEST_ASSERT_EQUAL_STRING("https://a/m-kin.mp3", s.audioUrl[1].c_str());

    // One voice only: the other URL stays empty.
    s.audios = {clip("https://a/m-chika.mp3", 1, "ちかぢか")};
    audioChooseSubjectUrls(s);
    TEST_ASSERT_TRUE(s.audioUrl[0].empty());
    TEST_ASSERT_EQUAL_STRING("https://a/m-chika.mp3", s.audioUrl[1].c_str());
}

// An OGG clip is never chosen, and a subject that is not vocabulary or kana
// vocabulary gets no URLs even when it has clips.
static void test_audioq_subject_urls_mp3_and_type_only() {
    Subject ogg;
    ogg.type = SubjectType::Vocabulary;
    ogg.readings = {{"かな", true, true, ReadingType::Unknown}};
    ogg.audios = {clip("https://a/f.ogg", 0, "かな", "audio/ogg")};
    audioChooseSubjectUrls(ogg);
    TEST_ASSERT_TRUE(ogg.audioUrl[0].empty());
    TEST_ASSERT_TRUE(ogg.audioUrl[1].empty());

    Subject kanji;
    kanji.type = SubjectType::Kanji;
    kanji.readings = {{"かな", true, true, ReadingType::Onyomi}};
    kanji.audioUrl[0] = "https://a/stale.mp3";
    kanji.audios = {clip("https://a/k.mp3", 0, "かな")};
    audioChooseSubjectUrls(kanji);
    TEST_ASSERT_TRUE(kanji.audioUrl[0].empty());
    TEST_ASSERT_TRUE(kanji.audioUrl[1].empty());

    // A kana vocabulary word with no reading list speaks its own characters.
    Subject kana;
    kana.type = SubjectType::KanaVocabulary;
    kana.characters = "かな";
    kana.audios = {clip("https://a/kana.mp3", 0, "かな")};
    audioChooseSubjectUrls(kana);
    TEST_ASSERT_EQUAL_STRING("https://a/kana.mp3", kana.audioUrl[0].c_str());
}

void run_audioq_tests() {
    RUN_TEST(test_audioq_enqueue_dedupes_by_subject_and_voice);
    RUN_TEST(test_audioq_bounded_at_max);
    RUN_TEST(test_audioq_fifo_and_pop);
    RUN_TEST(test_audioq_failure_moves_back_then_drops);
    RUN_TEST(test_audioq_failed_entry_can_requeue_after_drop);
    RUN_TEST(test_audioq_account_change_empties);
    RUN_TEST(test_audioq_fixed_storage_keeps_each_url);
    RUN_TEST(test_audioq_storage_is_bounded_in_size);
    RUN_TEST(test_audioq_refuses_url_longer_than_the_slot);
    RUN_TEST(test_audioq_too_long_does_not_take_a_slot);
    RUN_TEST(test_audioq_drain_allowed_when_all_hold);
    RUN_TEST(test_audioq_drain_blocked_by_each_rule);
    RUN_TEST(test_audioq_drain_spacing_holds_until_passed);
    RUN_TEST(test_audioq_heap_thresholds);
    RUN_TEST(test_audioq_refill_rule);
    RUN_TEST(test_audioq_queue_choice_own_voice_then_other);
    RUN_TEST(test_audioq_subject_urls_primary_reading_per_voice);
    RUN_TEST(test_audioq_subject_urls_mp3_and_type_only);
}

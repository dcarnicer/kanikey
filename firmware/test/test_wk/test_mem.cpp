// Memory track B (docs/spec/12-memory.md): the xorshift32 generator that replaced
// std::mt19937 (MEM-11), the fixed header-line buffer (MEM-17) and the release of
// the clip structs once their URLs are chosen (MEM-13).
#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "wk/AudioPlan.h"
#include "wk/HeaderLine.h"
#include "wk/IdSample.h"
#include "wk/Types.h"
#include "wk/Xorshift.h"

using namespace kk;

namespace {

void feed(HeaderLine& line, const char* text) {
    for (const char* p = text; *p; ++p) line.push(*p);
}

}  // namespace

// Marsaglia's 13/17/5 step. Reference values from an independent computation.
static void test_xorshift_known_values() {
    Xorshift32 rng(1);
    TEST_ASSERT_EQUAL_UINT32(270369u, rng());
    TEST_ASSERT_EQUAL_UINT32(67634689u, rng());
    TEST_ASSERT_EQUAL_UINT32(2647435461u, rng());

    Xorshift32 batch(0x9E3779B9u);
    TEST_ASSERT_EQUAL_UINT32(1359758873u, batch());
    TEST_ASSERT_EQUAL_UINT32(3761132862u, batch());
    TEST_ASSERT_EQUAL_UINT32(2075758394u, batch());
}

// A zero seed would be stuck at 0; it is replaced by 1 (the IdSample rule).
static void test_xorshift_zero_seed_is_one() {
    Xorshift32 zero(0);
    Xorshift32 one(1);
    for (int i = 0; i < 1000; ++i) TEST_ASSERT_EQUAL_UINT32(one(), zero());
}

static void test_xorshift_never_returns_zero() {
    Xorshift32 rng(0x9E3779B9u);
    for (int i = 0; i < 100000; ++i) TEST_ASSERT_NOT_EQUAL(0u, rng());
}

// It is a UniformRandomBitGenerator: std::shuffle gives a permutation, the same
// one for the same seed, and a different one for another seed.
static void test_xorshift_drives_std_shuffle() {
    std::vector<int> a(50);
    std::iota(a.begin(), a.end(), 0);
    std::vector<int> b = a;
    std::vector<int> c = a;
    Xorshift32 ra(777), rb(777), rc(778);
    std::shuffle(a.begin(), a.end(), ra);
    std::shuffle(b.begin(), b.end(), rb);
    std::shuffle(c.begin(), c.end(), rc);
    TEST_ASSERT_TRUE(a == b);
    TEST_ASSERT_FALSE(a == c);
    std::vector<int> sorted = a;
    std::sort(sorted.begin(), sorted.end());
    for (int i = 0; i < 50; ++i) TEST_ASSERT_EQUAL(i, sorted[i]);
}

// IdSample moved from an inline loop to Xorshift32. The golden order is the
// one the inline loop gave (replicated before the change), so batches sampled
// on the device keep their order.
static void test_sample_ids_order_unchanged() {
    std::vector<uint32_t> ids(10);
    std::iota(ids.begin(), ids.end(), 0u);
    const std::vector<uint32_t> got = sampleIds(ids, 10, 1234);
    const uint32_t golden[] = {7, 8, 0, 3, 9, 4, 5, 2, 6, 1};
    TEST_ASSERT_EQUAL(10, got.size());
    for (size_t i = 0; i < 10; ++i) TEST_ASSERT_EQUAL_UINT32(golden[i], got[i]);
}

// MEM-17: a header line keeps its first kCapacity bytes; the rest is dropped and
// the flag says so.
static void test_header_line_truncates_long_line() {
    HeaderLine line;
    line.clear();
    for (size_t i = 0; i < HeaderLine::kCapacity + 40; ++i) line.push('x');
    TEST_ASSERT_EQUAL(HeaderLine::kCapacity, line.size());
    TEST_ASSERT_TRUE(line.truncated());
    TEST_ASSERT_EQUAL(0, line.c_str()[HeaderLine::kCapacity] ? 1 : 0);
}

static void test_header_line_blank_is_empty_or_cr() {
    HeaderLine line;
    line.clear();
    TEST_ASSERT_TRUE(line.blank());  // an empty line
    feed(line, "\r");
    TEST_ASSERT_TRUE(line.blank());  // CRLF with nothing before it
    line.clear();
    feed(line, "a\r");
    TEST_ASSERT_FALSE(line.blank());
    line.clear();
    feed(line, " \r");
    TEST_ASSERT_FALSE(line.blank());
}

static void test_header_line_clear_resets_truncation() {
    HeaderLine line;
    for (size_t i = 0; i < HeaderLine::kCapacity + 1; ++i) line.push('y');
    TEST_ASSERT_TRUE(line.truncated());
    line.clear();
    TEST_ASSERT_EQUAL(0, line.size());
    TEST_ASSERT_FALSE(line.truncated());
    TEST_ASSERT_EQUAL_STRING("", line.c_str());
}

static void test_header_line_trim_like_string() {
    HeaderLine line;
    feed(line, " \tOK\r\n ");
    line.trim();
    TEST_ASSERT_EQUAL_STRING("OK", line.c_str());
    TEST_ASSERT_EQUAL(2, line.size());
    HeaderLine status;
    feed(status, "HTTP/1.1 200 OK\r");
    status.trim();
    TEST_ASSERT_EQUAL_STRING("HTTP/1.1 200 OK", status.c_str());
}

// The client's header checks: lower-case first, then the prefix, the token or the
// number after the name (String::substring(n).toInt() in the old code).
static void test_header_line_checks_match_old_client() {
    HeaderLine cl;
    feed(cl, "Content-Length: 1234\r");
    cl.toLower();
    TEST_ASSERT_TRUE(cl.hasPrefix("content-length:"));
    TEST_ASSERT_EQUAL(1234, cl.valueFrom(15));

    HeaderLine te;
    feed(te, "Transfer-Encoding: chunked\r");
    te.toLower();
    TEST_ASSERT_TRUE(te.hasPrefix("transfer-encoding:"));
    TEST_ASSERT_TRUE(te.contains("chunked"));

    HeaderLine conn;
    feed(conn, "Connection: Keep-Alive\r");
    conn.toLower();
    TEST_ASSERT_TRUE(conn.hasPrefix("connection:"));
    TEST_ASSERT_TRUE(conn.contains("keep-alive"));
    TEST_ASSERT_FALSE(conn.contains("close"));

    HeaderLine reset;
    feed(reset, "RateLimit-Reset: 1700000000\r");
    reset.toLower();
    TEST_ASSERT_TRUE(reset.hasPrefix("ratelimit-reset:"));
    TEST_ASSERT_EQUAL(1700000000L, reset.valueFrom(16));

    HeaderLine remaining;
    feed(remaining, "RateLimit-Remaining: 57\r");
    remaining.toLower();
    TEST_ASSERT_TRUE(remaining.hasPrefix("ratelimit-remaining:"));
    TEST_ASSERT_EQUAL(57, remaining.valueFrom(20));

    // Not a match: a different name, and a line shorter than the prefix.
    HeaderLine other;
    feed(other, "Content-Type: application/json\r");
    other.toLower();
    TEST_ASSERT_FALSE(other.hasPrefix("content-length:"));
    HeaderLine short_line;
    feed(short_line, "conn");
    TEST_ASSERT_FALSE(short_line.hasPrefix("connection:"));
}

// A long header value that is cut still has its name and a parsed value when
// the number comes first; the cut only drops the tail.
static void test_header_line_long_value_keeps_prefix() {
    HeaderLine line;
    feed(line, "Content-Length: 4096");
    for (int i = 0; i < 300; ++i) line.push(' ');
    line.toLower();
    TEST_ASSERT_TRUE(line.truncated());
    TEST_ASSERT_TRUE(line.hasPrefix("content-length:"));
    TEST_ASSERT_EQUAL(4096, line.valueFrom(15));
}

// MEM-13: once the two URLs are chosen the clip structs are released (capacity
// too), and the URLs themselves are right.
static void test_audio_clips_released_after_url_choice() {
    auto clip = [](const char* url, uint8_t voice, const char* pronunciation) {
        AudioClip c;
        c.url = url;
        c.contentType = "audio/mpeg";
        c.pronunciation = pronunciation;
        c.voice = voice;
        return c;
    };
    Subject s;
    s.id = 30;
    s.type = SubjectType::Vocabulary;
    s.characters = "日本";
    s.audios = {clip("https://a/f.mp3", 0, "にほん"), clip("https://a/m.mp3", 1, "にほん")};
    audioChooseSubjectUrls(s);
    TEST_ASSERT_EQUAL_STRING("https://a/f.mp3", s.audioUrl[0].c_str());
    TEST_ASSERT_EQUAL_STRING("https://a/m.mp3", s.audioUrl[1].c_str());
    TEST_ASSERT_TRUE(s.audios.empty());
    TEST_ASSERT_EQUAL(0, s.audios.capacity());

    // A subject that does not play audio also releases its clips.
    Subject kanji;
    kanji.id = 1;
    kanji.type = SubjectType::Kanji;
    kanji.audios = {clip("https://a/k.mp3", 0, "か")};
    audioChooseSubjectUrls(kanji);
    TEST_ASSERT_TRUE(kanji.audioUrl[0].empty());
    TEST_ASSERT_TRUE(kanji.audios.empty());
    TEST_ASSERT_EQUAL(0, kanji.audios.capacity());
}

void run_mem_tests() {
    RUN_TEST(test_xorshift_known_values);
    RUN_TEST(test_xorshift_zero_seed_is_one);
    RUN_TEST(test_xorshift_never_returns_zero);
    RUN_TEST(test_xorshift_drives_std_shuffle);
    RUN_TEST(test_sample_ids_order_unchanged);
    RUN_TEST(test_header_line_truncates_long_line);
    RUN_TEST(test_header_line_blank_is_empty_or_cr);
    RUN_TEST(test_header_line_clear_resets_truncation);
    RUN_TEST(test_header_line_trim_like_string);
    RUN_TEST(test_header_line_checks_match_old_client);
    RUN_TEST(test_header_line_long_value_keeps_prefix);
    RUN_TEST(test_audio_clips_released_after_url_choice);
}

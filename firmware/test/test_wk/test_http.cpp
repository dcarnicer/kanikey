// SPEED-05: HTTP/1.1 chunked transfer decoding. Pure native tests.
#include <unity.h>

#include <cstring>
#include <string>

#include "wk/HttpChunked.h"

using namespace kk;

namespace {

// Copies the wire form into a buffer with room for an untouched canary after
// it, then decodes. Returns the decode result and leaves the status in `st`.
bool decode(const char* wire, size_t cap, std::string* body, ChunkedStatus* st) {
    char buffer[512];
    const size_t len = std::strlen(wire);
    std::memset(buffer, 'X', sizeof buffer);
    std::memcpy(buffer, wire, len);
    size_t outLen = 0;
    const bool ok = chunkedDecodeInPlace(buffer, len, cap, &outLen, st);
    for (size_t i = len; i < sizeof buffer; ++i) {
        if (buffer[i] != 'X') return false;  // wrote past the wire body
    }
    body->assign(ok ? std::string(buffer, outLen) : std::string());
    return ok;
}

}  // namespace

static void test_chunked_single_chunk() {
    std::string body;
    ChunkedStatus st = ChunkedStatus::Malformed;
    TEST_ASSERT_TRUE(decode("5\r\nhello\r\n0\r\n\r\n", 32, &body, &st));
    TEST_ASSERT_EQUAL(ChunkedStatus::Complete, st);
    TEST_ASSERT_EQUAL_STRING("hello", body.c_str());
}

static void test_chunked_multiple_chunks() {
    std::string body;
    ChunkedStatus st = ChunkedStatus::Malformed;
    TEST_ASSERT_TRUE(decode("3\r\nfoo\r\n3\r\nbar\r\n0\r\n\r\n", 32, &body, &st));
    TEST_ASSERT_EQUAL(ChunkedStatus::Complete, st);
    TEST_ASSERT_EQUAL_STRING("foobar", body.c_str());
}

static void test_chunked_extension_and_trailer() {
    std::string body;
    ChunkedStatus st = ChunkedStatus::Malformed;
    TEST_ASSERT_TRUE(decode("5;name=value\r\nhello\r\n0\r\nX-Trace: 42\r\nX-Other: y\r\n\r\n",
                            32, &body, &st));
    TEST_ASSERT_EQUAL(ChunkedStatus::Complete, st);
    TEST_ASSERT_EQUAL_STRING("hello", body.c_str());
}

static void test_chunked_truncated_is_incomplete() {
    std::string body;
    ChunkedStatus st = ChunkedStatus::Complete;
    TEST_ASSERT_FALSE(decode("5\r\nhel", 32, &body, &st));
    TEST_ASSERT_EQUAL(ChunkedStatus::Incomplete, st);
    TEST_ASSERT_EQUAL_UINT32(0, body.size());

    // The size line is complete but the payload is not.
    TEST_ASSERT_FALSE(decode("5\r\nhello", 32, &body, &st));
    TEST_ASSERT_EQUAL(ChunkedStatus::Incomplete, st);

    // The closing CRLF of a payload is still an incomplete message.
    TEST_ASSERT_FALSE(decode("5\r\nhello\r", 32, &body, &st));
    TEST_ASSERT_EQUAL(ChunkedStatus::Incomplete, st);

    // A trailer line without its terminating empty line is also incomplete.
    TEST_ASSERT_FALSE(decode("0\r\nX-Trace: 42\r\n", 32, &body, &st));
    TEST_ASSERT_EQUAL(ChunkedStatus::Incomplete, st);
}

static void test_chunked_bad_size_is_error() {
    std::string body;
    ChunkedStatus st = ChunkedStatus::Complete;
    TEST_ASSERT_FALSE(decode("zz\r\nhello\r\n0\r\n\r\n", 32, &body, &st));
    TEST_ASSERT_EQUAL(ChunkedStatus::Malformed, st);

    TEST_ASSERT_FALSE(decode("\r\nhello\r\n0\r\n\r\n", 32, &body, &st));
    TEST_ASSERT_EQUAL(ChunkedStatus::Malformed, st);

    // A chunk that is not followed by CRLF is malformed too.
    TEST_ASSERT_FALSE(decode("5\r\nhelloXX0\r\n\r\n", 32, &body, &st));
    TEST_ASSERT_EQUAL(ChunkedStatus::Malformed, st);
}

static void test_chunked_respects_cap() {
    std::string body;
    ChunkedStatus st = ChunkedStatus::Complete;
    TEST_ASSERT_FALSE(decode("5\r\nhello\r\n0\r\n\r\n", 4, &body, &st));
    TEST_ASSERT_EQUAL(ChunkedStatus::TooLarge, st);
    TEST_ASSERT_EQUAL_UINT32(0, body.size());

    // Exactly cap is allowed.
    TEST_ASSERT_TRUE(decode("5\r\nhello\r\n0\r\n\r\n", 5, &body, &st));
    TEST_ASSERT_EQUAL(ChunkedStatus::Complete, st);
    TEST_ASSERT_EQUAL_STRING("hello", body.c_str());
}

static void test_chunked_in_place_output() {
    char buffer[64];
    const char* wire = "4\r\nabcd\r\n2\r\nef\r\n0\r\n\r\n";
    const size_t len = std::strlen(wire);
    std::memset(buffer, 'X', sizeof buffer);
    std::memcpy(buffer, wire, len);
    char* const start = buffer;
    size_t outLen = 0;
    ChunkedStatus st = ChunkedStatus::Malformed;
    TEST_ASSERT_TRUE(chunkedDecodeInPlace(buffer, len, sizeof buffer, &outLen, &st));
    TEST_ASSERT_EQUAL(ChunkedStatus::Complete, st);
    TEST_ASSERT_EQUAL_UINT32(6, outLen);
    TEST_ASSERT_EQUAL_PTR(start, buffer);
    TEST_ASSERT_EQUAL_STRING_LEN("abcdef", buffer, outLen);
    // Framing is gone: the decoded body is shorter than the wire message.
    TEST_ASSERT_LESS_THAN_UINT32(len, outLen);
    // Nothing beyond the wire message was touched.
    for (size_t i = len; i < sizeof buffer; ++i) TEST_ASSERT_EQUAL_CHAR('X', buffer[i]);
}

void run_http_tests() {
    RUN_TEST(test_chunked_single_chunk);
    RUN_TEST(test_chunked_multiple_chunks);
    RUN_TEST(test_chunked_extension_and_trailer);
    RUN_TEST(test_chunked_truncated_is_incomplete);
    RUN_TEST(test_chunked_bad_size_is_error);
    RUN_TEST(test_chunked_respects_cap);
    RUN_TEST(test_chunked_in_place_output);
}

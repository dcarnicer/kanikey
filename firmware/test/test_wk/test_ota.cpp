#include <unity.h>

#include <string>

#include "ota/HttpHeader.h"

using namespace kk::ota;

// The helper must keep the Location in the middle of GitHub's header block,
// where a ~4 KB Content-Security-Policy line comes first or last.
static void test_header_location_after_long_csp() {
    ResponseHeaders headers;
    const std::string csp = "Content-Security-Policy: " + std::string(4000, 'a');
    TEST_ASSERT_TRUE(applyHeaderLine(csp, &headers));
    TEST_ASSERT_TRUE(headers.location.empty());
    TEST_ASSERT_TRUE(applyHeaderLine("Location: https://release.example/x.bin", &headers));
    TEST_ASSERT_EQUAL_STRING("https://release.example/x.bin", headers.location.c_str());
    TEST_ASSERT_EQUAL_INT(-1, static_cast<int>(headers.contentLength));
}

static void test_header_location_before_long_csp() {
    ResponseHeaders headers;
    TEST_ASSERT_TRUE(applyHeaderLine("Location: https://release.example/x.bin", &headers));
    const std::string csp = "Content-Security-Policy: " + std::string(4000, 'b');
    TEST_ASSERT_TRUE(applyHeaderLine(csp, &headers));
    TEST_ASSERT_EQUAL_STRING("https://release.example/x.bin", headers.location.c_str());
}

static void test_header_long_csp_after_location() {
    ResponseHeaders headers;
    TEST_ASSERT_TRUE(applyHeaderLine("Content-Length: 7", &headers));
    TEST_ASSERT_TRUE(applyHeaderLine("Location: https://a.test/x", &headers));
    const std::string csp = "Set-Cookie: " + std::string(4096, 'c');
    TEST_ASSERT_TRUE(applyHeaderLine(csp, &headers));
    TEST_ASSERT_FALSE(applyHeaderLine("\r\n", &headers));  // end of block
    TEST_ASSERT_EQUAL_STRING("https://a.test/x", headers.location.c_str());
    TEST_ASSERT_EQUAL_INT(7, static_cast<int>(headers.contentLength));
}

static void test_header_crlf_trimmed() {
    ResponseHeaders headers;
    TEST_ASSERT_TRUE(applyHeaderLine("  Location:   https://a.test/x \r\n", &headers));
    TEST_ASSERT_EQUAL_STRING("https://a.test/x", headers.location.c_str());
    TEST_ASSERT_TRUE(applyHeaderLine(" Content-Length:\t42 \r\n", &headers));
    TEST_ASSERT_EQUAL_INT(42, static_cast<int>(headers.contentLength));
}

static void test_header_name_case_insensitive() {
    ResponseHeaders headers;
    TEST_ASSERT_TRUE(applyHeaderLine("lOcAtIoN: https://a.test/x", &headers));
    TEST_ASSERT_EQUAL_STRING("https://a.test/x", headers.location.c_str());
    TEST_ASSERT_TRUE(applyHeaderLine("CONTENT-LENGTH: 9", &headers));
    TEST_ASSERT_EQUAL_INT(9, static_cast<int>(headers.contentLength));
    TEST_ASSERT_TRUE(applyHeaderLine("location: https://b.test/y", &headers));
    TEST_ASSERT_EQUAL_STRING("https://b.test/y", headers.location.c_str());
}

static void test_header_location_4096_accepted() {
    ResponseHeaders headers;
    const std::string value(4096, 'd');
    TEST_ASSERT_TRUE(applyHeaderLine("Location: " + value, &headers));
    TEST_ASSERT_EQUAL_UINT32(kMaxLocationChars, headers.location.size());
    TEST_ASSERT_EQUAL_STRING(value.c_str(), headers.location.c_str());
}

static void test_header_location_over_4096_rejected() {
    ResponseHeaders headers;
    const std::string value(4097, 'e');
    // Consumed and dropped, not a "stop" signal: the caller then sees an empty
    // Location and reports an HTTP error instead of following a cut URL.
    TEST_ASSERT_TRUE(applyHeaderLine("Location: " + value, &headers));
    TEST_ASSERT_TRUE(headers.location.empty());
}

static void test_header_content_length_parsed() {
    ResponseHeaders headers;
    TEST_ASSERT_TRUE(applyHeaderLine("Content-Length: 0", &headers));
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(headers.contentLength));
    TEST_ASSERT_TRUE(applyHeaderLine("Content-Length: 1461584", &headers));
    TEST_ASSERT_EQUAL_INT(1461584, static_cast<int>(headers.contentLength));
    // Invalid values leave the last good one in place.
    TEST_ASSERT_TRUE(applyHeaderLine("Content-Length: 12x", &headers));
    TEST_ASSERT_EQUAL_INT(1461584, static_cast<int>(headers.contentLength));
    TEST_ASSERT_TRUE(applyHeaderLine("Content-Length:", &headers));
    TEST_ASSERT_EQUAL_INT(1461584, static_cast<int>(headers.contentLength));
}

void run_ota_tests() {
    RUN_TEST(test_header_location_after_long_csp);
    RUN_TEST(test_header_location_before_long_csp);
    RUN_TEST(test_header_long_csp_after_location);
    RUN_TEST(test_header_crlf_trimmed);
    RUN_TEST(test_header_name_case_insensitive);
    RUN_TEST(test_header_location_4096_accepted);
    RUN_TEST(test_header_location_over_4096_rejected);
    RUN_TEST(test_header_content_length_parsed);
}

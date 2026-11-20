#include <unity.h>

#include <string>

#include "wk/DevOverrides.h"

using namespace kk;

// Body cap: no override leaves the request cap alone, capped by the buffer.
static void test_body_cap_without_override_is_request_cap() {
    TEST_ASSERT_EQUAL_UINT32(5000u, static_cast<uint32_t>(effectiveBodyCap(5000, 32768, 0)));
    TEST_ASSERT_EQUAL_UINT32(32768u, static_cast<uint32_t>(effectiveBodyCap(40000, 32768, 0)));
}

// The override lowers the cap, which is what forces the 5 -> 2 -> 1 fallback.
static void test_body_cap_override_lowers_cap() {
    TEST_ASSERT_EQUAL_UINT32(2048u, static_cast<uint32_t>(effectiveBodyCap(32768, 32768, 2048)));
    TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(effectiveBodyCap(32768, 32768, 1)));
}

// The override never raises a cap, and never goes past the real buffer.
static void test_body_cap_override_never_raises_cap() {
    TEST_ASSERT_EQUAL_UINT32(1000u, static_cast<uint32_t>(effectiveBodyCap(1000, 32768, 2048)));
    TEST_ASSERT_EQUAL_UINT32(32768u,
                             static_cast<uint32_t>(effectiveBodyCap(32768, 32768, 999999)));
}

static void test_dev_host_accepts_lan_names() {
    TEST_ASSERT_TRUE(devHostAllowed("192.168.1.20", 8443));
    TEST_ASSERT_TRUE(devHostAllowed("dev-box.local", 443));
    TEST_ASSERT_TRUE(devHostAllowed("slow", 1));
    TEST_ASSERT_TRUE(devHostAllowed("slow", 65535));
}

// The token must never be sent to the real API with certificate checks off.
static void test_dev_host_refuses_wanikani() {
    TEST_ASSERT_FALSE(devHostAllowed("api.wanikani.com", 443));
    TEST_ASSERT_FALSE(devHostAllowed("API.WaniKani.com", 443));
    TEST_ASSERT_FALSE(devHostAllowed("wanikani.example", 8443));
    TEST_ASSERT_FALSE(devHostAllowed("mirror-WANIKANI-1.lan", 8443));
}

static void test_dev_host_refuses_bad_ports() {
    TEST_ASSERT_FALSE(devHostAllowed("192.168.1.20", 0));
    TEST_ASSERT_FALSE(devHostAllowed("192.168.1.20", 65536));
}

static void test_dev_host_refuses_bad_names() {
    TEST_ASSERT_FALSE(devHostAllowed("", 443));
    TEST_ASSERT_FALSE(devHostAllowed("bad host", 443));
    TEST_ASSERT_FALSE(devHostAllowed("a/b", 443));
    TEST_ASSERT_FALSE(devHostAllowed("http://x", 443));
    TEST_ASSERT_FALSE(devHostAllowed("-leading", 443));
    TEST_ASSERT_FALSE(devHostAllowed("trailing.", 443));
    const std::string longName(64, 'a');
    TEST_ASSERT_FALSE(devHostAllowed(longName, 443));
    TEST_ASSERT_TRUE(devHostAllowed(std::string(63, 'a'), 443));
}

void run_devhooks_tests() {
    RUN_TEST(test_body_cap_without_override_is_request_cap);
    RUN_TEST(test_body_cap_override_lowers_cap);
    RUN_TEST(test_body_cap_override_never_raises_cap);
    RUN_TEST(test_dev_host_accepts_lan_names);
    RUN_TEST(test_dev_host_refuses_wanikani);
    RUN_TEST(test_dev_host_refuses_bad_ports);
    RUN_TEST(test_dev_host_refuses_bad_names);
}

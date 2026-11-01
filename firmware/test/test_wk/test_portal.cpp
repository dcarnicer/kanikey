// Native tests for wk/RandomCode: the setup AP key and the station unlock code.
#include <unity.h>

#include <cstring>

#include "wk/RandomCode.h"

using namespace kk;

namespace {

// Small LCG so every test starts from a fixed point; the real code uses
// esp_random().
uint32_t gRnd = 0;

uint32_t testRnd() {
    gRnd = gRnd * 1664525u + 1013904223u;
    return gRnd >> 8;
}

bool onlyFrom(const char* code, const char* alphabet) {
    for (size_t i = 0; code[i]; ++i)
        if (!strchr(alphabet, code[i])) return false;
    return true;
}

}  // namespace

static void test_ap_password_is_10_chars() {
    char code[20] = "";
    gRnd = 7;
    makeRandomCode(testRnd, kCodeAlphabet, 10, code, sizeof code);
    TEST_ASSERT_EQUAL_UINT(10u, strlen(code));
}

static void test_ap_password_uses_alphabet() {
    char code[20] = "";
    gRnd = 11;
    makeRandomCode(testRnd, kCodeAlphabet, 10, code, sizeof code);
    TEST_ASSERT_TRUE(onlyFrom(code, kCodeAlphabet));
}

static void test_ap_password_no_confusables() {
    char code[20] = "";
    for (uint32_t seed = 1; seed <= 64; ++seed) {
        gRnd = seed;
        makeRandomCode(testRnd, kCodeAlphabet, 10, code, sizeof code);
        TEST_ASSERT_NULL(strpbrk(code, "0O1Iil"));
    }
}

static void test_ap_password_changes_between_starts() {
    char first[20] = "";
    char second[20] = "";
    char again[20] = "";
    gRnd = 3;
    makeRandomCode(testRnd, kCodeAlphabet, 10, first, sizeof first);
    gRnd = 4;
    makeRandomCode(testRnd, kCodeAlphabet, 10, second, sizeof second);
    gRnd = 3;
    makeRandomCode(testRnd, kCodeAlphabet, 10, again, sizeof again);
    TEST_ASSERT_EQUAL_STRING(first, again);
    TEST_ASSERT_TRUE(strcmp(first, second) != 0);
}

static void test_random_code_digits_only() {
    char code[8] = "";
    for (uint32_t seed = 1; seed <= 64; ++seed) {
        gRnd = seed;
        makeRandomCode(testRnd, kDigitAlphabet, 6, code, sizeof code);
        TEST_ASSERT_EQUAL_UINT(6u, strlen(code));
        TEST_ASSERT_TRUE(onlyFrom(code, "0123456789"));
    }
}

static void test_random_code_length() {
    char code[20] = "";
    gRnd = 5;
    makeRandomCode(testRnd, kDigitAlphabet, 6, code, sizeof code);
    TEST_ASSERT_EQUAL_UINT(6u, strlen(code));
    gRnd = 5;
    makeRandomCode(testRnd, kCodeAlphabet, 16, code, sizeof code);
    TEST_ASSERT_EQUAL_UINT(16u, strlen(code));
}

static void test_random_code_short_buffer_stays_empty() {
    char code[4] = "xx";
    gRnd = 9;
    makeRandomCode(testRnd, kDigitAlphabet, 6, code, sizeof code);
    TEST_ASSERT_EQUAL_STRING("", code);
}

void run_portal_tests() {
    RUN_TEST(test_ap_password_is_10_chars);
    RUN_TEST(test_ap_password_uses_alphabet);
    RUN_TEST(test_ap_password_no_confusables);
    RUN_TEST(test_ap_password_changes_between_starts);
    RUN_TEST(test_random_code_digits_only);
    RUN_TEST(test_random_code_length);
    RUN_TEST(test_random_code_short_buffer_stays_empty);
}

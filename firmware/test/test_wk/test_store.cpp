#include <unity.h>

#include <cstdio>
#include <string>

#include "wk/LevelProgress.h"
#include "wk/Time.h"

using namespace kk;

namespace {
// Builds a /v2/level_progressions collection with one entry per level. Each
// entry has a distinct unlocked_at date so a test can tell which one was kept.
std::string syntheticProgressions(int count) {
    std::string body = "{\"object\":\"collection\",\"total_count\":";
    body += std::to_string(count);
    body += ",\"data\":[";
    char entry[256];
    for (int level = 1; level <= count; ++level) {
        if (level > 1) body += ',';
        const int day = (level % 28) + 1;
        std::snprintf(entry, sizeof entry,
                      "{\"id\":%d,\"object\":\"level_progression\",\"data\":{"
                      "\"level\":%d,\"unlocked_at\":\"2026-01-%02dT12:00:00Z\","
                      "\"started_at\":null,\"passed_at\":null,\"completed_at\":null,"
                      "\"abandoned_at\":null}}",
                      level, level, day);
        body += entry;
    }
    body += "]}";
    return body;
}

uint32_t iso(const char* text) {
    uint32_t epoch = 0;
    parseIso8601(text, &epoch);
    return epoch;
}
}  // namespace

static void test_level_progressions_60_levels_keeps_current() {
    const std::string body = syntheticProgressions(60);
    LevelProgress progress;
    TEST_ASSERT_TRUE(parseLevelProgressionsText(body, 42, &progress));
    TEST_ASSERT_TRUE(progress.valid);
    TEST_ASSERT_EQUAL(42, progress.level);
    TEST_ASSERT_EQUAL_UINT32(iso("2026-01-15T12:00:00Z"), progress.unlockedAt);
    TEST_ASSERT_EQUAL_UINT32(0, progress.startedAt);
    TEST_ASSERT_EQUAL_UINT32(0, progress.passedAt);
    TEST_ASSERT_EQUAL_UINT32(0, progress.completedAt);
}

static void test_level_progressions_missing_current_uses_highest() {
    const std::string body = syntheticProgressions(3);
    LevelProgress progress;
    TEST_ASSERT_TRUE(parseLevelProgressionsText(body, 99, &progress));
    TEST_ASSERT_TRUE(progress.valid);
    TEST_ASSERT_EQUAL(3, progress.level);
}

static void test_level_progressions_empty_keeps_defaults() {
    LevelProgress progress;
    TEST_ASSERT_TRUE(parseLevelProgressionsText("{\"data\":[]}", 5, &progress));
    TEST_ASSERT_FALSE(progress.valid);
    TEST_ASSERT_EQUAL(0, progress.level);
}

static void test_level_progressions_malformed_body() {
    LevelProgress progress;
    progress.level = 7;
    progress.valid = true;
    TEST_ASSERT_FALSE(parseLevelProgressionsText("{\"data\":[{\"data\":{\"level\":3,}}]}", 3,
                                                 &progress));
    TEST_ASSERT_FALSE(progress.valid);
    TEST_ASSERT_EQUAL(0, progress.level);
}

static void test_level_progressions_truncated_body() {
    std::string body = syntheticProgressions(60);
    body.resize(body.size() / 2);
    LevelProgress progress;
    progress.level = 7;
    progress.valid = true;
    TEST_ASSERT_FALSE(parseLevelProgressionsText(body, 60, &progress));
    TEST_ASSERT_FALSE(progress.valid);
    TEST_ASSERT_EQUAL(0, progress.level);
}

static void test_level_progressions_60_levels_memory_constant() {
    // A 60-entry body is far too large for the old per-entry DOM. The sink
    // keeps one LevelProgress regardless of the entry count; parse the two
    // ends of the body to show that only the requested entry is copied out.
    const std::string body = syntheticProgressions(60);
    TEST_ASSERT_TRUE(body.size() > 4000);
    LevelProgress first;
    LevelProgress last;
    TEST_ASSERT_TRUE(parseLevelProgressionsText(body, 1, &first));
    TEST_ASSERT_TRUE(parseLevelProgressionsText(body, 60, &last));
    TEST_ASSERT_EQUAL(1, first.level);
    TEST_ASSERT_EQUAL(60, last.level);
    TEST_ASSERT_EQUAL_UINT32(iso("2026-01-02T12:00:00Z"), first.unlockedAt);
    TEST_ASSERT_EQUAL_UINT32(iso("2026-01-05T12:00:00Z"), last.unlockedAt);
}

void run_store_tests() {
    RUN_TEST(test_level_progressions_60_levels_keeps_current);
    RUN_TEST(test_level_progressions_missing_current_uses_highest);
    RUN_TEST(test_level_progressions_empty_keeps_defaults);
    RUN_TEST(test_level_progressions_malformed_body);
    RUN_TEST(test_level_progressions_truncated_body);
    RUN_TEST(test_level_progressions_60_levels_memory_constant);
}

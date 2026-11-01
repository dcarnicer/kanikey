// FIX-08 / FIX-11 / FIX-14 / FIX-20: summary size limits, the batch sampler,
// the lesson-bucket update, summary truncation reporting and the OTA
// two-strike rollback rule.
#include <unity.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "ota/Rollback.h"
#include "wk/IdSample.h"
#include "wk/Summary.h"
#include "wk/SummaryParser.h"
#include "wk/Time.h"

using namespace kk;
using namespace kk::ota;

namespace {

void appendIds(std::string* out, uint32_t first, uint32_t count) {
    char id[16];
    for (uint32_t i = 0; i < count; ++i) {
        if (i) out->push_back(',');
        snprintf(id, sizeof id, "%u", static_cast<unsigned>(first + i));
        out->append(id);
    }
}

// A realistic /summary body: one lesson bucket and one due review bucket.
// 1000 four-digit ids on each side is about 14 KB.
std::string summaryBody(uint32_t lessonIds, uint32_t reviewIds) {
    std::string body =
        "{\"object\":\"report\",\"url\":\"https://api.wanikani.com/v2/summary\","
        "\"data_updated_at\":\"2026-10-25T12:00:00.000000Z\",\"data\":{"
        "\"lessons\":[{\"available_at\":\"2026-10-25T12:00:00.000000Z\","
        "\"subject_ids\":[";
    appendIds(&body, 1000, lessonIds);
    body +=
        "]}],\"next_reviews_at\":\"2026-10-25T13:00:00.000000Z\",\"reviews\":[{"
        "\"available_at\":\"2026-10-25T12:00:00.000000Z\",\"subject_ids\":[";
    appendIds(&body, 2000, reviewIds);
    body += "]}]}}";
    return body;
}

std::vector<uint32_t> rangeIds(uint32_t count) {
    std::vector<uint32_t> ids;
    ids.reserve(count);
    for (uint32_t i = 0; i < count; ++i) ids.push_back(i);
    return ids;
}

// ---------------------------------------------------------------- FIX-08
static void test_summary_parser_keeps_1000_ids() {
    const std::string body = summaryBody(1000, 1000);
    Summary s;
    bool truncated = true;
    TEST_ASSERT_TRUE(parseSummaryText(body, &s, 1000, &truncated));
    TEST_ASSERT_FALSE(truncated);
    TEST_ASSERT_EQUAL(1, s.reviews.size());
    TEST_ASSERT_EQUAL(1000, s.reviews[0].count);
    TEST_ASSERT_EQUAL(1000, s.reviews[0].subjectIds.size());
    TEST_ASSERT_EQUAL_UINT32(1000, s.totalReviews());
}

static void test_summary_parser_respects_lower_max() {
    const std::string body = summaryBody(1000, 1000);
    Summary s;
    TEST_ASSERT_TRUE(parseSummaryText(body, &s, 25));
    TEST_ASSERT_EQUAL(25, s.reviews[0].subjectIds.size());
    // The counts still describe the whole bucket, not the kept slice.
    TEST_ASSERT_EQUAL(1000, s.reviews[0].count);
    TEST_ASSERT_EQUAL_UINT32(1000, s.totalReviews());
}

static void test_summary_due_ids_returns_more_than_20() {
    const std::string body = summaryBody(1000, 1000);
    Summary s;
    TEST_ASSERT_TRUE(parseSummaryText(body, &s));
    uint32_t now = 0;
    TEST_ASSERT_TRUE(parseIso8601("2026-10-25T12:30:00Z", &now));
    const std::vector<uint32_t> due = s.dueSubjectIds(now, 1000);
    TEST_ASSERT_EQUAL(1000, due.size());
    TEST_ASSERT_TRUE(due.size() > 20);
    TEST_ASSERT_EQUAL(30, s.dueSubjectIds(now, 30).size());
}

static void test_summary_parser_1000_ids_stays_under_32k() {
    const std::string body = summaryBody(1000, 1000);
    TEST_ASSERT_TRUE(body.size() < 32768);
    Summary s;
    TEST_ASSERT_TRUE(parseSummaryText(body, &s));
}

static void test_sample_ids_deterministic_for_seed() {
    const std::vector<uint32_t> ids = rangeIds(100);
    const std::vector<uint32_t> a = sampleIds(ids, 30, 1234);
    const std::vector<uint32_t> b = sampleIds(ids, 30, 1234);
    TEST_ASSERT_EQUAL(30, a.size());
    TEST_ASSERT_TRUE(a == b);
}

static void test_sample_ids_no_duplicates() {
    const std::vector<uint32_t> ids = rangeIds(100);
    std::vector<uint32_t> out = sampleIds(ids, 80, 7);
    TEST_ASSERT_EQUAL(80, out.size());
    std::sort(out.begin(), out.end());
    for (size_t i = 1; i < out.size(); ++i) TEST_ASSERT_TRUE(out[i - 1] < out[i]);
}

static void test_sample_ids_keeps_all_when_limit_large() {
    const std::vector<uint32_t> ids = rangeIds(100);
    std::vector<uint32_t> out = sampleIds(ids, 1000, 7);
    TEST_ASSERT_EQUAL(100, out.size());
    std::sort(out.begin(), out.end());
    for (size_t i = 0; i < out.size(); ++i) TEST_ASSERT_EQUAL_UINT32(i, out[i]);
    TEST_ASSERT_EQUAL(0, sampleIds(ids, 0, 7).size());
}

static void test_sample_ids_different_seed_differs() {
    const std::vector<uint32_t> ids = rangeIds(100);
    const std::vector<uint32_t> a = sampleIds(ids, 30, 1);
    const std::vector<uint32_t> b = sampleIds(ids, 30, 2);
    TEST_ASSERT_FALSE(a == b);
}

// ---------------------------------------------------------------- FIX-11
static void test_summary_remove_lesson_subject_updates_count() {
    Summary s;
    s.lessons = {{0, 3, {1, 2, 3}}, {0, 2, {4, 5}}};
    TEST_ASSERT_TRUE(s.removeLessonSubject(2));
    TEST_ASSERT_EQUAL(2, s.lessons[0].count);
    TEST_ASSERT_EQUAL(2, s.lessons[0].subjectIds.size());
    TEST_ASSERT_EQUAL_UINT32(1, s.lessons[0].subjectIds[0]);
    TEST_ASSERT_EQUAL_UINT32(3, s.lessons[0].subjectIds[1]);
    TEST_ASSERT_EQUAL_UINT32(4, s.totalLessons());
}

static void test_summary_remove_lesson_subject_keeps_other_buckets() {
    Summary s;
    s.lessons = {{0, 2, {1, 2}}, {0, 2, {3, 4}}};
    s.reviews = {{0, 2, {10, 11}}};
    TEST_ASSERT_TRUE(s.removeLessonSubject(1));
    TEST_ASSERT_EQUAL(1, s.lessons[0].count);
    TEST_ASSERT_EQUAL(1, s.lessons[0].subjectIds.size());
    TEST_ASSERT_EQUAL_UINT32(2, s.lessons[0].subjectIds[0]);
    TEST_ASSERT_EQUAL(2, s.lessons[1].count);
    TEST_ASSERT_EQUAL(2, s.lessons[1].subjectIds.size());
    TEST_ASSERT_EQUAL_UINT32(3, s.lessons[1].subjectIds[0]);
    TEST_ASSERT_EQUAL(2, s.reviews[0].count);
    TEST_ASSERT_EQUAL(2, s.reviews[0].subjectIds.size());
    TEST_ASSERT_EQUAL_UINT32(10, s.reviews[0].subjectIds[0]);
}

static void test_summary_remove_unknown_subject_is_noop() {
    Summary s;
    s.lessons = {{0, 2, {1, 2}}, {0, 1, {3}}};
    TEST_ASSERT_FALSE(s.removeLessonSubject(99));
    TEST_ASSERT_EQUAL(3, s.lessons[0].count + s.lessons[1].count);
    TEST_ASSERT_EQUAL(2, s.lessons[0].subjectIds.size());
    TEST_ASSERT_EQUAL(1, s.lessons[1].subjectIds.size());
}

// ---------------------------------------------------------------- FIX-14
static void test_summary_large_body_parses() {
    const std::string body = summaryBody(1000, 1000);
    Summary s;
    bool truncated = true;
    TEST_ASSERT_TRUE(parseSummaryText(body, &s, 1000, &truncated));
    TEST_ASSERT_FALSE(truncated);
    TEST_ASSERT_EQUAL(1000, s.totalLessons());
    TEST_ASSERT_EQUAL(1000, s.totalReviews());
    TEST_ASSERT_EQUAL(1000, s.lessonSubjectIds(1000).size());
    uint32_t now = 0;
    TEST_ASSERT_TRUE(parseIso8601("2026-10-25T12:30:00Z", &now));
    TEST_ASSERT_EQUAL(1000, s.dueSubjectIds(now, 1000).size());
}

static void test_summary_body_over_cap_reports_truncated() {
    const std::string body = summaryBody(1000, 1000);
    const std::string cut = body.substr(0, 4096);
    Summary s;
    bool truncated = false;
    TEST_ASSERT_FALSE(parseSummaryText(cut, &s, 1000, &truncated));
    TEST_ASSERT_TRUE(truncated);
    TEST_ASSERT_EQUAL(0, s.totalLessons());
    TEST_ASSERT_EQUAL(0, s.totalReviews());
}

// ---------------------------------------------------------------- FIX-20
static void test_ota_first_rollback_not_blacklisted() {
    const RollbackRecord stored;
    const RollbackDecision d = rollbackDecision(stored, "v0.2.0");
    TEST_ASSERT_FALSE(d.blacklist);
    TEST_ASSERT_EQUAL_UINT8(1, d.record.count);
    TEST_ASSERT_EQUAL_STRING("v0.2.0", d.record.version);
}

static void test_ota_second_rollback_blacklists() {
    RollbackRecord stored;
    snprintf(stored.version, sizeof stored.version, "%s", "v0.2.0");
    stored.count = 1;
    const RollbackDecision d = rollbackDecision(stored, "v0.2.0");
    TEST_ASSERT_TRUE(d.blacklist);
    TEST_ASSERT_EQUAL_UINT8(2, d.record.count);
    TEST_ASSERT_EQUAL_STRING("v0.2.0", d.record.version);
}

static void test_ota_other_version_resets_count() {
    RollbackRecord stored;
    snprintf(stored.version, sizeof stored.version, "%s", "v0.2.0");
    stored.count = 1;
    const RollbackDecision d = rollbackDecision(stored, "v0.3.0");
    TEST_ASSERT_FALSE(d.blacklist);
    TEST_ASSERT_EQUAL_UINT8(1, d.record.count);
    TEST_ASSERT_EQUAL_STRING("v0.3.0", d.record.version);
}

}  // namespace

void run_net_tests() {
    RUN_TEST(test_summary_parser_keeps_1000_ids);
    RUN_TEST(test_summary_parser_respects_lower_max);
    RUN_TEST(test_summary_due_ids_returns_more_than_20);
    RUN_TEST(test_summary_parser_1000_ids_stays_under_32k);
    RUN_TEST(test_sample_ids_deterministic_for_seed);
    RUN_TEST(test_sample_ids_no_duplicates);
    RUN_TEST(test_sample_ids_keeps_all_when_limit_large);
    RUN_TEST(test_sample_ids_different_seed_differs);
    RUN_TEST(test_summary_remove_lesson_subject_updates_count);
    RUN_TEST(test_summary_remove_lesson_subject_keeps_other_buckets);
    RUN_TEST(test_summary_remove_unknown_subject_is_noop);
    RUN_TEST(test_summary_large_body_parses);
    RUN_TEST(test_summary_body_over_cap_reports_truncated);
    RUN_TEST(test_ota_first_rollback_not_blacklisted);
    RUN_TEST(test_ota_second_rollback_blacklists);
    RUN_TEST(test_ota_other_version_resets_count);
}

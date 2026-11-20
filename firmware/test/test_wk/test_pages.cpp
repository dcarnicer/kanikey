// FIX-14 follow-up: cap overflow vs short read (API-04), and the adaptive page
// plan used by the lesson and review pages. Pure native tests.
#include <unity.h>

#include <algorithm>
#include <cstddef>
#include <vector>

#include "wk/HttpRetry.h"
#include "wk/PagePlan.h"

using namespace kk;

namespace {

// ---- overflow vs short read -------------------------------------------------

void test_cap_overflow_needs_cap_reached_and_more_data() {
    const size_t cap = 5000;
    TEST_ASSERT_TRUE(isCapOverflow(cap, cap, true));        // cap hit, more pending
    TEST_ASSERT_TRUE(isCapOverflow(cap + 1, cap, true));    // stored past cap (defensive)
    TEST_ASSERT_FALSE(isCapOverflow(cap, cap, false));      // exact fit, nothing pending
    TEST_ASSERT_FALSE(isCapOverflow(cap - 1, cap, true));   // short read below the cap
    TEST_ASSERT_FALSE(isCapOverflow(0, cap, false));        // empty complete body
}

void test_overflow_200_is_not_retried() {
    // The same request returns the same oversized body: no second request.
    TEST_ASSERT_FALSE(httpRetryable(200, true, false, true));
    TEST_ASSERT_FALSE(httpRetryable(200, true, true, true));
}

void test_short_read_and_parse_failure_keep_the_one_retry() {
    // Below the cap a truncated or unparsable 200 is still retried once (API-04).
    TEST_ASSERT_TRUE(httpRetryable(200, true, false, false));   // short read
    TEST_ASSERT_TRUE(httpRetryable(200, false, false, false));  // parse failure
    TEST_ASSERT_FALSE(httpRetryable(200, false, true, false));  // complete and parsed
    // Transport and server errors are unchanged.
    TEST_ASSERT_TRUE(httpRetryable(0, false, false, false));
    TEST_ASSERT_TRUE(httpRetryable(503, false, false, false));
    TEST_ASSERT_FALSE(httpRetryable(429, false, false, false));
}

// ---- page plan -------------------------------------------------------------

void test_plan_steady_state_stays_at_original() {
    PagePlan p = pagePlanStart(30);
    TEST_ASSERT_EQUAL_UINT32(30, p.next);
    for (int i = 0; i < 10; ++i) {
        pagePlanLoaded(&p, 30);
        TEST_ASSERT_EQUAL_UINT32(30, p.next);
    }
    TEST_ASSERT_EQUAL_UINT32(30, p.good);
    TEST_ASSERT_EQUAL_UINT8(0, p.hold);
}

void test_plan_original_is_at_least_one() {
    TEST_ASSERT_EQUAL_UINT32(1, pagePlanStart(0).original);
    TEST_ASSERT_EQUAL_UINT32(1, pagePlanStart(0).next);
}

void test_plan_single_big_item_recovers_by_doubling() {
    PagePlan p = pagePlanStart(30);
    pagePlanLoaded(&p, 30);
    // One big item: the 30-page overflows and the caller falls back to 10.
    pagePlanTruncated(&p, 30, 10);
    TEST_ASSERT_EQUAL_UINT32(10, p.next);
    TEST_ASSERT_EQUAL_UINT8(0, p.hold);  // a first fallback is not a failed probe
    pagePlanLoaded(&p, 10);
    TEST_ASSERT_EQUAL_UINT32(20, p.next);  // next page does not stay at 10
    pagePlanLoaded(&p, 20);
    TEST_ASSERT_EQUAL_UINT32(30, p.next);  // back at the original
    pagePlanLoaded(&p, 30);
    TEST_ASSERT_EQUAL_UINT32(30, p.next);
}

void test_plan_fallback_to_one_then_back_up() {
    PagePlan p = pagePlanStart(3);  // subject pages
    pagePlanTruncated(&p, 3, 1);
    TEST_ASSERT_EQUAL_UINT32(1, p.next);
    pagePlanLoaded(&p, 1);
    TEST_ASSERT_EQUAL_UINT32(2, p.next);
    pagePlanLoaded(&p, 2);
    TEST_ASSERT_EQUAL_UINT32(3, p.next);
}

void test_plan_short_last_page_changes_nothing() {
    PagePlan p = pagePlanStart(30);
    pagePlanTruncated(&p, 30, 10);
    pagePlanLoaded(&p, 7);  // end of the list: 7 < next (10)
    TEST_ASSERT_EQUAL_UINT32(10, p.next);
    TEST_ASSERT_EQUAL_UINT32(0, p.good);
}

void test_plan_never_below_one_or_above_original() {
    PagePlan p = pagePlanStart(5);
    pagePlanTruncated(&p, 5, 0);  // a bad ladder value is clamped up to 1
    TEST_ASSERT_EQUAL_UINT32(1, p.next);
    pagePlanTruncated(&p, 1, 0);
    TEST_ASSERT_EQUAL_UINT32(1, p.next);
    pagePlanTruncated(&p, 3, 100);  // and a larger one clamped to the original
    TEST_ASSERT_EQUAL_UINT32(5, p.next);
    for (int i = 0; i < 20; ++i) {
        pagePlanLoaded(&p, p.next);
        TEST_ASSERT_TRUE(p.next >= 1 && p.next <= 5);
    }
}

void test_plan_failed_probe_holds_at_good_size() {
    PagePlan p = pagePlanStart(10);  // level pages
    pagePlanTruncated(&p, 10, 5);
    pagePlanLoaded(&p, 5);           // good = 5, probe 10 next
    TEST_ASSERT_EQUAL_UINT32(10, p.next);
    pagePlanTruncated(&p, 10, 5);    // probe above good failed: hold
    TEST_ASSERT_EQUAL_UINT32(5, p.next);
    TEST_ASSERT_EQUAL_UINT8(kPagePlanHold, p.hold);
    for (uint8_t i = 0; i < kPagePlanHold; ++i) {
        pagePlanLoaded(&p, 5);
        TEST_ASSERT_EQUAL_UINT32(5, p.next);  // no probe during the hold
    }
    pagePlanLoaded(&p, 5);
    TEST_ASSERT_EQUAL_UINT32(10, p.next);     // probe again after the hold
}

// Simulates one list fetch. `big(i)` marks an item that overflows any page of
// more than one item (a page with it cannot fit the cap; alone it does).
struct Sim {
    size_t requests = 0;
    std::vector<size_t> sizes;   // size of each request, in order
    std::vector<size_t> begins;  // first item of each request, in order
};

template <typename Big>
Sim simulate(size_t total, size_t original, Big big) {
    Sim s;
    PagePlan p = pagePlanStart(original);
    size_t begin = 0;
    while (begin < total) {
        const size_t count = std::min(p.next, total - begin);
        TEST_ASSERT_TRUE(count >= 1 && count <= original);
        ++s.requests;
        s.sizes.push_back(count);
        s.begins.push_back(begin);
        bool overflow = false;
        if (count > 1)
            for (size_t i = begin; i < begin + count; ++i) overflow = overflow || big(i);
        if (overflow) {
            pagePlanTruncated(&p, count, std::max<size_t>(1, count / 2));
            continue;
        }
        pagePlanLoaded(&p, count);
        begin += count;
    }
    return s;
}

void test_sim_steady_state_no_extra_requests() {
    const Sim s = simulate(100, 30, [](size_t) { return false; });
    TEST_ASSERT_EQUAL_UINT32(4, s.requests);  // 30 + 30 + 30 + 10
    for (size_t i = 0; i + 1 < s.sizes.size(); ++i) TEST_ASSERT_EQUAL_UINT32(30, s.sizes[i]);
}

void test_sim_single_big_item_falls_back_for_one_run_only() {
    // Item 35 is the only big one. Pages around it fall back; the plan must
    // return to the original size afterwards instead of staying small.
    const Sim s = simulate(200, 30, [](size_t i) { return i == 35; });
    size_t firstSmall = s.sizes.size();
    for (size_t i = 0; i < s.sizes.size(); ++i)
        if (s.sizes[i] < 30) {
            firstSmall = i;
            break;
        }
    TEST_ASSERT_TRUE(firstSmall < s.sizes.size());
    bool backAtOriginal = false;
    for (size_t i = firstSmall + 1; i < s.sizes.size(); ++i)
        if (s.sizes[i] == 30) backAtOriginal = true;
    TEST_ASSERT_TRUE(backAtOriginal);
    // Bounded extra work: well under one request per item (200 items).
    TEST_ASSERT_TRUE(s.requests <= 30);
}

void test_sim_run_of_big_items_is_bounded() {
    // Every item is big: the list degrades to one item per page, and the
    // plan's probe-and-hold keeps the extra requests to about one in four.
    const size_t total = 46;
    const Sim s = simulate(total, 30, [](size_t) { return true; });
    TEST_ASSERT_TRUE(s.requests <= total + total / 3 + 6);
    size_t ones = 0;
    for (size_t size : s.sizes) ones += size == 1 ? 1 : 0;
    TEST_ASSERT_EQUAL_UINT32(total, ones);  // every item loaded, one at a time
}

void test_sim_run_of_big_items_then_clear_recovers() {
    // Big items in the first 20 positions only; the rest are small. Once the
    // run ends the plan must climb back to the original size and stay there.
    const Sim s = simulate(120, 30, [](size_t i) { return i < 20; });
    // A full-size page must start after the big run has ended (the last page
    // of the list is a short remainder, so position is what matters here).
    bool backAtOriginal = false;
    for (size_t i = 0; i < s.sizes.size(); ++i)
        if (s.begins[i] >= 20 && s.sizes[i] == 30) backAtOriginal = true;
    TEST_ASSERT_TRUE(backAtOriginal);
}

}  // namespace

void run_page_tests() {
    RUN_TEST(test_cap_overflow_needs_cap_reached_and_more_data);
    RUN_TEST(test_overflow_200_is_not_retried);
    RUN_TEST(test_short_read_and_parse_failure_keep_the_one_retry);
    RUN_TEST(test_plan_steady_state_stays_at_original);
    RUN_TEST(test_plan_original_is_at_least_one);
    RUN_TEST(test_plan_single_big_item_recovers_by_doubling);
    RUN_TEST(test_plan_fallback_to_one_then_back_up);
    RUN_TEST(test_plan_short_last_page_changes_nothing);
    RUN_TEST(test_plan_never_below_one_or_above_original);
    RUN_TEST(test_plan_failed_probe_holds_at_good_size);
    RUN_TEST(test_sim_steady_state_no_extra_requests);
    RUN_TEST(test_sim_single_big_item_falls_back_for_one_run_only);
    RUN_TEST(test_sim_run_of_big_items_is_bounded);
    RUN_TEST(test_sim_run_of_big_items_then_clear_recovers);
}

#pragma once
// Adaptive page size for a list fetched in pages (FIX-14 follow-up). One plan
// lives for one fetch, so it is the "known good" size for that session.
//
// A page that overflows the body cap falls back to a smaller size (the caller
// picks the ladder). The plan then climbs back toward the original size by
// doubling after each full page, instead of staying at the smallest size for
// the rest of the list. After a probe above the last good size fails, the plan
// holds at the good size for a few pages before probing again, so a run of big
// items costs about one extra request per few pages rather than one per page.
//
// Invariants: 1 <= next <= original, always.
// Native-tested in test/test_wk/test_pages.cpp.

#include <cstddef>
#include <cstdint>

namespace kk {

// Full pages to keep at the good size after a failed probe above it.
constexpr uint8_t kPagePlanHold = 3;

struct PagePlan {
    size_t original = 1;  // the healthy size; `next` never exceeds it
    size_t next = 1;      // size for the next page, in [1, original]
    size_t good = 0;      // last size that loaded in full; 0 until one does
    uint8_t hold = 0;     // full pages still to load at `good` before probing up
};

// Starts a plan at `original` (at least 1).
PagePlan pagePlanStart(size_t original);

// A page of `count` items loaded. Only a page of exactly `next` items informs
// the plan; a shorter final page says nothing about the size and changes nothing.
void pagePlanLoaded(PagePlan* plan, size_t count);

// A page of `attempted` items was truncated and the caller will retry with
// `smaller`. `smaller` is clamped into [1, original]. A failed probe above the
// good size starts the hold.
void pagePlanTruncated(PagePlan* plan, size_t attempted, size_t smaller);

}  // namespace kk

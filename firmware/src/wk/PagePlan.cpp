#include "wk/PagePlan.h"

#include <algorithm>

namespace kk {

PagePlan pagePlanStart(size_t original) {
    PagePlan plan;
    plan.original = original ? original : 1;
    plan.next = plan.original;
    return plan;
}

void pagePlanLoaded(PagePlan* plan, size_t count) {
    if (count == 0 || count < plan->next) return;
    plan->good = count;
    if (plan->next >= plan->original) return;  // steady state
    if (plan->hold > 0) {
        --plan->hold;  // stay at the good size for now
        return;
    }
    plan->next = std::min(plan->original, count * 2);
}

void pagePlanTruncated(PagePlan* plan, size_t attempted, size_t smaller) {
    // A probe above the good size failed: stop probing for a few pages.
    if (plan->good != 0 && attempted > plan->good) plan->hold = kPagePlanHold;
    plan->next = std::clamp<size_t>(smaller, 1, plan->original);
}

}  // namespace kk

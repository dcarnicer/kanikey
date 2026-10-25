#include "wk/Summary.h"

#include "wk/Time.h"

namespace kk {

namespace {

bool parseBuckets(const JsonValue* arr, std::vector<SummaryBucket>* out) {
    if (!arr || !arr->isArray()) return false;
    out->clear();
    out->reserve(arr->size());
    for (const JsonValue& item : arr->array) {
        if (!item.isObject()) continue;
        SummaryBucket b;
        if (const JsonValue* at = item.find("available_at"); at && at->isString())
            parseIso8601(at->string, &b.availableAt);
        if (const JsonValue* ids = item.find("subject_ids"); ids && ids->isArray()) {
            const size_t n = ids->size();
            b.count = static_cast<uint16_t>(n > 0xFFFF ? 0xFFFF : n);
        }
        out->push_back(b);
    }
    return true;
}

uint32_t sum(const std::vector<SummaryBucket>& buckets) {
    uint32_t n = 0;
    for (const SummaryBucket& b : buckets) n += b.count;
    return n;
}

}  // namespace

uint32_t Summary::totalReviews() const { return sum(reviews); }
uint32_t Summary::totalLessons() const { return sum(lessons); }

uint32_t Summary::dueReviews(uint32_t now) const {
    uint32_t n = 0;
    for (const SummaryBucket& b : reviews)
        if (b.availableAt == 0 || b.availableAt <= now) n += b.count;
    return n;
}

uint32_t Summary::nextReviewAt(uint32_t now) const {
    uint32_t best = 0;
    for (const SummaryBucket& b : reviews) {
        if (b.count == 0 || b.availableAt <= now) continue;
        if (best == 0 || b.availableAt < best) best = b.availableAt;
    }
    return best;
}

bool parseSummary(const JsonValue& root, Summary* out) {
    if (!out) return false;
    const JsonValue* data = root.find("data");
    if (!data || !data->isObject()) return false;
    out->nextReviewsAt.clear();
    if (const JsonValue* next = data->find("next_reviews_at"); next && next->isString())
        out->nextReviewsAt = next->string;
    const bool reviews = parseBuckets(data->find("reviews"), &out->reviews);
    const bool lessons = parseBuckets(data->find("lessons"), &out->lessons);
    return reviews || lessons;
}

}  // namespace kk

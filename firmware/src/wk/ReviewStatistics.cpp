#include "wk/ReviewStatistics.h"

#include <utility>

namespace kk {

namespace {
uint32_t u32Field(const JsonValue& data, const char* key) {
    const JsonValue* value = data.find(key);
    if (!value || !value->isNumber() || value->number < 0) return 0;
    return static_cast<uint32_t>(value->number);
}

uint16_t u16Field(const JsonValue& data, const char* key) {
    const uint32_t value = u32Field(data, key);
    return value > 0xFFFFu ? 0xFFFFu : static_cast<uint16_t>(value);
}
}  // namespace

std::vector<ReviewStatistic> parseReviewStatistics(const JsonValue& root) {
    std::vector<ReviewStatistic> out;
    const JsonValue* data = root.find("data");
    if (!data || !data->isArray()) return out;
    out.reserve(data->size());
    for (const JsonValue& item : data->array) {
        const JsonValue* d = item.find("data");
        if (!d || !d->isObject()) continue;
        ReviewStatistic stat;
        stat.subjectId = u32Field(*d, "subject_id");
        stat.meaningCorrect = u32Field(*d, "meaning_correct");
        stat.meaningIncorrect = u32Field(*d, "meaning_incorrect");
        stat.readingCorrect = u32Field(*d, "reading_correct");
        stat.readingIncorrect = u32Field(*d, "reading_incorrect");
        stat.meaningMaxStreak = u16Field(*d, "meaning_max_streak");
        stat.meaningCurrentStreak = u16Field(*d, "meaning_current_streak");
        stat.readingMaxStreak = u16Field(*d, "reading_max_streak");
        stat.readingCurrentStreak = u16Field(*d, "reading_current_streak");
        const uint32_t percentage = u32Field(*d, "percentage_correct");
        stat.percentageCorrect = percentage > 100 ? 100 : static_cast<uint8_t>(percentage);
        if (const JsonValue* hidden = d->find("hidden"); hidden && hidden->type == JsonType::Bool)
            stat.hidden = hidden->boolean;
        if (stat.subjectId) out.push_back(stat);
    }
    return out;
}

size_t applyReviewStatistics(const std::vector<ReviewStatistic>& statistics,
                             std::vector<Subject>* subjects) {
    if (!subjects) return 0;
    size_t applied = 0;
    for (const ReviewStatistic& stat : statistics) {
        if (stat.hidden) continue;
        for (Subject& subject : *subjects) {
            if (subject.id != stat.subjectId) continue;
            subject.meaningCorrect = stat.meaningCorrect;
            subject.meaningIncorrect = stat.meaningIncorrect;
            subject.readingCorrect = stat.readingCorrect;
            subject.readingIncorrect = stat.readingIncorrect;
            subject.meaningMaxStreak = stat.meaningMaxStreak;
            subject.meaningCurrentStreak = stat.meaningCurrentStreak;
            subject.readingMaxStreak = stat.readingMaxStreak;
            subject.readingCurrentStreak = stat.readingCurrentStreak;
            subject.percentageCorrect = stat.percentageCorrect;
            subject.hasReviewStats = true;
            ++applied;
            break;
        }
    }
    return applied;
}

}  // namespace kk

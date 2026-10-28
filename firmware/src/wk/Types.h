#pragma once
// Pure WaniKani domain types shared by the answer checker, the review queue
// and the native tests. No Arduino headers.
#include <cstdint>
#include <string>
#include <vector>

namespace kk {

enum class SubjectType : uint8_t { Radical, Kanji, Vocabulary, KanaVocabulary };

enum class ReadingType : uint8_t { Unknown, Onyomi, Kunyomi, Nanori };

struct Meaning {
    std::string text;
    bool primary = false;
    bool accepted = true;
};

struct Reading {
    std::string text;  // as returned by WaniKani; may contain '.' okurigana
    bool primary = false;
    bool accepted = true;
    ReadingType type = ReadingType::Unknown;
};

struct ContextSentence {
    std::string ja;
    std::string en;
};

struct Subject {
    uint32_t id = 0;
    SubjectType type = SubjectType::Kanji;
    uint8_t level = 0;
    std::string characters;  // empty for a radical with no Unicode character
    std::vector<Meaning> meanings;
    std::vector<Reading> readings;
    std::vector<std::string> meaningSynonyms;  // study material synonyms
    std::vector<std::string> whitelist;        // auxiliary whitelist
    std::vector<std::string> blacklist;        // auxiliary blacklist
    std::vector<uint32_t> componentSubjectIds;  // lesson components
    std::string meaningMnemonic;                // lessons (stripped markup)
    std::string readingMnemonic;
    std::string meaningHint;
    std::string readingHint;
    std::string meaningNote;  // study material note (whitespace collapsed)
    std::string readingNote;
    std::vector<std::string> partsOfSpeech;       // vocabulary details
    std::vector<ContextSentence> contextSentences;  // vocabulary details

    // Review statistics (GET /review_statistics); hasReviewStats is false for
    // lessons and for subjects without a statistics resource.
    uint32_t meaningCorrect = 0;
    uint32_t meaningIncorrect = 0;
    uint32_t readingCorrect = 0;
    uint32_t readingIncorrect = 0;
    uint16_t meaningMaxStreak = 0;
    uint16_t meaningCurrentStreak = 0;
    uint16_t readingMaxStreak = 0;
    uint16_t readingCurrentStreak = 0;
    uint8_t percentageCorrect = 0;
    bool hasReviewStats = false;

    bool needsReading() const {
        return type == SubjectType::Kanji || type == SubjectType::Vocabulary;
    }

    uint32_t reviewCount() const {
        return meaningCorrect + meaningIncorrect + readingCorrect + readingIncorrect;
    }
};

enum class AssignmentState : uint8_t { Lesson, Review, Burned };

struct Assignment {
    uint32_t id = 0;
    uint32_t subjectId = 0;
    SubjectType type = SubjectType::Kanji;
    uint8_t srsStage = 0;
    uint16_t lessonPosition = 0;  // WaniKani lesson ordering
    uint32_t availableAt = 0;   // Unix seconds; 0 = null
    uint32_t dataUpdatedAt = 0; // Unix seconds; the server's last change
    bool hidden = false;
    bool pendingReview = false;  // completed locally, waiting for the outbox
};

}  // namespace kk

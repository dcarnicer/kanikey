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

    bool needsReading() const {
        return type == SubjectType::Kanji || type == SubjectType::Vocabulary;
    }
};

enum class AssignmentState : uint8_t { Lesson, Review, Burned };

struct Assignment {
    uint32_t id = 0;
    uint32_t subjectId = 0;
    SubjectType type = SubjectType::Kanji;
    uint8_t srsStage = 0;
    uint32_t availableAt = 0;   // Unix seconds; 0 = null
    uint32_t dataUpdatedAt = 0; // Unix seconds; the server's last change
    bool hidden = false;
    bool pendingReview = false;  // completed locally, waiting for the outbox
};

}  // namespace kk

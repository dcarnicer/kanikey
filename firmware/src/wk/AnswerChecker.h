#pragma once
// WaniKani answer checking: accepted meanings, user synonyms, whitelist and
// blacklist meanings, accepted readings (primary or not), readings the item
// lists but does not accept, okurigana and the common "n before a vowel" romaji
// mistake. Pure C++, no Arduino.
#include <string>
#include <string_view>

#include "wk/Types.h"

namespace kk {

enum class AnswerKind : uint8_t { Meaning, Reading };

enum class AnswerStatus : uint8_t {
    Correct,    // counts as a correct answer
    Warning,    // valid knowledge but not the answer WaniKani wants; retry
    Incorrect,  // counts as a wrong answer
};

struct AnswerCheck {
    AnswerStatus status = AnswerStatus::Incorrect;
    std::string message;
    bool close = false;  // a typo / okurigana / valid-reading warning
};

// `strict` (default) treats a close meaning as a warning that can be retried
// without counting a mistake. With strict=false, a close meaning is accepted.
// Any reading with accepted_answer=true is correct either way.
AnswerCheck checkAnswer(const Subject& subject, AnswerKind kind, std::string_view input,
                        bool strict = true);

}  // namespace kk

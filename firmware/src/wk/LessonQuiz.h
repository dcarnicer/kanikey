#pragma once
// Optional local practice quiz for a lesson item: meaning, then reading when
// the subject has one. It never calls WaniKani; the lesson card still decides
// when to send PUT /assignments/{id}/start. Pure C++.
#include <cstdint>
#include <string_view>

#include "wk/AnswerChecker.h"
#include "wk/Types.h"

namespace kk {

class LessonQuiz {
public:
    void start(const Subject& subject);

    bool active() const { return active_; }
    bool complete() const { return complete_; }
    AnswerKind part() const { return part_; }
    bool feedback() const { return feedback_; }
    const AnswerCheck& result() const { return result_; }
    uint16_t mistakes() const { return mistakes_; }
    bool needsReading() const { return subject_.needsReading(); }
    const Subject& subject() const { return subject_; }

    // Checks an answer and enters feedback. Incorrect answers are counted but
    // do not advance; warnings also stay on the same part.
    void submit(std::string_view input, bool strict = true);

    // Leaves feedback. Correct advances to the reading or completes the quiz;
    // warning/incorrect clears feedback and retries the same part.
    void advance();

private:
    Subject subject_;
    AnswerCheck result_;
    AnswerKind part_ = AnswerKind::Meaning;
    uint16_t mistakes_ = 0;
    bool active_ = false;
    bool feedback_ = false;
    bool complete_ = false;
};

}  // namespace kk

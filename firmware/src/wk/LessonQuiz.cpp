#include "wk/LessonQuiz.h"

namespace kk {

void LessonQuiz::start(const Subject& subject, bool readingFirst) {
    subject_ = subject;
    result_ = AnswerCheck{};
    part_ = readingFirst && subject_.needsReading() ? AnswerKind::Reading : AnswerKind::Meaning;
    meaningDone_ = false;
    readingDone_ = false;
    mistakes_ = 0;
    active_ = true;
    feedback_ = false;
    complete_ = false;
}

void LessonQuiz::submit(std::string_view input, bool strict) {
    if (!active_ || feedback_ || complete_) return;
    result_ = checkAnswer(subject_, part_, input, strict);
    feedback_ = true;
    if (result_.status == AnswerStatus::Incorrect && mistakes_ < 0xFFFF) ++mistakes_;
}

void LessonQuiz::advance() {
    if (!active_ || !feedback_) return;
    feedback_ = false;
    if (result_.status != AnswerStatus::Correct) return;
    if (part_ == AnswerKind::Meaning)
        meaningDone_ = true;
    else
        readingDone_ = true;
    // The other part still has to be answered: ask it next.
    if (part_ == AnswerKind::Meaning && subject_.needsReading() && !readingDone_) {
        part_ = AnswerKind::Reading;
        return;
    }
    if (part_ == AnswerKind::Reading && !meaningDone_) {
        part_ = AnswerKind::Meaning;
        return;
    }
    active_ = false;
    complete_ = true;
}

}  // namespace kk

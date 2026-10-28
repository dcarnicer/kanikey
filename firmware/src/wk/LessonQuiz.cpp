#include "wk/LessonQuiz.h"

namespace kk {

void LessonQuiz::start(const Subject& subject) {
    subject_ = subject;
    result_ = AnswerCheck{};
    part_ = AnswerKind::Meaning;
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
    if (part_ == AnswerKind::Meaning && subject_.needsReading()) {
        part_ = AnswerKind::Reading;
        return;
    }
    active_ = false;
    complete_ = true;
}

}  // namespace kk

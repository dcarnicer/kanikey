#pragma once
// The lesson list, card, local quiz and PUT /start.
#include "app/AppState.h"

namespace kk::app {

void showLessonList();
void showLessonEmpty();
void showLesson();
void showLessonQuiz();
void lessonQuizKey(const tgc::KeyEvent& e);
void startLessons();
void startCurrentLesson();
void lessonListKey(const tgc::KeyEvent& e);
void lessonKey(const tgc::KeyEvent& e);
void startLessonQuiz();
void checkLessonQuiz();
void releaseLessonData();
void clearLessonQuizInput();

}  // namespace kk::app

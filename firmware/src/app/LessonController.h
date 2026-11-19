#pragma once
// The lesson list, card, local quiz and PUT /start.
#include "app/AppState.h"

namespace kk::app {

void showLessonList();
void showLessonEmpty();
void showLesson();
void showLessonQuiz();
void lessonQuizKey(const tgc::KeyEvent& e);
void showLessonBatchQuiz();
void lessonBatchQuizKey(const tgc::KeyEvent& e);
void checkLessonBatchQuiz();
void drawLessonStart();
void lessonStartKey(const tgc::KeyEvent& e);
void startLessons();
void startLessonBatch();
void startCurrentLesson();  // legacy notice retry: opens the confirmation
void lessonListKey(const tgc::KeyEvent& e);
void lessonKey(const tgc::KeyEvent& e);
void startLessonQuiz();
void checkLessonQuiz();
void releaseLessonData();
void clearLessonQuizInput();

}  // namespace kk::app

// Lesson list, card, local quiz and PUT /start.
#include <Arduino.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <time.h>

#include <algorithm>
#include <cstring>

#include "app/AboutScreen.h"
#include "app/App.h"
#include "app/AppState.h"
#include "app/AppUi.h"
#include "app/BootScreen.h"
#include "app/CacheController.h"
#include "app/DebugScreens.h"
#include "app/DevConsole.h"
#include "app/FirmwareScreen.h"
#include "app/HomeScreen.h"
#include "app/LessonController.h"
#include "app/LessonScreens.h"
#include "app/LoadingScreen.h"
#include "app/ModalScreens.h"
#include "app/OutboxController.h"
#include "app/OutboxScreen.h"
#include "app/PowerController.h"
#include "app/ReviewController.h"
#include "app/ReviewScreens.h"
#include "app/SettingsController.h"
#include "app/SettingsScreen.h"
#include "app/SetupController.h"
#include "app/SetupScreen.h"
#include "app/StatsController.h"
#include "app/StatsScreen.h"
#include "app/StudyController.h"
#include "app/StudyScreens.h"
#include "app/SyncController.h"
#include "app/SyncScreen.h"
#include "board/Board.h"
#include "Version.h"
#include "platform/AudioCache.h"
#include "platform/CacheStore.h"
#include "platform/CriticalStore.h"
#include "platform/DeviceSettings.h"
#include "platform/DevToken.h"
#include "platform/DevWifi.h"
#include "platform/GlyphPack.h"
#include "platform/JournalStore.h"
#include "platform/OutboxStore.h"
#include "platform/Ota.h"
#include "platform/PracticeStore.h"
#include "platform/SetupPortal.h"
#include "platform/SubjectCatalog.h"
#include "platform/WkClient.h"
#include "platform/WkLessons.h"
#include "platform/WkLevelProgress.h"
#include "platform/WkProbe.h"
#include "platform/WkReview.h"
#include "platform/WkReviewStatistics.h"
#include "platform/WkSrsCounts.h"
#include "wk/AnswerChecker.h"
#include "wk/IdSample.h"
#include "wk/LessonOrder.h"
#include "wk/LessonQuiz.h"
#include "wk/Outbox.h"
#include "wk/ReviewQueue.h"
#include "wk/ReviewResponse.h"
#include "wk/RomajiIme.h"
#include "wk/SessionStats.h"
#include "wk/Stats.h"
#include "wk/SubjectText.h"
#include "wk/Time.h"
#include "ui/Theme.h"

namespace kk::app {
namespace {

using tgc::Key;
using tgc::KeyEvent;
using namespace kk::ui;

const kk::Subject* lessonSubjectById(uint32_t id);
const kk::Subject* lessonComponentById(uint32_t id);
const kk::Subject* currentLessonSubject();
std::vector<std::string> wrapLines(lgfx::LovyanGFX& d, const std::string& text, int width);
void clearLessonQuizInputImpl();
std::string lessonQuizInput(bool meaning);
void showLessonFetchError(int httpCode, const String& error);

const kk::Subject* lessonSubjectById(uint32_t id) {
    for (const kk::Subject& s : state().lesson.gLessonBatch.subjects)
        if (s.id == id) return &s;
    return nullptr;
}

const kk::Subject* lessonComponentById(uint32_t id) {
    for (const kk::Subject& s : state().lesson.gLessonComponents)
        if (s.id == id) return &s;
    return nullptr;
}

const kk::Subject* currentLessonSubject() {
    if (!state().lesson.gLessonActive || state().lesson.gLessonIndex >= state().lesson.gLessonBatch.assignments.size()) return nullptr;
    return lessonSubjectById(state().lesson.gLessonBatch.assignments[state().lesson.gLessonIndex].subjectId);
}

std::vector<std::string> wrapLines(lgfx::LovyanGFX& d, const std::string& text, int width) {
    d.setTextFont(0);
    d.setTextSize(1);
    std::vector<std::string> lines;
    std::string cur;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == ' ') ++i;
        size_t j = i;
        while (j < text.size() && text[j] != ' ') ++j;
        if (j == i) break;
        const std::string word = text.substr(i, j - i);
        const std::string cand = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && d.textWidth(cand.c_str()) > width) {
            lines.push_back(cur);
            cur = word;
        } else {
            cur = cand;
        }
        i = j;
    }
    if (!cur.empty()) lines.push_back(cur);
    if (lines.empty()) lines.emplace_back();
    return lines;
}

void clearLessonQuizInputImpl() {
    state().lesson.gLessonQuizInput.clear();
#ifdef KANIKEY_DEV
    state().lesson.gLessonQuizDirectReading.clear();
#endif
    state().lesson.gLessonQuizIme.clear();
}

std::string lessonQuizInput(bool meaning) {
    if (meaning) return state().lesson.gLessonQuizInput;
#ifdef KANIKEY_DEV
    if (!state().lesson.gLessonQuizDirectReading.empty()) return state().lesson.gLessonQuizDirectReading;
#endif
    return state().lesson.gLessonQuizIme.text();
}

void showLessonFetchError(int httpCode, const String& error) {
    Serial.printf("[lesson] failed: %s\n", error.c_str());
    if (error == "no due lessons" || error == "no lessons") {
        state().lesson.gLessonEmptyActive = true;
        state().lesson.gLessonListActive = false;
        state().lesson.gLessonActive = false;
        state().session.gShowHome = false;
        state().display.gDirty = true;
        return;
    }
    const char* lessonError = error.c_str();
    if (httpCode == 401 || httpCode == 403) {
        setSyncIssue(httpCode, true, 0, "Lessons");
        lessonError = state().ui.gNetLine;
    } else if (kk::wkAuthBlocked()) {
        setSyncIssue(401, true, 0, "Lessons");
        lessonError = state().ui.gNetLine;
    }
    drawLoading("No lessons available", lessonError);
    state().session.gShowHome = false;
    snprintf(state().ui.gNoticeLine, sizeof state().ui.gNoticeLine, "%s", lessonError);
    state().ui.gNoticeLine2[0] = 0;
    state().ui.gNoticeAction = NoticeAction::LessonError;
    state().ui.gNoticeActive = true;
    state().display.gDirty = true;
}

}  // namespace

void clearLessonQuizInput() { clearLessonQuizInputImpl(); }

void showLessonList() {
    if (!state().display.gDisp) return;
    LessonListView view;
    view.selectedIndex = state().lesson.gLessonIndex;
    view.rows.reserve(state().lesson.gLessonBatch.assignments.size());
    for (const kk::Assignment& a : state().lesson.gLessonBatch.assignments) {
        const kk::Subject* s = lessonSubjectById(a.subjectId);
        LessonRowView row;
        row.subject = s;
        if (s) row.meaning = subjectMainMeaning(*s);
        view.rows.push_back(std::move(row));
    }

    auto& d = target();
    d.startWrite();
    drawLessonListScreen(d, state().storage.gPack, view);
    d.endWrite();
    present();
}

void showLessonEmpty() {
    if (!state().display.gDisp) return;
    LessonEmptyView view;
    view.nextReview = state().session.gNextReview;

    auto& d = target();
    d.startWrite();
    drawLessonEmptyScreen(d, view);
    d.endWrite();
    present();
}

void showLesson() {
    if (!state().display.gDisp) return;
    const kk::Subject* s = currentLessonSubject();
    if (!s) return;
    auto& d = target();

    std::string meaning;
    for (const kk::Meaning& m : s->meanings)
        if (m.accepted) {
            meaning = m.text;
            break;
        }
    if (meaning.empty() && !s->meanings.empty()) meaning = s->meanings[0].text;

    std::string reading;
    for (const kk::Reading& r : s->readings)
        if (r.accepted && r.primary) {
            reading = r.text;
            break;
        }
    if (reading.empty())
        for (const kk::Reading& r : s->readings)
            if (r.accepted) {
                reading = r.text;
                break;
            }

    std::string mnemonic = s->meaningMnemonic;
    if (mnemonic.empty()) mnemonic = s->readingMnemonic;
    if (mnemonic.empty()) mnemonic = s->meaningHint;
    if (mnemonic.empty()) mnemonic = "No mnemonic cached.";
    std::string components;
    for (uint32_t id : s->componentSubjectIds) {
        const kk::Subject* c = lessonComponentById(id);
        if (!c) continue;
        const std::string componentMeaning = subjectMainMeaning(*c);
        if (componentMeaning.empty()) continue;
        if (!components.empty()) components += " + ";
        components += componentMeaning;
    }
    std::string extra;
    if (!components.empty()) extra += "Components: " + components + ". ";
    if (!s->meaningHint.empty()) extra += "Hint: " + s->meaningHint + ". ";
    if (!s->readingHint.empty()) extra += "Reading hint: " + s->readingHint + ". ";
    if (!s->meaningNote.empty()) extra += "Meaning note: " + s->meaningNote + ". ";
    if (!s->readingNote.empty()) extra += "Reading note: " + s->readingNote + ". ";
    if (!extra.empty()) mnemonic += " " + extra;
    std::vector<std::string> lines = wrapLines(d, mnemonic, 232);
    const size_t pages = (lines.size() + 2) / 3;
    if (state().lesson.gLessonPage >= pages) state().lesson.gLessonPage = pages - 1;

    LessonCardView view;
    view.subject = s;
    view.meaning = meaning;
    view.reading = reading;
    view.lines = std::move(lines);
    view.page = state().lesson.gLessonPage;
    view.pages = pages;
    view.index = state().lesson.gLessonIndex;
    view.total = state().lesson.gLessonBatch.assignments.size();

    d.startWrite();
    drawLessonCardScreen(d, state().storage.gPack, view);
    d.endWrite();
    present();
}

void startLessonQuiz() {
    const kk::Subject* s = currentLessonSubject();
    if (!s) return;
    state().lesson.gLessonQuiz.start(*s);
    clearLessonQuizInputImpl();
    state().lesson.gLessonQuizActive = true;
    state().lesson.gLessonActive = false;
    state().display.gDirty = true;
    Serial.printf("[lesson] quiz %u\n", static_cast<unsigned>(s->id));
}

void checkLessonQuiz() {
    if (!state().lesson.gLessonQuiz.active() || state().lesson.gLessonQuiz.feedback()) return;
    const bool meaning = state().lesson.gLessonQuiz.part() == kk::AnswerKind::Meaning;
    const std::string input = lessonQuizInput(meaning);
    if (input.empty()) return;
    state().lesson.gLessonQuiz.submit(input, true);
    const char* status = state().lesson.gLessonQuiz.result().status == kk::AnswerStatus::Correct ? "correct"
                         : state().lesson.gLessonQuiz.result().status == kk::AnswerStatus::Warning ? "warning"
                                                                                   : "incorrect";
    Serial.printf("[lesson] quiz %s %s\n", meaning ? "meaning" : "reading", status);
    state().display.gDirty = true;
}

void showLessonQuiz() {
    if (!state().display.gDisp) return;
    const kk::Subject& s = state().lesson.gLessonQuiz.subject();
    const bool meaning = state().lesson.gLessonQuiz.part() == kk::AnswerKind::Meaning;
    const int total = state().lesson.gLessonQuiz.needsReading() ? 2 : 1;
    int pos = 1;
    if (state().lesson.gLessonQuiz.complete())
        pos = total;
    else if (state().lesson.gLessonQuiz.part() == kk::AnswerKind::Reading)
        pos = 2;

    LessonQuizView view;
    view.subject = &s;
    view.complete = state().lesson.gLessonQuiz.complete();
    view.feedback = state().lesson.gLessonQuiz.feedback();
    view.meaning = meaning;
    view.mistakes = state().lesson.gLessonQuiz.mistakes();
    view.position = pos;
    view.total = total;
    if (view.feedback) {
        view.status = state().lesson.gLessonQuiz.result().status;
        view.message = state().lesson.gLessonQuiz.result().message;
    }
    if (!view.complete) {
        view.typed = lessonQuizInput(meaning);
        if (!view.feedback || view.status != kk::AnswerStatus::Warning)
            view.answer = correctAnswer(s, meaning ? kk::ReviewPart::Meaning
                                                   : kk::ReviewPart::Reading);
        view.romaji = state().lesson.gLessonQuizIme.raw();
    }

    auto& d = target();
    d.startWrite();
    drawLessonQuizScreen(d, state().storage.gPack, view);
    d.endWrite();
    present();
}

void lessonQuizKey(const KeyEvent& e) {
    if (!e.down) return;
    if (e.key == Key::Esc) {
        state().lesson.gLessonQuizActive = false;
        state().lesson.gLessonActive = true;
        state().display.gDirty = true;
        return;
    }
    if (state().lesson.gLessonQuiz.complete()) {
        if (e.key == Key::Enter) {
            state().lesson.gLessonQuizActive = false;
            state().lesson.gLessonActive = true;
            state().display.gDirty = true;
        }
        return;
    }
    if (state().lesson.gLessonQuiz.feedback()) {
        if (e.key == Key::Enter) {
            const bool warning =
                state().lesson.gLessonQuiz.result().status == kk::AnswerStatus::Warning;
            state().lesson.gLessonQuiz.advance();
            // A warning retries the same part; keep the typed text so the user
            // can correct it instead of retyping.
            if (!warning) clearLessonQuizInputImpl();
            state().display.gDirty = true;
        }
        return;
    }
    if (e.key == Key::Enter) {
        checkLessonQuiz();
        return;
    }
    if (e.key == Key::Backspace) {
        if (state().lesson.gLessonQuiz.part() == kk::AnswerKind::Meaning) {
            if (!state().lesson.gLessonQuizInput.empty()) state().lesson.gLessonQuizInput.pop_back();
        }
#ifdef KANIKEY_DEV
        else if (!state().lesson.gLessonQuizDirectReading.empty()) {
            state().lesson.gLessonQuizDirectReading.pop_back();
        }
#endif
        else {
            state().lesson.gLessonQuizIme.backspace();
        }
        state().display.gDirty = true;
        return;
    }
    const char c = tgc::cardputerCharAt(e.x, e.y, state().ui.gShift);
    if (c) {
        if (state().lesson.gLessonQuiz.part() == kk::AnswerKind::Meaning)
            state().lesson.gLessonQuizInput.push_back(c);
        else {
#ifdef KANIKEY_DEV
            state().lesson.gLessonQuizDirectReading.clear();
#endif
            state().lesson.gLessonQuizIme.feed(c);
        }
        state().display.gDirty = true;
    }
}

void releaseLessonData() {
    state().lesson.gLessonBatch = kk::LessonBatch{};
    state().lesson.gLessonComponents.clear();
    state().lesson.gLessonIndex = 0;
    state().lesson.gLessonPage = 0;
}

void startLessons() {
    if (!state().session.gHaveSummary) return;
    state().lesson.gLessonQuizActive = false;
    if (state().session.gSummary.totalLessons() == 0) {
        state().lesson.gLessonEmptyActive = true;
        state().lesson.gLessonListActive = false;
        state().lesson.gLessonActive = false;
        state().session.gShowHome = false;
        state().display.gDirty = true;
        return;
    }
    if (!state().ui.gLowBatteryConfirmed && lowBatteryNow()) {
        requestLowBatteryConfirm(2);
        return;
    }
    state().ui.gLowBatteryConfirmed = false;
    // Home does not need the review batch while the user is in lessons; drop
    // its subjects before the lesson fetch so TLS keeps a healthy block.
    evictCachedBatch(true);
    state().lesson.gLessonComponents.clear();
    state().lesson.gLessonBatch = kk::LessonBatch{};
    state().lesson.gLessonEmptyActive = false;
    size_t lessonLimit = static_cast<size_t>(kk::settingsLessonBatchSize());
    if (state().session.gHaveUser && state().session.gUser.lessonsBatchSize > 0)
        lessonLimit = std::min(lessonLimit, static_cast<size_t>(state().session.gUser.lessonsBatchSize));
    if (lessonLimit == 0) lessonLimit = 1;
    // Sort a much larger candidate pool than a single batch: WaniKani orders
    // lessons by level and lesson_position, while the summary order is not
    // authoritative. Fetching the first ten only could hide a radical behind
    // ten vocabulary items and offer the wrong lesson first.
    const size_t candidateLimit = std::max<size_t>(lessonLimit, 60);
    const std::vector<uint32_t> candidateIds = state().session.gSummary.lessonSubjectIds(candidateLimit);
    char subtitle[48];
    snprintf(subtitle, sizeof subtitle, "up to %u, please wait",
             static_cast<unsigned>(candidateIds.size()));
    drawLoading("Fetching lessons...", subtitle);
    const String token = kk::DevToken::load();
    state().ui.gFreeCanvasForRequest = true;
    std::vector<kk::Assignment> chosen;
    {
        const kk::LessonAssignments assignments =
            kk::fetchLessonAssignments(token, candidateIds, candidateLimit);
        if (!assignments.ok) {
            state().ui.gFreeCanvasForRequest = false;
            showLessonFetchError(assignments.httpCode, assignments.error);
            return;
        }

        std::vector<uint32_t> subjectIds;
        subjectIds.reserve(assignments.assignments.size());
        for (const kk::Assignment& a : assignments.assignments)
            if (std::find(subjectIds.begin(), subjectIds.end(), a.subjectId) == subjectIds.end())
                subjectIds.push_back(a.subjectId);
        const kk::SubjectLevels levels = kk::fetchSubjectLevels(token, subjectIds);
        if (!levels.ok) {
            state().ui.gFreeCanvasForRequest = false;
            showLessonFetchError(levels.httpCode, levels.error);
            return;
        }

        std::vector<kk::LessonCandidate> ordered;
        ordered.reserve(assignments.assignments.size());
        for (const kk::Assignment& a : assignments.assignments) {
            kk::LessonCandidate c;
            c.assignmentId = a.id;
            c.subjectId = a.subjectId;
            c.lessonPosition = a.lessonPosition;
            c.type = a.type;
            for (const kk::SubjectLevel& level : levels.levels)
                if (level.subjectId == a.subjectId) {
                    c.level = level.level;
                    // lesson_position belongs to the subject, not the
                    // assignment; prefer it over any assignment copy.
                    c.lessonPosition = level.lessonPosition;
                    break;
                }
            ordered.push_back(c);
        }
        kk::sortLessonCandidates(&ordered);
        if (ordered.size() > lessonLimit) ordered.resize(lessonLimit);
        Serial.printf("[lesson] candidates %u, chosen %u\n",
                      static_cast<unsigned>(assignments.assignments.size()),
                      static_cast<unsigned>(ordered.size()));

        chosen.reserve(ordered.size());
        for (const kk::LessonCandidate& c : ordered) {
            for (const kk::Assignment& a : assignments.assignments)
                if (a.id == c.assignmentId) {
                    chosen.push_back(a);
                    break;
                }
        }
    }
    state().lesson.gLessonBatch = kk::fetchLessonDetails(token, std::move(chosen));
    state().ui.gFreeCanvasForRequest = false;
    if (!state().lesson.gLessonBatch.ok) {
        showLessonFetchError(state().lesson.gLessonBatch.httpCode, state().lesson.gLessonBatch.error);
        return;
    }
    state().lesson.gLessonComponents.clear();
    std::vector<uint32_t> componentIds;
    for (const kk::Assignment& a : state().lesson.gLessonBatch.assignments) {
        const kk::Subject* s = lessonSubjectById(a.subjectId);
        if (!s) continue;
        for (uint32_t id : s->componentSubjectIds) {
            if (std::find(componentIds.begin(), componentIds.end(), id) == componentIds.end())
                componentIds.push_back(id);
        }
    }
    if (!componentIds.empty()) {
        drawLoading("Loading components...", "lesson detail");
        state().ui.gFreeCanvasForRequest = true;
        state().lesson.gLessonComponents =
            kk::fetchSubjectsByIds(kk::DevToken::load(), componentIds, 10);
        state().ui.gFreeCanvasForRequest = false;
    }
    if (kk::wkAuthBlocked()) {
        // An optional materials/component request rejected the token; stop
        // before showing a lesson list that cannot be started.
        setSyncIssue(401, true, 0, "Lessons");
        drawLoading("No lessons available", state().ui.gNetLine);
        state().session.gShowHome = false;
        snprintf(state().ui.gNoticeLine, sizeof state().ui.gNoticeLine, "%s", state().ui.gNetLine);
        state().ui.gNoticeLine2[0] = 0;
        state().ui.gNoticeAction = NoticeAction::LessonError;
        state().ui.gNoticeActive = true;
        state().display.gDirty = true;
        return;
    }
    state().lesson.gLessonListActive = true;
    state().lesson.gLessonActive = false;
    state().session.gShowHome = false;
    state().lesson.gLessonIndex = 0;
    state().lesson.gLessonPage = 0;
    Serial.printf("[lesson] list %u assignments, %u subjects\n",
                  static_cast<unsigned>(state().lesson.gLessonBatch.assignments.size()),
                  static_cast<unsigned>(state().lesson.gLessonBatch.subjects.size()));
    state().display.gDirty = true;
}

void startCurrentLesson() {
    if (!state().lesson.gLessonActive || state().lesson.gLessonIndex >= state().lesson.gLessonBatch.assignments.size()) return;
    const kk::Assignment& a = state().lesson.gLessonBatch.assignments[state().lesson.gLessonIndex];
    const kk::Subject* s = lessonSubjectById(a.subjectId);
    const std::string subtitle = s ? subjectMainMeaning(*s) : std::string();
    drawLoading("Starting lesson...", subtitle.c_str());
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    int httpCode;
#ifdef KANIKEY_DEV
    if (state().lesson.gDevLessonStartCode >= 0) {
        httpCode = state().lesson.gDevLessonStartCode;
        Serial.printf("[lesson] dev start code %d\n", httpCode);
    } else
#endif
    {
        state().ui.gFreeCanvasForRequest = true;
        httpCode = kk::startAssignment(kk::DevToken::load(), a.id,
                                       now >= 1700000000u ? now : 0);
        state().ui.gFreeCanvasForRequest = false;
    }
    if ((httpCode >= 200 && httpCode < 300) || httpCode == 422) {
        // 422 means the item was already started on WaniKani; either way the
        // card is done and the summary's lesson bucket is stale.
        state().session.gSummary.removeLessonSubject(a.subjectId);
        if (state().session.gLessons > 0) --state().session.gLessons;
        saveCacheSnapshot();
        ++state().lesson.gLessonIndex;
        state().lesson.gLessonPage = 0;
        if (state().lesson.gLessonIndex >= state().lesson.gLessonBatch.assignments.size()) {
            state().lesson.gLessonActive = false;
            state().session.gShowHome = true;
            releaseLessonData();
        }
    } else if (httpCode == 401 || httpCode == 403) {
        setSyncIssue(httpCode, true, 0, "Lesson");
        drawLoading("Could not start lesson", state().ui.gNetLine);
        state().lesson.gLessonActive = false;
        state().lesson.gLessonListActive = false;
        state().session.gShowHome = true;
        releaseLessonData();
    } else {
        // Keep the card (state().lesson.gLessonActive, state().lesson.gLessonIndex) so Enter can retry.
        if (httpCode == 0)
            snprintf(state().ui.gNoticeLine, sizeof state().ui.gNoticeLine, "No connection");
        else
            snprintf(state().ui.gNoticeLine, sizeof state().ui.gNoticeLine, "HTTP %d", httpCode);
        state().ui.gNoticeLine2[0] = 0;
        state().ui.gNoticeAction = NoticeAction::LessonStart;
        state().ui.gNoticeActive = true;
        state().session.gShowHome = false;
    }
    state().display.gDirty = true;
}

void lessonListKey(const KeyEvent& e) {
    if (!e.down) return;
    const size_t total = state().lesson.gLessonBatch.assignments.size();
    if (total == 0) {
        state().lesson.gLessonListActive = false;
        state().session.gShowHome = true;
        releaseLessonData();
        state().display.gDirty = true;
        return;
    }
    if (e.key == Key::Esc) {
        state().lesson.gLessonListActive = false;
        state().session.gShowHome = true;
        releaseLessonData();
        state().display.gDirty = true;
        return;
    }
    if (e.key == Key::Up) {
        state().lesson.gLessonIndex = state().lesson.gLessonIndex == 0 ? total - 1 : state().lesson.gLessonIndex - 1;
        state().display.gDirty = true;
    } else if (e.key == Key::Down) {
        state().lesson.gLessonIndex = (state().lesson.gLessonIndex + 1) % total;
        state().display.gDirty = true;
    } else if (e.key == Key::Enter) {
        if (state().lesson.gLessonIndex >= total) state().lesson.gLessonIndex = 0;
        state().lesson.gLessonQuizActive = false;
        state().lesson.gLessonListActive = false;
        state().lesson.gLessonActive = true;
        state().lesson.gLessonPage = 0;
        state().display.gDirty = true;
    }
}

void lessonKey(const KeyEvent& e) {
    if (!e.down) return;
    if (e.key == Key::Enter) {
        startCurrentLesson();
        return;
    }
    if (e.key == Key::Other) {
        const char c = tgc::cardputerCharAt(e.x, e.y, state().ui.gShift);
        if (c == 'q' || c == 'Q') {
            startLessonQuiz();
            return;
        }
    }
    if (e.key == Key::Esc) {
        state().lesson.gLessonActive = false;
        if (!state().lesson.gLessonBatch.assignments.empty() && state().lesson.gLessonIndex < state().lesson.gLessonBatch.assignments.size())
            state().lesson.gLessonListActive = true;
        else {
            state().session.gShowHome = true;
            releaseLessonData();
        }
        state().display.gDirty = true;
        return;
    }
    if (e.key == Key::Right || (e.key == Key::Other && tgc::cardputerCharAt(e.x, e.y, false) == ' ')) {
        ++state().lesson.gLessonPage;
        state().display.gDirty = true;
    } else if (e.key == Key::Left || e.key == Key::Backspace) {
        if (state().lesson.gLessonPage > 0) --state().lesson.gLessonPage;
        state().display.gDirty = true;
    }
}

}  // namespace kk::app

// Lesson list, card browser, batch quiz and the one confirmed PUT /start run.
#include <Arduino.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>

#include "app/AppUi.h"
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <time.h>

#include <algorithm>
#include <cstring>

#include "app/AboutScreen.h"
#include "app/App.h"
#include "app/AppState.h"
#include "app/AppUi.h"
#include "app/AudioController.h"
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
#include "platform/Sound.h"
#include "platform/SubjectCatalog.h"
#include "platform/WkClient.h"
#include "platform/WkLessons.h"
#include "platform/WkLevelProgress.h"
#include "platform/WkProbe.h"
#include "platform/WkReview.h"
#include "platform/WkReviewStatistics.h"
#include "platform/WkSrsCounts.h"
#include "wk/AnswerChecker.h"
#include "wk/AnswerDisplay.h"
#include "wk/FeedbackLayout.h"
#include "wk/IdSample.h"
#include "wk/LessonOrder.h"
#include "wk/LessonPages.h"
#include "wk/LessonQuiz.h"
#include "wk/LessonSession.h"
#include "wk/Outbox.h"
#include "wk/ReviewQueue.h"
#include "wk/ReviewResponse.h"
#include "wk/RomajiIme.h"
#include "wk/SessionStats.h"
#include "wk/Stats.h"
#include "wk/SubjectText.h"
#include "wk/Text.h"
#include "wk/Time.h"
#include "wk/WorkloadGuard.h"
#include "ui/ListView.h"
#include "ui/Theme.h"

namespace kk::app {
namespace {

using tgc::Key;
using tgc::KeyEvent;
using kk::Modal;
using kk::Screen;
using namespace kk::ui;

const kk::Subject* lessonSubjectById(uint32_t id);
const kk::Subject* lessonComponentById(uint32_t id);
const kk::Assignment* currentLessonAssignment();
const kk::Subject* currentLessonSubject();
std::vector<std::string> wrapLines(lgfx::LovyanGFX& d, const std::string& text, int width);
void clearLessonQuizInputImpl();
std::string lessonQuizInput(bool meaning);
void clearLessonBatchInput();
std::string lessonBatchInput(bool meaning);
void showLessonFetchError(int httpCode, const String& error);
void afterLastLessonCard();
void startLessonBatchQuiz();
void openLessonStartConfirm();
void runLessonStart();
void journalLessonStarted(const kk::LessonStartItem& item, bool offline);
uint32_t lessonsStartedToday(uint32_t now);

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

const kk::Assignment* currentLessonAssignment() {
    const size_t index = state().lesson.gLessonBrowse.batchIndex(
        state().lesson.gLessonBrowse.card());
    if (index >= state().lesson.gLessonBatch.assignments.size()) return nullptr;
    return &state().lesson.gLessonBatch.assignments[index];
}

const kk::Subject* currentLessonSubject() {
    if (kk::screenBase(state().screen) != Screen::LessonCard) return nullptr;
    const kk::Assignment* a = currentLessonAssignment();
    return a ? lessonSubjectById(a->subjectId) : nullptr;
}

}  // namespace

const kk::Subject* lessonCardSubject() { return currentLessonSubject(); }

namespace {

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

void clearLessonBatchInput() {
    state().lesson.gLessonBatchInput.clear();
#ifdef KANIKEY_DEV
    state().lesson.gLessonBatchDirectReading.clear();
#endif
    state().lesson.gLessonBatchIme.clear();
}

std::string lessonBatchInput(bool meaning) {
    if (meaning) return state().lesson.gLessonBatchInput;
#ifdef KANIKEY_DEV
    if (!state().lesson.gLessonBatchDirectReading.empty())
        return state().lesson.gLessonBatchDirectReading;
#endif
    return state().lesson.gLessonBatchIme.text();
}

void showLessonFetchError(int httpCode, const String& error) {
    Serial.printf("[lesson] failed: %s\n", error.c_str());
    if (error == "no due lessons" || error == "no lessons") {
        state().screen.screen = Screen::LessonEmpty;
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
    snprintf(state().ui.gNoticeLine, sizeof state().ui.gNoticeLine, "%s", lessonError);
    state().ui.gNoticeLine2[0] = 0;
    state().ui.gNoticeAction = NoticeAction::LessonError;
    state().ui.gNoticeActive = true;
    state().display.gDirty = true;
}

// After the last page of the last card: the batch quiz (L3 Required) or the
// start confirmation (L3 Skip).
void afterLastLessonCard() {
    const bool quiz = lessonNextStep(kk::settingsLessonQuizRequired()) == LessonNextStep::Quiz;
    // PD-03: the quiz types an answer and the start is a PUT: neither runs on a
    // two-button board. The card stays put and says why.
    if (tgc::board::buttonsOnly()) {
        needsKeyboardNotice(quiz ? "Quizzes need a keyboard" : "Lesson start needs a keyboard");
        return;
    }
    if (quiz)
        startLessonBatchQuiz();
    else
        openLessonStartConfirm();
}

void startLessonBatchQuiz() {
    audioStop();
    std::vector<const kk::Subject*> subjects;
    subjects.reserve(state().lesson.gLessonBrowse.selectedCount());
    for (size_t pos = 0; pos < state().lesson.gLessonBrowse.selectedCount(); ++pos) {
        const size_t index = state().lesson.gLessonBrowse.batchIndex(pos);
        if (index >= state().lesson.gLessonBatch.assignments.size()) continue;
        const kk::Subject* s =
            lessonSubjectById(state().lesson.gLessonBatch.assignments[index].subjectId);
        if (s) subjects.push_back(s);
    }
    clearLessonBatchInput();
    state().lesson.gLessonBatchFeedback = false;
    state().lesson.gLessonBatchPart = kk::ReviewPart::None;
    state().lesson.gLessonBatchSubjectId = 0;
    state().lesson.gLessonBatchQueue.buildPractice(
        subjects, 0, static_cast<uint32_t>(millis()) ^ effectiveNow());
    if (state().lesson.gLessonBatchQueue.empty()) {
        openLessonStartConfirm();
        return;
    }
    state().screen.screen = Screen::LessonQuiz;
    state().lesson.quizBatch = true;
    state().lesson.startConfirm = false;
    Serial.printf("[lesson] quiz %u items\n",
                  static_cast<unsigned>(state().lesson.gLessonBatchQueue.remaining()));
    state().display.gDirty = true;
}

void openLessonStartConfirm() {
    LessonState& l = state().lesson;
    l.gLessonStartPlan.build(l.gLessonBatch.assignments, l.gLessonBrowse.selected());
    if (l.gLessonStartPlan.empty()) {
        state().screen.screen = Screen::LessonList;
        l.startConfirm = false;
        l.quizBatch = false;
        snprintf(l.gLessonNote, sizeof l.gLessonNote, "Nothing to start");
        state().display.gDirty = true;
        return;
    }
    state().screen.screen = Screen::LessonList;
    l.startConfirm = true;
    l.gLessonStartFailed = false;
    l.gLessonStartOffline = false;
    l.quizBatch = false;
    l.gLessonNote[0] = 0;
    clearLessonBatchInput();
    Serial.printf("[lesson] start confirm %u\n",
                  static_cast<unsigned>(l.gLessonStartPlan.size()));
    state().display.gDirty = true;
}

struct StartedTodayCounter {
    uint32_t dayStart = 0;
    uint32_t now = 0;
    uint32_t count = 0;
};

bool countStartedTodayVisitor(const kk::JournalRecord& record, void* user) {
    auto* counter = static_cast<StartedTodayCounter*>(user);
    if (!record.lessonStarted || record.completedAt == 0) return true;
    if (record.completedAt >= counter->dayStart && record.completedAt <= counter->now)
        ++counter->count;
    return true;
}

// L6 counts this device's lesson starts from the local journal (ARCH-03).
// `completedAt` is UTC seconds, so the window is local midnight to now.
uint32_t lessonsStartedToday(uint32_t now) {
    if (!state().storage.gFsOk || now < 1700000000u) return 0;
    if (!kk::journalOpen(currentAccountKey(), false)) return 0;
    struct tm local {};
    const time_t t = static_cast<time_t>(now);
    if (!localtime_r(&t, &local)) return 0;
    local.tm_hour = 0;
    local.tm_min = 0;
    local.tm_sec = 0;
    const time_t dayStart = mktime(&local);
    if (dayStart <= 0) return 0;
    StartedTodayCounter counter;
    counter.dayStart = static_cast<uint32_t>(dayStart);
    counter.now = now;
    kk::journalForEach(countStartedTodayVisitor, &counter);
    return counter.count;
}

// ARCH-03: one journal record per item started on this device. No SRS fields
// are set; the lessonStarted flag is what the journal and later stats read.
void journalLessonStarted(const kk::LessonStartItem& item, bool offline) {
    if (!state().storage.gFsOk || !kk::journalOpen(currentAccountKey())) return;
    kk::JournalRecord record;
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    record.completedAt = now >= 1700000000u ? now : 0;
    record.subjectId = item.subjectId;
    record.assignmentId = item.assignmentId;
    record.lessonStarted = true;
    record.offline = offline;
    kk::journalAppend(record);
}

void runLessonStart() {
    audioStop();
    LessonState& l = state().lesson;
    if (l.gLessonStartPlan.empty()) return;

    bool canStart = kk::DevWifi::connected();
#ifdef KANIKEY_DEV
    if (l.gDevLessonStartCode >= 0) canStart = true;
#endif
    if (!canStart) {
        l.gLessonStartFailed = false;
        l.gLessonStartOffline = true;
        l.startConfirm = true;
        state().display.gDirty = true;
        return;
    }

    l.gLessonStartOffline = false;
    l.gLessonStartFailed = false;
    std::vector<uint32_t> startedSubjects;
    bool stopped = false;
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    const uint32_t startedAt = now >= 1700000000u ? now : 0;

    state().ui.gFreeCanvasForRequest = true;
    while (const kk::LessonStartItem* item = l.gLessonStartPlan.nextPending()) {
        const auto& items = l.gLessonStartPlan.items();
        const size_t position = static_cast<size_t>(item - items.data());
        char subtitle[32];
        snprintf(subtitle, sizeof subtitle, "Starting %u/%u",
                 static_cast<unsigned>(l.gLessonStartPlan.startedCount() + 1),
                 static_cast<unsigned>(l.gLessonStartPlan.size()));
        drawLoading("Starting lessons...", subtitle);
        int httpCode;
#ifdef KANIKEY_DEV
        if (l.gDevLessonStartCode >= 0) {
            httpCode = l.gDevLessonStartCode;
            Serial.printf("[lesson] dev start code %d\n", httpCode);
        } else
#endif
        {
            httpCode = kk::startAssignment(kk::DevToken::load(), item->assignmentId,
                                           startedAt);
        }
        const kk::LessonStartResult result = kk::classifyStartResult(httpCode);
        if (result == kk::LessonStartResult::Started ||
            result == kk::LessonStartResult::AlreadyStarted) {
            // 422 already-started counts as a started item, so it journals the
            // same lessonStarted record; no SRS fields are written either way.
            journalLessonStarted(*item, !kk::DevWifi::connected());
            l.gLessonStartPlan.markStarted(position);
            startedSubjects.push_back(item->subjectId);
            continue;
        }
        l.gLessonStartPlan.markFailed(position);
        if (result == kk::LessonStartResult::Auth)
            setSyncIssue(httpCode, true, 0, "Lesson");
        stopped = true;
        break;
    }
    state().ui.gFreeCanvasForRequest = false;

    // The summary is what Home reads. Remove every item that actually started
    // (2xx or already-started 422) and persist once for the whole run.
    if (!startedSubjects.empty()) {
        state().session.gSummary.removeLessonSubjects(startedSubjects);
        applySummary(effectiveNow());
        saveCacheSnapshot();
    }

    if (l.gLessonStartPlan.allStarted()) {
        Serial.printf("[lesson] batch started %u\n",
                      static_cast<unsigned>(l.gLessonStartPlan.size()));
        l.startConfirm = false;
        l.gLessonStartFailed = false;
        l.gLessonStartOffline = false;
        state().screen.screen = Screen::Home;
        releaseLessonData();
    } else {
        l.startConfirm = true;
        l.gLessonStartFailed = stopped;
        l.gLessonStartOffline = false;
    }
    state().display.gDirty = true;
}

}  // namespace

void clearLessonQuizInput() { clearLessonQuizInputImpl(); }

void showLessonList() {
    if (!state().display.gDisp) return;
    LessonListView view;
    view.selectedIndex = state().lesson.gLessonBrowse.cursor();
    view.note = state().lesson.gLessonNote;
    view.rows.reserve(state().lesson.gLessonBatch.assignments.size());
    for (const kk::Assignment& a : state().lesson.gLessonBatch.assignments) {
        const kk::Subject* s = lessonSubjectById(a.subjectId);
        LessonRowView row;
        row.subject = s;
        if (s) row.meaning = subjectMainMeaning(*s);
        row.selected =
            std::find(state().lesson.gLessonBrowse.selected().begin(),
                      state().lesson.gLessonBrowse.selected().end(),
                      static_cast<size_t>(view.rows.size())) !=
            state().lesson.gLessonBrowse.selected().end();
        view.rows.push_back(std::move(row));
    }

    auto& d = target();
    d.startWrite();
    drawLessonListScreen(d, state().storage.gFontPack, state().storage.gRadicalPack, view);
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

    std::vector<const kk::Subject*> components;
    components.reserve(s->componentSubjectIds.size());
    for (uint32_t id : s->componentSubjectIds)
        if (const kk::Subject* c = lessonComponentById(id)) components.push_back(c);

    // LES-06: one topic per page; a long topic continues over more card pages
    // with the same title. The browser owns the page count so Left/Enter ask
    // the same number the card draws.
    std::vector<LessonPageView> pages;
    for (const kk::LessonPageText& topic : kk::buildLessonPages(*s, components)) {
        const std::vector<std::string> lines = wrapLines(d, topic.body, 232);
        for (size_t start = 0; start < lines.size(); start += 3) {
            LessonPageView page;
            page.title = topic.title;
            for (size_t i = start; i < lines.size() && i < start + 3; ++i)
                page.lines.push_back(lines[i]);
            pages.push_back(std::move(page));
        }
    }
    if (pages.empty()) pages.emplace_back();
    state().lesson.gLessonBrowse.setPageCount(pages.size());

    char header[24];
    state().lesson.gLessonBrowse.header(header, sizeof header);

    LessonCardView view;
    view.subject = s;
    view.meaning = meaning;
    view.reading = reading;
    // AUD-03: R appears only when the card draws a reading; kanji/radicals
    // resolve to None in the audio policy.
    view.audio = reading.empty() ? kk::AudioHint::None : audioHintFor(*s);
    // M6: the first draw of a card with a reading asks for autoplay, once.
    audioLessonCardShown(*s, !reading.empty());
    view.header = header;
    view.pages = std::move(pages);
    view.page = state().lesson.gLessonBrowse.page();
    view.index = state().lesson.gLessonBrowse.card();
    view.total = state().lesson.gLessonBrowse.selectedCount();

    d.startWrite();
    drawLessonCardScreen(d, state().storage.gFontPack, state().storage.gRadicalPack, view);
    d.endWrite();
    present();
}

void showLessonBatchQuiz() {
    if (!state().display.gDisp) return;
    const kk::SessionItem* item = state().lesson.gLessonBatchQueue.current();
    const kk::Subject* s =
        state().lesson.gLessonBatchFeedback
            ? lessonSubjectById(state().lesson.gLessonBatchSubjectId)
            : (item ? lessonSubjectById(item->subjectId) : nullptr);
    if (!s) return;
    const kk::ReviewPart part =
        state().lesson.gLessonBatchFeedback ? state().lesson.gLessonBatchPart
                                            : state().lesson.gLessonBatchQueue.part();

    const int completed = static_cast<int>(state().lesson.gLessonBatchQueue.completedCount());
    const int total =
        completed + static_cast<int>(state().lesson.gLessonBatchQueue.remaining());
    int position = completed + 1;
    if (state().lesson.gLessonBatchFeedback &&
        state().lesson.gLessonBatchQueue.remaining() == 0)
        position = completed;
    position = std::max(1, std::min(position, std::max(1, total)));

    ReviewView view;
    view.subject = s;
    view.header = "LESSON QUIZ";
    view.part = part;
    view.feedback = state().lesson.gLessonBatchFeedback;
    view.showDetailHint = false;
    view.escHint = "Esc: card";
    view.srsStage = 0;
    view.feedbackScroll = &state().lesson.gLessonBatchScroll;
    view.position = position;
    view.total = std::max(1, total);
    if (view.feedback) {
        view.typed = state().lesson.gLessonBatchInput;
        view.status = state().lesson.gLessonBatchCheck.status;
        view.message = state().lesson.gLessonBatchCheck.message;
        if (view.status != kk::AnswerStatus::Warning) {
            if (part == kk::ReviewPart::Meaning) {
                view.answers = kk::acceptedMeaningTokens(*s);
                view.synonyms = s->meaningSynonyms;
            } else {
                view.answers = kk::acceptedReadingTokens(*s);
            }
        }
    } else {
        const bool meaning = part == kk::ReviewPart::Meaning;
        view.typed = lessonBatchInput(meaning);
        view.romaji = state().lesson.gLessonBatchIme.raw();
    }

    auto& d = target();
    d.startWrite();
    drawReviewScreen(d, state().storage.gFontPack, state().storage.gRadicalPack, view);
    d.endWrite();
    present();
}

void startLessonQuiz() {
    if (tgc::board::buttonsOnly()) {
        needsKeyboardNotice("Quizzes need a keyboard");
        return;
    }
    audioStop();
    const kk::Subject* s = currentLessonSubject();
    if (!s) return;
    state().lesson.gLessonQuiz.start(*s);
    clearLessonQuizInputImpl();
    state().screen.screen = Screen::LessonQuiz;
    state().lesson.quizBatch = false;
    state().display.gDirty = true;
    Serial.printf("[lesson] quiz %u\n", static_cast<unsigned>(s->id));
}

void checkLessonQuiz() {
    if (!state().lesson.gLessonQuiz.active() || state().lesson.gLessonQuiz.feedback()) return;
    const bool meaning = state().lesson.gLessonQuiz.part() == kk::AnswerKind::Meaning;
    const std::string input = lessonQuizInput(meaning);
    if (input.empty()) return;
    // R6: the lesson quiz follows the same typo policy as reviews.
    int typos = 0;
    const bool typosStrict = kk::settingsGetInt("rv.typos", &typos) && typos == 1;
    state().lesson.gLessonQuizScroll = 0;  // a new answer starts at the top
    state().lesson.gLessonQuiz.submit(input, typosStrict);
    // REM-03: the same answer tones as reviews (a Warning is silent).
    kk::sound::soundAnswer(state().lesson.gLessonQuiz.result().status, state().sync.gSyncing,
                           state().setup.gSetupPortal.active());
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
    view.feedbackScroll = &state().lesson.gLessonQuizScroll;
    view.position = pos;
    view.total = total;
    if (view.feedback) {
        view.status = state().lesson.gLessonQuiz.result().status;
        view.message = state().lesson.gLessonQuiz.result().message;
    }
    if (!view.complete) {
        view.typed = lessonQuizInput(meaning);
        if (!view.feedback || view.status != kk::AnswerStatus::Warning) {
            if (meaning) {
                view.answers = kk::acceptedMeaningTokens(s);
                view.synonyms = s.meaningSynonyms;
            } else {
                view.answers = kk::acceptedReadingTokens(s);
            }
        }
        view.romaji = state().lesson.gLessonQuizIme.raw();
    }

    auto& d = target();
    d.startWrite();
    drawLessonQuizScreen(d, state().storage.gFontPack, state().storage.gRadicalPack, view);
    d.endWrite();
    present();
}

void lessonQuizKey(const KeyEvent& e) {
    if (!e.down) return;
    if (e.key == Key::Esc) {
        state().screen.screen = Screen::LessonCard;
        state().display.gDirty = true;
        return;
    }
    if (state().lesson.gLessonQuiz.complete()) {
        if (e.key == Key::Enter) {
            state().screen.screen = Screen::LessonCard;
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
        } else if (e.key == Key::Up || e.key == Key::Down) {
            // REV-01/03: Up/Down scroll the feedback body; there is no timer here.
            state().lesson.gLessonQuizScroll =
                kk::feedbackScrollKey(state().lesson.gLessonQuizScroll, e.key == Key::Down);
            state().display.gDirty = true;
        }
        return;
    }
    if (e.key == Key::Enter) {
        checkLessonQuiz();
        return;
    }
    if (e.key == Key::Backspace) {
        // REV-10: Fn or Ctrl clears the field, including the romaji raw line.
        if (state().ui.gFn || state().ui.gCtrl) {
            clearLessonQuizInputImpl();
        } else if (state().lesson.gLessonQuiz.part() == kk::AnswerKind::Meaning) {
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
    const char c = tgc::keyChar(e, state().ui.gShift);
    if (c) {
        if (state().lesson.gLessonQuiz.part() == kk::AnswerKind::Meaning)
            kk::appendCapped(state().lesson.gLessonQuizInput, c, kk::kMaxAnswerBytes);
        else {
#ifdef KANIKEY_DEV
            state().lesson.gLessonQuizDirectReading.clear();
#endif
            state().lesson.gLessonQuizIme.feedCapped(c, kk::kMaxAnswerBytes);
        }
        state().display.gDirty = true;
    }
}

void checkLessonBatchQuiz() {
    LessonState& l = state().lesson;
    if (l.gLessonBatchFeedback || l.gLessonBatchQueue.empty()) return;
    const kk::SessionItem* item = l.gLessonBatchQueue.current();
    if (!item) return;
    const kk::Subject* s = lessonSubjectById(item->subjectId);
    if (!s) return;
    const bool meaning = l.gLessonBatchQueue.part() == kk::ReviewPart::Meaning;
    const std::string input = lessonBatchInput(meaning);
    if (input.empty()) return;
    l.gLessonBatchSubjectId = item->subjectId;
    l.gLessonBatchPart = l.gLessonBatchQueue.part();
    l.gLessonBatchInput = input;
    // R6: the batch quiz follows the same typo policy as reviews.
    int typos = 0;
    const bool typosStrict = kk::settingsGetInt("rv.typos", &typos) && typos == 1;
    l.gLessonBatchCheck =
        kk::checkAnswer(*s, meaning ? kk::AnswerKind::Meaning : kk::AnswerKind::Reading,
                        input, typosStrict);
    l.gLessonBatchFeedback = true;
    l.gLessonBatchScroll = 0;  // a new answer starts at the top
    if (l.gLessonBatchCheck.status != kk::AnswerStatus::Warning) {
        // Lesson completions are local: no outbox record, no POST /reviews and
        // no cache write.
        l.gLessonBatchQueue.answer(l.gLessonBatchCheck.status);
    }
    Serial.printf("[lesson] quiz %s %s\n", meaning ? "meaning" : "reading",
                  l.gLessonBatchCheck.status == kk::AnswerStatus::Correct ? "correct"
                  : l.gLessonBatchCheck.status == kk::AnswerStatus::Warning ? "warning"
                                                                            : "incorrect");
    // REM-03: the same answer tones as reviews (a Warning is silent).
    kk::sound::soundAnswer(l.gLessonBatchCheck.status, state().sync.gSyncing,
                           state().setup.gSetupPortal.active());
    state().display.gDirty = true;
}

void lessonBatchQuizKey(const KeyEvent& e) {
    if (!e.down) return;
    LessonState& l = state().lesson;
    if (e.key == Key::Esc) {
        // Back to the last card, batch unchanged.
        l.quizBatch = false;
        l.gLessonBatchFeedback = false;
        l.gLessonBatchPart = kk::ReviewPart::None;
        l.gLessonBatchSubjectId = 0;
        clearLessonBatchInput();
        state().screen.screen = Screen::LessonCard;
        state().display.gDirty = true;
        return;
    }
    if (l.gLessonBatchFeedback) {
        if (e.key == Key::Enter) {
            if (l.gLessonBatchCheck.status == kk::AnswerStatus::Warning) {
                // Warning: retry the same part with the typed text kept.
                l.gLessonBatchFeedback = false;
            } else {
                l.gLessonBatchFeedback = false;
                clearLessonBatchInput();
                if (l.gLessonBatchQueue.empty()) {
                    openLessonStartConfirm();
                }
            }
            state().display.gDirty = true;
        } else if (e.key == Key::Up || e.key == Key::Down) {
            // REV-01/03: Up/Down scroll the feedback body, as on the single quiz.
            l.gLessonBatchScroll =
                kk::feedbackScrollKey(l.gLessonBatchScroll, e.key == Key::Down);
            state().display.gDirty = true;
        }
        return;
    }
    if (l.gLessonBatchQueue.empty()) {
        openLessonStartConfirm();
        return;
    }
    if (e.key == Key::Enter) {
        checkLessonBatchQuiz();
        return;
    }
    if (e.key == Key::Backspace) {
        // REV-10: Fn or Ctrl clears the field, including the romaji raw line.
        if (state().ui.gFn || state().ui.gCtrl) {
            clearLessonBatchInput();
        } else if (l.gLessonBatchQueue.part() == kk::ReviewPart::Meaning) {
            if (!l.gLessonBatchInput.empty()) l.gLessonBatchInput.pop_back();
        }
#ifdef KANIKEY_DEV
        else if (!l.gLessonBatchDirectReading.empty()) {
            l.gLessonBatchDirectReading.pop_back();
        }
#endif
        else {
            l.gLessonBatchIme.backspace();
        }
        state().display.gDirty = true;
        return;
    }
    const char c = tgc::keyChar(e, state().ui.gShift);
    if (c) {
        if (l.gLessonBatchQueue.part() == kk::ReviewPart::Meaning) {
            kk::appendCapped(l.gLessonBatchInput, c, kk::kMaxAnswerBytes);
        } else {
#ifdef KANIKEY_DEV
            l.gLessonBatchDirectReading.clear();
#endif
            l.gLessonBatchIme.feedCapped(c, kk::kMaxAnswerBytes);
        }
        state().display.gDirty = true;
    }
}

void drawLessonStart() {
    if (!state().display.gDisp) return;
    LessonState& l = state().lesson;
    if (l.gLessonWorkloadActive) {
        WorkloadView view;
        char line[48];
        if (kk::formatWorkloadReason(l.gLessonWorkloadReasons, kk::kWorkloadApprentice,
                                     l.gLessonWorkloadApprentice, l.gLessonWorkloadDue,
                                     l.gLessonWorkloadStartedToday, l.gLessonWorkloadLimits,
                                     line, sizeof line))
            view.reasons.emplace_back(line);
        if (kk::formatWorkloadReason(l.gLessonWorkloadReasons, kk::kWorkloadDueReviews,
                                     l.gLessonWorkloadApprentice, l.gLessonWorkloadDue,
                                     l.gLessonWorkloadStartedToday, l.gLessonWorkloadLimits,
                                     line, sizeof line))
            view.reasons.emplace_back(line);
        if (kk::formatWorkloadReason(l.gLessonWorkloadReasons, kk::kWorkloadStartedToday,
                                     l.gLessonWorkloadApprentice, l.gLessonWorkloadDue,
                                     l.gLessonWorkloadStartedToday, l.gLessonWorkloadLimits,
                                     line, sizeof line))
            view.reasons.emplace_back(line);
        auto& d = target();
        d.startWrite();
        drawWorkloadScreen(d, view);
        d.endWrite();
        present();
        return;
    }

    LessonStartView view;
    view.total = static_cast<unsigned>(state().lesson.gLessonStartPlan.size());
    view.started = static_cast<unsigned>(state().lesson.gLessonStartPlan.startedCount());
    view.failed = state().lesson.gLessonStartFailed;
    view.offline = state().lesson.gLessonStartOffline;
    if (view.failed) {
        for (const kk::LessonStartItem& item : state().lesson.gLessonStartPlan.items()) {
            if (item.state != kk::LessonStartItem::State::Failed) continue;
            if (item.batchIndex >= state().lesson.gLessonBatch.assignments.size()) continue;
            const kk::Assignment& a = state().lesson.gLessonBatch.assignments[item.batchIndex];
            const kk::Subject* s = lessonSubjectById(a.subjectId);
            if (s) {
                view.failedItems.push_back(subjectMainMeaning(*s));
            } else {
                char label[24];
                snprintf(label, sizeof label, "Lesson %u", static_cast<unsigned>(a.subjectId));
                view.failedItems.emplace_back(label);
            }
        }
    }

    auto& d = target();
    d.startWrite();
    drawLessonStartScreen(d, view);
    d.endWrite();
    present();
}

void lessonStartKey(const KeyEvent& e) {
    if (!e.down) return;
    LessonState& l = state().lesson;
    if (l.gLessonWorkloadActive) {
        if (e.key == Key::Enter) {
            // Continue the same entry: the guard is done, the battery prompt
            // (if any) still gets the last word before the fetch.
            l.gLessonWorkloadActive = false;
            l.gLessonWorkloadConfirmed = true;
            l.startConfirm = false;
            startLessons();
        } else if (e.key == Key::Esc) {
            l.gLessonWorkloadActive = false;
            l.gLessonWorkloadConfirmed = false;
            l.startConfirm = false;
            state().screen.screen = Screen::Home;
            releaseLessonData();
            state().display.gDirty = true;
        }
        return;
    }
    if (e.key == Key::Esc) {
        l.startConfirm = false;
        l.gLessonStartFailed = false;
        l.gLessonStartOffline = false;
        state().screen.screen = Screen::LessonCard;
        state().display.gDirty = true;
        return;
    }
    if (e.key != Key::Enter) return;
    if (state().lesson.gLessonStartFailed) state().lesson.gLessonStartPlan.resetFailed();
    state().lesson.gLessonStartFailed = false;
    state().lesson.gLessonStartOffline = false;
    startLessonBatch();
}

// The one PUT /start path. PD-03: refused on a two-button board.
void startLessonBatch() {
    if (tgc::board::buttonsOnly()) {
        needsKeyboardNotice("Lesson start needs a keyboard");
        return;
    }
    runLessonStart();
}

// Kept for the notice retry path. It never sends anything: the confirmation
// owns every PUT /assignments/{id}/start.
void startCurrentLesson() { openLessonStartConfirm(); }

void releaseLessonData() {
    LessonState& l = state().lesson;
    l.gLessonBatch = kk::LessonBatch{};
    l.gLessonComponents.clear();
    l.gLessonBrowse = kk::LessonBrowse{};
    l.gLessonBatchLimit = 0;
    l.gLessonStartPlan = kk::LessonStartPlan{};
    l.startConfirm = false;
    l.gLessonStartFailed = false;
    l.gLessonStartOffline = false;
    l.gLessonWorkloadActive = false;
    l.gLessonWorkloadConfirmed = false;
    l.gLessonWorkloadReasons = 0;
    l.gLessonBatchQueue = kk::ReviewQueue{};
    l.gLessonBatchCheck = kk::AnswerCheck{};
    l.gLessonBatchPart = kk::ReviewPart::None;
    l.gLessonBatchSubjectId = 0;
    l.gLessonBatchFeedback = false;
    l.quizBatch = false;
    clearLessonBatchInput();
    clearLessonQuizInputImpl();
    l.gLessonNote[0] = 0;
}

void startLessons() {
    if (!state().session.gHaveSummary) return;
    LessonState& l = state().lesson;
    // A fresh entry from Home re-arms the workload guard; continuing a
    // guard/battery confirmation keeps the earlier decision.
    if (kk::screenBase(state().screen) == Screen::Home) l.gLessonWorkloadConfirmed = false;
    l.gLessonWorkloadActive = false;
    l.quizBatch = false;
    l.startConfirm = false;
    if (state().session.gSummary.totalLessons() == 0) {
        state().screen.screen = Screen::LessonEmpty;
        state().display.gDirty = true;
        return;
    }
    // LES-05: local, non-blocking guard before any Wi-Fi join or request. The
    // battery prompt stays the last gate.
    if (!l.gLessonWorkloadConfirmed) {
        kk::WorkloadThresholds limits;
        int value = 0;
        if (kk::settingsGetInt("ls.appr", &value) && value > 0)
            limits.apprentice = static_cast<uint32_t>(value);
        value = 0;
        if (kk::settingsGetInt("ls.due", &value) && value > 0)
            limits.dueReviews = static_cast<uint32_t>(value);
        value = 0;
        if (kk::settingsGetInt("ls.daily", &value) && value > 0)
            limits.startedToday = static_cast<uint32_t>(value);
        const uint32_t now = effectiveNow();
        const uint32_t apprentice =
            state().session.gSrs.valid ? state().session.gSrs.apprentice : 0;
        const uint32_t due = state().session.gSummary.dueReviews(now);
        const uint32_t started = limits.startedToday ? lessonsStartedToday(now) : 0;
        const uint8_t reasons = kk::workloadWarning(apprentice, due, started, limits);
        if (reasons) {
            l.gLessonWorkloadActive = true;
            l.gLessonWorkloadReasons = reasons;
            l.gLessonWorkloadApprentice = apprentice;
            l.gLessonWorkloadDue = due;
            l.gLessonWorkloadStartedToday = started;
            l.gLessonWorkloadLimits = limits;
            state().screen.screen = Screen::LessonList;
            l.startConfirm = true;
            l.gLessonStartFailed = false;
            l.gLessonStartOffline = false;
            Serial.printf("[lesson] workload guard %u\n", static_cast<unsigned>(reasons));
            state().display.gDirty = true;
            return;
        }
    }
    if (!state().ui.gLowBatteryConfirmed && lowBatteryNow()) {
        requestLowBatteryConfirm(2);
        return;
    }
    state().ui.gLowBatteryConfirmed = false;
    // SPEED-02 can skip the automatic sync (and its Wi-Fi join) when the cache
    // is recent. Joining on the first network action keeps L usable without a
    // manual Sync now.
    if (!kk::DevWifi::connected()) {
        drawLoading("Connecting Wi-Fi...", kk::DevWifi::savedSsid().c_str());
        kk::DevWifi::connect(30000);
    }
    // Home does not need the review batch while the user is in lessons; drop
    // its subjects before the lesson fetch so TLS keeps a healthy block.
    evictCachedBatch(true);
    state().lesson.gLessonComponents.clear();
    state().lesson.gLessonBatch = kk::LessonBatch{};
    state().lesson.gLessonBrowse = kk::LessonBrowse{};
    state().lesson.gLessonBatchLimit = 0;
    state().lesson.gLessonStartPlan = kk::LessonStartPlan{};
    state().lesson.gLessonStartFailed = false;
    state().lesson.gLessonStartOffline = false;
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
        int lessonOrderValue = 0;
        kk::settingsGetInt("ls.order", &lessonOrderValue);
        kk::sortLessonCandidates(&ordered, kk::lessonOrderFromValue(lessonOrderValue));
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
    audioPrefetchBatch(state().lesson.gLessonBatch.subjects, /*drainNow=*/true);
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
        snprintf(state().ui.gNoticeLine, sizeof state().ui.gNoticeLine, "%s", state().ui.gNetLine);
        state().ui.gNoticeLine2[0] = 0;
        state().ui.gNoticeAction = NoticeAction::LessonError;
        state().ui.gNoticeActive = true;
        state().display.gDirty = true;
        return;
    }
    state().lesson.gLessonBatchLimit = lessonLimit;
    state().lesson.gLessonBrowse.begin(state().lesson.gLessonBatch.assignments.size(),
                                       lessonLimit);
    state().lesson.gLessonStartFailed = false;
    state().lesson.gLessonStartOffline = false;
    state().lesson.gLessonNote[0] = 0;
    state().screen.screen = Screen::LessonList;
    state().lesson.startConfirm = false;
    state().lesson.quizBatch = false;
    Serial.printf("[lesson] list %u assignments, %u subjects, batch %u\n",
                  static_cast<unsigned>(state().lesson.gLessonBatch.assignments.size()),
                  static_cast<unsigned>(state().lesson.gLessonBatch.subjects.size()),
                  static_cast<unsigned>(state().lesson.gLessonBrowse.selectedCount()));
    state().display.gDirty = true;
}

void lessonListKey(const KeyEvent& e) {
    if (!e.down) return;
    LessonState& l = state().lesson;
    const size_t total = l.gLessonBatch.assignments.size();
    if (total == 0) {
        state().screen.screen = Screen::Home;
        releaseLessonData();
        state().display.gDirty = true;
        return;
    }
    if (e.key == Key::Esc) {
        state().screen.screen = Screen::Home;
        releaseLessonData();
        state().display.gDirty = true;
        return;
    }
    if (e.key == Key::Up) {
        l.gLessonBrowse.setCursor(
            static_cast<size_t>(listMoveSelection(static_cast<int>(l.gLessonBrowse.cursor()),
                                                  static_cast<int>(total), -1, true)));
        l.gLessonNote[0] = 0;
        state().display.gDirty = true;
    } else if (e.key == Key::Down) {
        l.gLessonBrowse.setCursor(
            static_cast<size_t>(listMoveSelection(static_cast<int>(l.gLessonBrowse.cursor()),
                                                  static_cast<int>(total), 1, true)));
        l.gLessonNote[0] = 0;
        state().display.gDirty = true;
    } else if (e.key == Key::Other &&
               tgc::keyChar(e, state().ui.gShift) == ' ') {
        const size_t limit = l.gLessonBatchLimit ? l.gLessonBatchLimit : total;
        if (l.gLessonBrowse.toggle(l.gLessonBrowse.cursor(), limit)) {
            l.gLessonNote[0] = 0;
        } else {
            snprintf(l.gLessonNote, sizeof l.gLessonNote, "batch full");
        }
        state().display.gDirty = true;
    } else if (e.key == Key::Enter) {
        if (tgc::board::buttonsOnly()) {
            // No Space key: a hold of A opens the item under the cursor alone.
            const size_t cursor = l.gLessonBrowse.cursor();
            const size_t limit = l.gLessonBatchLimit ? l.gLessonBatchLimit : total;
            l.gLessonBrowse.begin(total, limit);
            l.gLessonBrowse.setCursor(cursor);
            l.gLessonBrowse.toggle(cursor, limit);
        }
        if (l.gLessonBrowse.selectedCount() == 0) {
            snprintf(l.gLessonNote, sizeof l.gLessonNote, "select one first");
            state().display.gDirty = true;
            return;
        }
        l.gLessonBrowse.setCard(0);
        l.gLessonBrowse.setPageCount(1);
        l.gLessonNote[0] = 0;
        state().screen.screen = Screen::LessonCard;
        l.startConfirm = false;
        l.quizBatch = false;
        state().display.gDirty = true;
    }
}

void lessonKey(const KeyEvent& e) {
    if (!e.down) return;
    LessonState& l = state().lesson;
    const char c = tgc::keyChar(e, state().ui.gShift);
    // AUD-03: R replays the card's pronunciation on a vocabulary card. Every
    // other handled key turns the page, opens the quiz or leaves, so it stops
    // the clip first.
    if (e.key == Key::Other && (c == 'r' || c == 'R')) {
        if (const kk::Subject* s = currentLessonSubject()) audioPlay(*s);
        return;
    }
    audioStop();
    if (e.key == Key::Other) {
        if (c == 'q' || c == 'Q') {
            startLessonQuiz();
            return;
        }
    }
    if (e.key == Key::Esc) {
        if (l.gLessonBrowse.selectedCount() > 0) {
            state().screen.screen = Screen::LessonList;
        } else {
            state().screen.screen = Screen::Home;
            releaseLessonData();
        }
        state().display.gDirty = true;
        return;
    }
    // PD-03: a two-button board goes back a page with hold B (Up).
    if (e.key == Key::Left || e.key == Key::Backspace ||
        (tgc::board::buttonsOnly() && e.key == Key::Up)) {
        l.gLessonBrowse.previous();
        state().display.gDirty = true;
        return;
    }
    if (e.key == Key::Right ||
        (e.key == Key::Other && tgc::keyChar(e, false) == ' ')) {
        // Space and Right turn pages only; Enter on the last page moves on.
        if (!l.gLessonBrowse.onLastPage()) l.gLessonBrowse.nextPageOrCard();
        state().display.gDirty = true;
        return;
    }
    if (e.key == Key::Enter) {
        if (!l.gLessonBrowse.onLastPage()) {
            l.gLessonBrowse.nextPageOrCard();
            state().display.gDirty = true;
            return;
        }
        if (l.gLessonBrowse.card() + 1 < l.gLessonBrowse.selectedCount()) {
            l.gLessonBrowse.nextPageOrCard();
            state().display.gDirty = true;
            return;
        }
        afterLastLessonCard();
    }
}

}  // namespace kk::app

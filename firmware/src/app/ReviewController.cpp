// Review queue, feedback, detail card, summary and stage updates.
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
#include "wk/AnswerDisplay.h"
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
#include "wk/SubmitPolicy.h"
#include "wk/Text.h"
#include "wk/Time.h"
#include "ui/Theme.h"

namespace kk::app {
namespace {

using tgc::Key;
using tgc::KeyEvent;
using namespace kk::ui;

void clearReviewInput();
// Feed one printable character into the active question field.
void feedReviewChar(char c);
// The serial test helper can inject a reading directly; release builds only
// use the romaji IME.
std::string reviewInput(bool meaning);
bool hasRelatedSubjects(const kk::Subject& s);
int detailPageKind(int index, const kk::Subject& s);
// REV-04: R7 gates the C/A feedback actions.
bool reviewWrongActionsEnabled();
// REV-07: R9 opens the mnemonic page on a wrong answer.
bool reviewDetailsOnWrongEnabled();
// SPEED-01: R12 selects when completed reviews are POSTed.
kk::SubmitPolicy reviewSubmitPolicy();
// Queue, persist and count one completed item (normal answers and C).
void applyCompletion();
// REV-11: keep the typed wrong answers and per-assignment stage pairs for the
// summary rows.
void recordWrongAnswer(uint32_t assignmentId, kk::ReviewPart part, const std::string& text);
void recordSummaryStage(uint32_t assignmentId, uint8_t from, uint8_t to);

void clearReviewInput() {
    state().review.gMeaningInput.clear();
#ifdef KANIKEY_DEV
    state().review.gDirectReading.clear();
#endif
    state().review.gIme.clear();
}

void feedReviewChar(char c) {
    if (state().review.gQueue.part() == kk::ReviewPart::Meaning) {
        kk::appendCapped(state().review.gMeaningInput, c, kk::kMaxAnswerBytes);
    } else {
#ifdef KANIKEY_DEV
        state().review.gDirectReading.clear();
#endif
        state().review.gIme.feedCapped(c, kk::kMaxAnswerBytes);
    }
    state().display.gDirty = true;
}

// R8 is stored in milliseconds; 0 is Off. Read live so a change in pause takes
// effect on the next feedback frame.
uint32_t reviewAdvanceDelayMs() {
    int value = 0;
    if (!kk::settingsGetInt("rv.advance", &value) || value <= 0) return 0;
    return static_cast<uint32_t>(value);
}

bool reviewWrongActionsEnabled() {
    int value = 0;
    return kk::settingsGetInt("rv.wrong", &value) && value != 0;
}

bool reviewDetailsOnWrongEnabled() {
    int value = 0;
    return kk::settingsGetInt("rv.details", &value) && value != 0;
}

void recordWrongAnswer(uint32_t assignmentId, kk::ReviewPart part, const std::string& text) {
    if (assignmentId == 0 || part == kk::ReviewPart::None) return;
    std::string capped = text;
    if (capped.size() > kk::kMaxAnswerBytes) capped.resize(kk::kMaxAnswerBytes);
    auto& answers = state().review.gWrongAnswers;
    for (WrongAnswer& answer : answers) {
        if (answer.assignmentId != assignmentId || answer.part != part) continue;
        answer.text = capped;
        return;
    }
    if (answers.size() >= kSummaryWrongAnswers) answers.erase(answers.begin());
    answers.push_back(WrongAnswer{assignmentId, part, capped});
}

void recordSummaryStage(uint32_t assignmentId, uint8_t from, uint8_t to) {
    if (assignmentId == 0 || from == 0 || to == 0) return;
    auto& stages = state().review.gSummaryStages;
    for (SummaryStage& stage : stages) {
        if (stage.assignmentId != assignmentId) continue;
        stage.from = from;
        stage.to = to;
        return;
    }
    if (stages.size() >= kSummaryStages) stages.erase(stages.begin());
    stages.push_back(SummaryStage{assignmentId, from, to});
}

kk::SubmitPolicy reviewSubmitPolicy() {
    int value = 0;
    if (!kk::settingsGetInt("rv.submit", &value)) return kk::SubmitPolicy::AtSessionEnd;
    return kk::submitPolicyFromValue(value);
}

// The blocking send loop still owns input: Esc stops it between items, just
// like the old sync stop path. Other keys are swallowed so they cannot leak
// into the answer field of the next prompt.
bool sendStopRequested() {
    tgc::board::update();
    bool any = false;
    bool stop = false;
    KeyEvent e;
    while (tgc::board::nextKeyEvent(&e)) {
        if (e.key == Key::None) continue;
        any = true;
        if (e.down && e.key == Key::Esc) stop = true;
    }
    if (any) state().ui.gLastInputAt = millis();
    return stop;
}

// The serial test helper can inject a reading directly; release builds only
// use the romaji IME.
std::string reviewInput(bool meaning) {
    if (meaning) return state().review.gMeaningInput;
#ifdef KANIKEY_DEV
    if (!state().review.gDirectReading.empty()) return state().review.gDirectReading;
#endif
    return state().review.gIme.text();
}

bool hasRelatedSubjects(const kk::Subject& s) {
    return !s.visuallySimilarSubjectIds.empty() || !s.amalgamationSubjectIds.empty();
}

int detailPageKind(int index, const kk::Subject& s) {
    if (index < 5) return index;
    int next = 5;
    if (s.type == kk::SubjectType::Vocabulary) {
        if (index == next) return 5;
        ++next;
    }
    if (hasRelatedSubjects(s)) return 6;
    return 4;
}

// Queue, persist and count one completed item. Both a normal answer and the
// REV-04 C shortcut go through here so the bookkeeping cannot drift.
void applyCompletion() {
    state().review.gCompletedItem = state().review.gQueue.completed().back();
    state().review.gFeedbackCompleted = true;
    if (state().study.gPracticeActive) {
        state().review.gNeedsSubmit = false;
        journalPractice(state().review.gCompletedItem);
    } else {
        // Queue and persist before the feedback is drawn: a crash, power loss
        // or an OTA install on the feedback screen keeps the review.
        state().review.gNeedsSubmit = queueReviewLocal(state().review.gCompletedItem);
        if (state().session.gDueReviews > 0) --state().session.gDueReviews;
        markCacheCompleted(state().review.gCompletedItem.subjectId,
                           state().review.gCompletedItem.assignmentId);
        if (!state().review.gNeedsSubmit) {
            // Unreachable when startReview() checked the free slots; show the
            // failure instead of silently losing the review.
            state().review.gFeedback = false;
            state().review.gReviewActive = false;
            state().review.gReviewSummary = false;
            state().session.gShowHome = false;
            snprintf(state().ui.gNoticeLine, sizeof state().ui.gNoticeLine,
                     "Review not saved; outbox full.");
            state().ui.gNoticeLine2[0] = 0;
            state().ui.gNoticeAction = NoticeAction::OutboxFull;
            state().ui.gNoticeActive = true;
        }
    }
}

}  // namespace

const kk::Subject* subjectById(uint32_t id) {
    for (const kk::Subject& s : state().cache.gBatch.subjects)
        if (s.id == id) return &s;
    for (const kk::Subject& s : state().study.gPracticeSubjects)
        if (s.id == id) return &s;
    for (const kk::Subject& s : state().study.gCriticalSubjects)
        if (s.id == id) return &s;
    return nullptr;
}

void showReview() {
    if (!state().display.gDisp) return;
    const kk::SessionItem* item = state().review.gQueue.current();
    const kk::Subject* s = state().review.gFeedback ? subjectById(state().review.gFeedbackSubjectId)
                                     : (item ? subjectById(item->subjectId) : nullptr);
    if (!s) return;
    const kk::ReviewPart part = state().review.gFeedback ? state().review.gFeedbackPart : state().review.gQueue.part();

    ReviewView view;
    view.subject = s;
    view.part = part;
    view.feedback = state().review.gFeedback;
    int headerInfo = 0;
    view.minimalHeader =
        kk::settingsGetInt("rv.header", &headerInfo) && headerInfo == 1;
    view.srsStage = state().review.gFeedbackCompleted ? state().review.gCompletedItem.srsStage : (item ? item->srsStage : 0);
    view.wrapUpCount =
        state().review.gWrapUp ? static_cast<int>(state().review.gQueue.remaining()) : 0;
    view.wrongShortcuts =
        state().review.gFeedback &&
        state().review.gCheck.status == kk::AnswerStatus::Incorrect &&
        reviewWrongActionsEnabled();
    view.total = static_cast<int>(state().review.gQueue.completedCount() + state().review.gQueue.remaining());
    view.position = static_cast<int>(state().review.gQueue.completedCount()) + 1;
    if (state().review.gFeedback && state().review.gFeedbackCompleted)
        view.position = static_cast<int>(state().review.gQueue.completedCount());
    if (view.position > view.total) view.position = view.total;
    view.sending = state().review.gSending;
    view.sent = static_cast<unsigned>(state().review.gSendSent);
    view.sendTotal = static_cast<unsigned>(state().review.gSendTotal);
    if (view.sending) view.escHint = "Esc: stop";
    if (!state().review.gFeedback) {
        view.typed = reviewInput(part == kk::ReviewPart::Meaning);
        view.romaji = state().review.gIme.raw();
    } else {
        view.typed = state().review.gFeedbackInput;
        view.status = state().review.gCheck.status;
        view.message = state().review.gCheck.message;
        // A warning must not leak the answer through the feedback frame or the
        // detail card; the user is expected to fix the typed text instead.
        if (state().review.gCheck.status != kk::AnswerStatus::Warning) {
            if (part == kk::ReviewPart::Meaning) {
                view.answers = kk::acceptedMeaningTokens(*s);
                view.synonyms = s->meaningSynonyms;
            } else {
                view.answers = kk::acceptedReadingTokens(*s);
            }
        }
    }

    auto& d = target();
    d.startWrite();
    drawReviewScreen(d, state().storage.gFontPack, state().storage.gRadicalPack, view);
    d.endWrite();
    present();
}

int detailPageCount(const kk::Subject& s) {
    return 5 + (s.type == kk::SubjectType::Vocabulary ? 1 : 0) + (hasRelatedSubjects(s) ? 1 : 0);
}

void showReviewDetail() {
    if (!state().display.gDisp) return;
    uint32_t subjectId = state().review.gDetailSubjectId ? state().review.gDetailSubjectId : state().review.gFeedbackSubjectId;
    if (!subjectId && state().review.gQueue.current()) subjectId = state().review.gQueue.current()->subjectId;
    const kk::Subject* s = subjectById(subjectId);
    if (!s) return;

    ReviewDetailView view;
    view.subject = s;
    view.page = state().review.gReviewDetailPage;
    view.pages = state().review.gReviewDetailPages;
    view.kind = detailPageKind(state().review.gReviewDetailPage, *s);
    for (uint32_t id : s->componentSubjectIds) view.components.push_back(subjectById(id));
    for (uint32_t id : s->visuallySimilarSubjectIds) view.similar.push_back(subjectById(id));
    for (uint32_t id : s->amalgamationSubjectIds) view.amalgamations.push_back(subjectById(id));

    auto& d = target();
    d.startWrite();
    drawReviewDetailScreen(d, state().storage.gFontPack, state().storage.gRadicalPack,
                           view);
    d.endWrite();
    present();
}

void reviewDetailKey(const KeyEvent& e) {
    if (!e.down) return;
    if (e.key == Key::Esc || e.key == Key::Tab) {
        state().review.gReviewDetail = false;
        if (state().review.gDetailFromCritical) {
            state().review.gDetailFromCritical = false;
            state().review.gDetailSubjectId = 0;
            state().study.gCriticalListActive = true;
        }
        state().display.gDirty = true;
        return;
    }
    if (e.key == Key::Right ||
        (e.key == Key::Other && tgc::cardputerCharAt(e.x, e.y, false) == ' ')) {
        if (state().review.gReviewDetailPage + 1 < state().review.gReviewDetailPages) ++state().review.gReviewDetailPage;
        state().display.gDirty = true;
    } else if (e.key == Key::Left || e.key == Key::Backspace) {
        if (state().review.gReviewDetailPage > 0) --state().review.gReviewDetailPage;
        state().display.gDirty = true;
    }
}

void showReviewSummary() {
    if (!state().display.gDisp) return;
    const kk::SessionStats stats = kk::summarizeSession(state().review.gQueue.completed());
    const kk::Subject* lastSubject =
        state().review.gCompletedItem.subjectId ? subjectById(state().review.gCompletedItem.subjectId) : nullptr;

    ReviewSummaryView view;
    view.reviews = static_cast<unsigned>(state().review.gQueue.completedCount());
    view.accuracy = static_cast<unsigned>(stats.accuracy);
    kk::formatSessionDuration(state().review.gReviewElapsedMs, view.duration, sizeof view.duration);
    view.stageUps = static_cast<unsigned>(state().review.gStageUps);
    view.mistakes = static_cast<unsigned>(stats.mistakes);
    view.pending = static_cast<unsigned>(kk::outboxPendingCount(state().outbox.gOutbox));
    view.attention = static_cast<unsigned>(kk::outboxAttentionCount(state().outbox.gOutbox));
    view.stageChangeValid = state().review.gStageChange.valid;
    view.stageFrom = state().review.gStageChange.from;
    view.stageTo = state().review.gStageChange.to;
    view.lastHasStats = lastSubject && lastSubject->hasReviewStats;
    view.practice = state().study.gPracticeActive;
    view.skipped = static_cast<unsigned>(state().study.gPracticeSkipped);
    view.sending = state().review.gSending;
    view.sent = static_cast<unsigned>(state().review.gSendSent);
    view.sendTotal = static_cast<unsigned>(state().review.gSendTotal);
    if (lastSubject) {
        view.lastReviewCount = static_cast<unsigned>(lastSubject->reviewCount());
        view.lastPercentage = lastSubject->percentageCorrect;
    }

    // REV-11: the mistake rows keep the completed items' assignment order and
    // attach the typed wrong answers and the per-assignment stage change.
    const std::vector<kk::SummaryMistake> misses =
        kk::summaryMistakes(state().review.gQueue.completed());
    view.items.reserve(misses.size());
    for (const kk::SummaryMistake& miss : misses) {
        const kk::Subject* subject = subjectById(miss.subjectId);
        if (!subject) continue;
        ReviewSummaryItem item;
        item.subject = subject;
        item.incorrectMeaning =
            static_cast<uint8_t>(std::min<unsigned>(miss.incorrectMeaning, 255));
        item.incorrectReading =
            static_cast<uint8_t>(std::min<unsigned>(miss.incorrectReading, 255));
        for (const SummaryStage& stage : state().review.gSummaryStages) {
            if (stage.assignmentId != miss.assignmentId) continue;
            item.stageFrom = stage.from;
            item.stageTo = stage.to;
            item.stageValid = stage.from != 0 && stage.to != 0;
        }
        for (const WrongAnswer& wrong : state().review.gWrongAnswers) {
            if (wrong.assignmentId != miss.assignmentId) continue;
            if (wrong.part == kk::ReviewPart::Meaning) item.wrongMeaning = wrong.text;
            else if (wrong.part == kk::ReviewPart::Reading) item.wrongReading = wrong.text;
        }
        view.items.push_back(item);
    }
    view.canNextBatch =
        !view.practice && kk::summaryCanNextBatch(static_cast<unsigned>(state().session.gDueReviews));
    view.focus = state().review.gSummaryFocus == 1 ? 1 : 0;
    view.selected = state().review.gSummarySelected;
    if (view.items.empty()) view.selected = 0;
    else if (view.selected < 0) view.selected = 0;
    else if (view.selected >= static_cast<int>(view.items.size()))
        view.selected = static_cast<int>(view.items.size()) - 1;

    auto& d = target();
    d.startWrite();
    drawReviewSummaryScreen(d, state().storage.gFontPack, state().storage.gRadicalPack, view);
    d.endWrite();
    present();
}

// SPEED-01: sends the records queued by this session one at a time. Called
// from the summary (At session end / the Every 5 tail) and from feedback when
// the policy is due. One keep-alive session for the batch; the canvas is
// released for the requests and restored after the loop. A failed send, an
// armed auth/rate gate or Esc stops the loop; unsent records stay Pending.
void sendPendingSession(bool onlyLatest = false) {
    auto& r = state().review;
    if (r.gSending) return;
    std::vector<uint32_t> seqs;
    if (onlyLatest) {
        // Immediately keeps today's one-POST-per-completion behaviour: only
        // the record just queued, never an older failed item.
        if (r.gStageTracker.size())
            seqs.push_back(r.gStageTracker.seqAt(r.gStageTracker.size() - 1));
    } else {
        seqs.reserve(r.gStageTracker.size());
        for (size_t i = 0; i < r.gStageTracker.size(); ++i) {
            const uint32_t seq = r.gStageTracker.seqAt(i);
            if (seq) seqs.push_back(seq);
        }
    }
    if (seqs.empty() || seqs.front() == 0) return;

    r.gSending = true;
    r.gSendTotal = static_cast<uint32_t>(seqs.size());
    r.gSendSent = 0;
    auto redraw = []() {
        if (state().review.gReviewSummary)
            showReviewSummary();
        else
            showReview();
    };
    redraw();
    {
        kk::WkSessionGuard session;
        for (uint32_t seq : seqs) {
            if (sendStopRequested()) break;
            ++r.gSendSent;
            redraw();
            if (!sendQueuedReview(seq)) break;
            // Redraw between items so stage moves and the outbox count are live.
            redraw();
            if (kk::wkAuthBlocked() || kk::wkRateLimitWaitSeconds() > 0) break;
        }
    }
    r.gSending = false;
    r.gSendSent = 0;
    r.gSendTotal = 0;
    redraw();
}

void checkCurrent() {
    const kk::SessionItem* item = state().review.gQueue.current();
    if (!item) return;
    const kk::Subject* s = subjectById(item->subjectId);
    if (!s) return;
    const bool meaning = state().review.gQueue.part() == kk::ReviewPart::Meaning;
    const std::string input = reviewInput(meaning);
    if (input.empty()) return;
    state().review.gFeedbackSubjectId = item->subjectId;
    state().review.gFeedbackPart = state().review.gQueue.part();
    state().review.gFeedbackInput = input;
    // R6: WaniKani (the default) accepts a close meaning, Strict retries it.
    int typos = 0;
    const bool typosStrict = kk::settingsGetInt("rv.typos", &typos) && typos == 1;
    state().review.gCheck = kk::checkAnswer(*s, meaning ? kk::AnswerKind::Meaning : kk::AnswerKind::Reading,
                             input, typosStrict);
    state().review.gFeedbackCorrectAt =
        state().review.gCheck.status == kk::AnswerStatus::Correct ? millis() : 0;
    state().review.gFeedback = true;
    state().review.gFeedbackCompleted = false;
    // REV-11: the summary can show the last text the user typed wrong for each
    // part. Record it before the queue moves to the other part.
    if (state().review.gCheck.status == kk::AnswerStatus::Incorrect)
        recordWrongAnswer(item->assignmentId, state().review.gQueue.part(), input);
    if (state().review.gCheck.status != kk::AnswerStatus::Warning) {
        const kk::AnswerOutcome o = state().review.gQueue.answer(state().review.gCheck.status);
        if (o.completed) applyCompletion();
    }
    // REV-07: R9 opens the mnemonic for the part that failed. Only here, so
    // returning from the card leaves the feedback and Tab starts at page 1.
    if (state().review.gFeedback &&
        state().review.gCheck.status == kk::AnswerStatus::Incorrect &&
        reviewDetailsOnWrongEnabled() &&
        subjectById(state().review.gFeedbackSubjectId)) {
        state().review.gDetailSubjectId = 0;
        state().review.gDetailFromCritical = false;
        state().review.gReviewDetailPages = detailPageCount(*s);
        state().review.gReviewDetailPage =
            state().review.gFeedbackPart == kk::ReviewPart::Meaning ? 1 : 2;
        state().review.gReviewDetail = true;
    }
    state().display.gDirty = true;
}

void advanceFeedback() {
    state().review.gFeedbackCorrectAt = 0;
    if (state().review.gCheck.status == kk::AnswerStatus::Warning) {
        // Keep the typed text (meaning string and IME raw) so Enter returns to
        // the same question ready for a correction.
        state().review.gFeedback = false;
        state().display.gDirty = true;
        return;
    }
    // SPEED-01: Immediately sends this item, Every 5 sends the batch after the
    // 5th/10th completion; the default leaves everything for the summary. Only
    // a feedback frame that completed an item may trigger a send, so A/later
    // on a wrong part never retries.
    const kk::SubmitPolicy policy = reviewSubmitPolicy();
    const uint32_t completed = static_cast<uint32_t>(state().review.gQueue.completedCount());
    if (state().review.gFeedbackCompleted && kk::submitAfterItem(policy, completed) &&
        state().review.gStageTracker.size())
        sendPendingSession(policy == kk::SubmitPolicy::Immediately);
    state().review.gReviewDetail = false;
    state().review.gFeedback = false;
    state().review.gFeedbackCompleted = false;
    clearReviewInput();
    if (state().review.gQueue.empty()) {
        state().review.gReviewElapsedMs = millis() - state().review.gReviewStartMs;
        state().review.gReviewActive = false;
        state().review.gReviewSummary = true;
        // The tail of an Every 5 session and every At session end session goes
        // out on the summary. Esc in this loop leaves the rest pending.
        if (kk::submitAtSessionEnd(policy) && state().review.gStageTracker.size())
            sendPendingSession();
    }
    state().display.gDirty = true;
}

// REV-06: the loop tick for the correct-answer timer. Never advances wrong or
// warning feedback, and stays idle while a modal screen or the blank panel
// owns the display.
bool reviewAutoAdvance(uint32_t now) {
    auto& r = state().review;
    if (!r.gReviewActive || !r.gFeedback || r.gReviewDetail || r.gFeedbackCorrectAt == 0)
        return false;
    if (state().ui.gPauseActive || state().ui.gSettingsActive || state().ui.gHelpActive ||
        state().ui.gScreenOff)
        return false;
    if (r.gCheck.status != kk::AnswerStatus::Correct) return false;
    const uint32_t delayMs = reviewAdvanceDelayMs();
    if (!kk::autoAdvanceDue(delayMs, now - r.gFeedbackCorrectAt)) return false;
    advanceFeedback();
    return true;
}

void endReview(bool send) {
    // The summary's own loop already ran; do not retry what Esc left pending
    // when Enter/Esc closes the session. Pause/End submits the session tail
    // unless the caller (SPEED-06 sleep) asks to send nothing.
    if (send && !state().review.gReviewSummary && state().review.gStageTracker.size())
        sendPendingSession();
    state().review.gNeedsSubmit = false;
    state().review.gPendingSeq = 0;
    state().review.gSending = false;
    state().review.gSendSent = 0;
    state().review.gSendTotal = 0;
    state().review.gStageTracker.clear();
    state().ui.gPauseActive = false;
    state().ui.gSettingsActive = false;
    state().ui.gAboutActive = false;
    state().ui.gSignOutConfirm = false;
    state().lesson.gLessonEmptyActive = false;
    state().lesson.gLessonListActive = false;
    state().lesson.gLessonQuizActive = false;
    state().ui.gLowBatteryPrompt = false;
    state().ui.gLowBatteryConfirmed = false;
    state().review.gReviewActive = false;
    state().review.gReviewSummary = false;
    state().review.gReviewDetail = false;
    state().review.gFeedbackCorrectAt = 0;
    state().session.gShowHome = true;
    state().review.gFeedback = false;
    state().review.gFeedbackCompleted = false;
    state().review.gQueue = kk::ReviewQueue{};
    state().review.gWrapUp = false;
    // REV-11: the summary rows and their typed wrong answers belong to this
    // session only.
    state().review.gWrongAnswers.clear();
    state().review.gSummaryStages.clear();
    state().review.gSummaryFocus = 0;
    state().review.gSummarySelected = 0;
    state().study.gPracticeActive = false;
    state().study.gPracticeMode = 0;
    state().study.gPracticeSubjects.clear();
    state().study.gPracticeSkipped = 0;
    state().study.gStudyMenuActive = false;
    state().study.gCriticalListActive = false;
    state().review.gDetailFromCritical = false;
    state().review.gDetailSubjectId = 0;
    // Home does not need the batch's subjects; the assignments stay on disk for
    // the next session.
    evictCachedBatch(true);
    state().display.gDirty = true;
}

void startReview() {
    if (state().storage.gFsFormatPrompt) return;  // wait for the storage choice
    if (!state().session.gHaveSummary) return;
    state().lesson.gLessonQuizActive = false;
    state().study.gPracticeActive = false;
    state().study.gPracticeSubjects.clear();
    state().study.gPracticeSkipped = 0;
    state().review.gDetailFromCritical = false;
    state().review.gDetailSubjectId = 0;
    state().review.gReviewDetail = false;
    state().review.gReviewDetailPage = 0;
    state().review.gStageChange = StageChange{};
    state().review.gStageTracker.clear();
    state().review.gSending = false;
    state().review.gSendSent = 0;
    state().review.gSendTotal = 0;
    state().review.gStageUps = 0;
    state().review.gReviewElapsedMs = 0;
    state().review.gFeedbackCorrectAt = 0;
    state().review.gWrapUp = false;
    if (!state().ui.gLowBatteryConfirmed && lowBatteryNow()) {
        requestLowBatteryConfirm(1);
        return;
    }
    state().ui.gLowBatteryConfirmed = false;
    // Create/open the journal before the session so the first completion does
    // not pay for a new 65 KB file between the answer and its feedback.
    if (state().storage.gFsOk) kk::journalOpen(currentAccountKey());
    const size_t reviewLimit = static_cast<size_t>(kk::settingsReviewBatchSize());
    if (outboxFreeSlots() < reviewLimit) {
        snprintf(state().ui.gNoticeLine, sizeof state().ui.gNoticeLine, "Sync to send pending reviews first.");
        state().ui.gNoticeLine2[0] = 0;
        state().ui.gNoticeAction = NoticeAction::OutboxFull;
        state().ui.gNoticeActive = true;
        state().session.gShowHome = false;
        state().display.gDirty = true;
        return;
    }
    if (state().cache.gBatch.assignments.empty() && state().cache.gBatchEvicted) loadCachedBatch();
    if (state().cache.gBatch.assignments.empty()) {
        char subtitle[48];
        const uint32_t dueNow = state().session.gSummary.dueReviews(effectiveNow());
        snprintf(subtitle, sizeof subtitle, "%u due items, please wait",
                 static_cast<unsigned>(dueNow));
        drawLoading("Fetching reviews...", subtitle);
        flushOutbox();
        if (!runBatch()) {
            if (state().cache.gBatch.httpCode == 401 || state().cache.gBatch.httpCode == 403)
                setSyncIssue(state().cache.gBatch.httpCode, true, 0, "Reviews");
            else if (kk::wkAuthBlocked())
                setSyncIssue(401, true, 0, "Reviews");
            snprintf(state().ui.gNoticeLine, sizeof state().ui.gNoticeLine, "%s", state().ui.gNetLine);
            state().ui.gNoticeLine2[0] = 0;
            state().ui.gNoticeAction = NoticeAction::ReviewError;
            state().ui.gNoticeActive = true;
            state().session.gShowHome = false;
            state().display.gDirty = true;
            return;
        }
    }
    if (kk::wkAuthBlocked()) {
        // Optional study materials/statistics/components can arm the gate after
        // the main batch succeeded; do not start a session with a rejected token.
        setSyncIssue(401, true, 0, "Reviews");
        snprintf(state().ui.gNoticeLine, sizeof state().ui.gNoticeLine, "%s", state().ui.gNetLine);
        state().ui.gNoticeLine2[0] = 0;
        state().ui.gNoticeAction = NoticeAction::ReviewError;
        state().ui.gNoticeActive = true;
        state().session.gShowHome = false;
        state().display.gDirty = true;
        return;
    }
    drawLoading("Starting session...", "");
    const uint32_t now = effectiveNow();
    std::vector<kk::Assignment> due;
    for (const kk::Assignment& a : state().cache.gBatch.assignments) {
        // Radicals with no Unicode character are valid review items: their
        // prompt comes from the radical bitmap in the visual pack.
        if (a.availableAt != 0 && a.availableAt > now) continue;
        if (outboxHasAssignment(a.id)) continue;
        if (subjectById(a.subjectId)) due.push_back(a);
    }
    state().review.gQueue.setMeaningFirst(kk::settingsMeaningFirst());
    int backToBack = 1;
    kk::settingsGetInt("rv.b2b", &backToBack);
    state().review.gQueue.setBackToBack(backToBack != 0);
    state().review.gQueue.setLowerLevelsFirst(state().session.gHaveUser &&
                               state().session.gUser.reviewsOrder == kk::ReviewOrder::LowerLevelsFirst);
    if (kk::settingsCriticalFirst()) {
        loadCritical();
        std::vector<uint32_t> ids;
        ids.reserve(state().study.gCritical.size());
        for (const kk::CriticalItem& item : state().study.gCritical) ids.push_back(item.subjectId);
        state().review.gQueue.setCriticalSubjectIds(ids);
    }
    state().review.gQueue.build(due, state().cache.gBatch.subjects, now, reviewLimit);
    Serial.printf("[review] order %s, first levels",
                  state().review.gQueue.lowerLevelsFirst() ? "lower_levels_first" : "shuffled");
    for (size_t i = 0; i < state().review.gQueue.queue().size() && i < 8; ++i)
        Serial.printf(" %u", static_cast<unsigned>(state().review.gQueue.queue()[i].level));
    Serial.println();
    if (state().review.gQueue.empty()) {
        Serial.println("[review] no reviewable items");
        // Leave the "Starting session..." frame instead of freezing on it.
        snprintf(state().ui.gNoticeLine, sizeof state().ui.gNoticeLine, "Next review  %s", state().session.gNextReview);
        state().ui.gNoticeLine2[0] = 0;
        state().ui.gNoticeAction = NoticeAction::ReviewEmpty;
        state().ui.gNoticeActive = true;
        state().session.gShowHome = false;
        state().display.gDirty = true;
        return;
    }
    state().review.gReviewActive = true;
    state().review.gReviewSummary = false;
    state().review.gReviewStartMs = millis();
    // REV-11: a new batch starts with an empty summary and no stale rows.
    // Kept until here so a failed fetch or an outbox-full notice leaves the
    // current summary (and its list) untouched.
    state().review.gWrongAnswers.clear();
    state().review.gSummaryStages.clear();
    state().review.gSummaryFocus = 0;
    state().review.gSummarySelected = 0;
    state().session.gShowHome = false;
    state().ui.gPauseActive = false;
    state().ui.gSettingsActive = false;
    state().ui.gAboutActive = false;
    state().ui.gSignOutConfirm = false;
    state().review.gFeedback = false;
    state().review.gFeedbackCompleted = false;
    state().review.gNeedsSubmit = false;
    state().review.gPendingSeq = 0;
    clearReviewInput();
    Serial.printf("[review] session with %u items\n", static_cast<unsigned>(state().review.gQueue.remaining()));
    for (size_t i = 0; i < state().review.gQueue.queue().size() && i < 5; ++i) {
        const kk::SessionItem& it = state().review.gQueue.queue()[i];
        const kk::Subject* s = subjectById(it.subjectId);
        Serial.printf("[review] queue %u %s\n", static_cast<unsigned>(it.subjectId),
                      s ? s->characters.c_str() : "?");
    }
    state().display.gDirty = true;
}

bool reviewKey(const KeyEvent& e) {
    if (!e.down) return false;
    if (state().review.gReviewDetail) {
        reviewDetailKey(e);
        return true;
    }
    if (state().review.gFeedback) {
        if (e.key == Key::Enter) {
            advanceFeedback();
        } else if (e.key == Key::Esc) {
            state().review.gFeedbackCorrectAt = 0;
            openPause();
        } else if (e.key == Key::Tab && state().review.gCheck.status != kk::AnswerStatus::Warning) {
            state().review.gFeedbackCorrectAt = 0;
            const kk::Subject* detail = subjectById(state().review.gFeedbackSubjectId);
            state().review.gReviewDetailPages = detail ? detailPageCount(*detail) : 5;
            state().review.gDetailSubjectId = 0;
            state().review.gDetailFromCritical = false;
            state().review.gReviewDetail = true;
            state().review.gReviewDetailPage = 0;
            state().display.gDirty = true;
        } else if (state().review.gCheck.status == kk::AnswerStatus::Incorrect &&
                   reviewWrongActionsEnabled()) {
            // REV-04: C accepts the failed part, A puts it back without a
            // mistake. Both undo the count the queue already recorded.
            const char c = tgc::cardputerCharAt(e.x, e.y, state().ui.gShift);
            state().review.gFeedbackCorrectAt = 0;
            if (c == 'c' || c == 'C') {
                if (state().review.gQueue.undoLastIncorrect()) {
                    state().review.gQueue.setWrongAnswerOverride('C');
                    const kk::AnswerOutcome o =
                        state().review.gQueue.answer(kk::AnswerStatus::Correct);
                    if (o.completed) applyCompletion();
                }
                advanceFeedback();
            } else if (c == 'a' || c == 'A') {
                if (state().review.gQueue.undoLastIncorrect()) {
                    state().review.gQueue.setWrongAnswerOverride('A');
                    state().review.gQueue.askAgain();
                }
                advanceFeedback();
            }
        } else if (state().review.gCheck.status == kk::AnswerStatus::Correct) {
            // REV-06: a printable key skips the delay and starts the next
            // answer; any other key cancels the timer for this frame.
            const char c = tgc::cardputerCharAt(e.x, e.y, state().ui.gShift);
            if (c) {
                advanceFeedback();
                if (state().review.gReviewActive && !state().review.gFeedback)
                    feedReviewChar(c);
            } else {
                state().review.gFeedbackCorrectAt = 0;
            }
        } else {
            state().review.gFeedbackCorrectAt = 0;
        }
        return true;
    }
    if (e.key == Key::Enter) {
        checkCurrent();
        return true;
    }
    if (e.key == Key::Esc) {
        openPause();
        return true;
    }
    if (e.key == Key::Backspace) {
        // REV-10: Fn or Ctrl clears the field, including the romaji raw line.
        if (state().ui.gFn || state().ui.gCtrl) {
            clearReviewInput();
        } else if (state().review.gQueue.part() == kk::ReviewPart::Meaning) {
            if (!state().review.gMeaningInput.empty()) state().review.gMeaningInput.pop_back();
        }
#ifdef KANIKEY_DEV
        else if (!state().review.gDirectReading.empty()) {
            state().review.gDirectReading.pop_back();
        }
#endif
        else {
            state().review.gIme.backspace();
        }
        state().display.gDirty = true;
        return true;
    }
    const char c = tgc::cardputerCharAt(e.x, e.y, state().ui.gShift);
    if (c) feedReviewChar(c);
    return true;
}

// REV-11: the summary's own keys. Down/Tab opens the mistake list, Up/Down
// move in it, Esc leaves it, Enter still goes Home and R starts another batch
// only when the server summary still reports due reviews.
void reviewSummaryKey(const KeyEvent& e) {
    if (!e.down) return;
    auto& r = state().review;
    const int count =
        static_cast<int>(kk::summaryMistakes(state().review.gQueue.completed()).size());
    if (e.key == Key::Esc) {
        if (r.gSummaryFocus == 1) {
            r.gSummaryFocus = 0;
            state().display.gDirty = true;
        } else {
            endReview();
        }
        return;
    }
    if (e.key == Key::Enter) {
        endReview();
        return;
    }
    if (e.key == Key::Down || e.key == Key::Tab) {
        if (r.gSummaryFocus == 0)
            r.gSummaryFocus = 1;
        else
            r.gSummarySelected = kk::summaryMoveSelection(r.gSummarySelected, count, 1);
        state().display.gDirty = true;
        return;
    }
    if (e.key == Key::Up) {
        if (r.gSummaryFocus == 1) {
            r.gSummarySelected = kk::summaryMoveSelection(r.gSummarySelected, count, -1);
            state().display.gDirty = true;
        }
        return;
    }
    if (e.key != Key::Other) return;
    const char c = tgc::cardputerCharAt(e.x, e.y, state().ui.gShift);
    if ((c == 'r' || c == 'R') && !state().study.gPracticeActive &&
        kk::summaryCanNextBatch(static_cast<unsigned>(state().session.gDueReviews)))
        startReview();
}

// Applies the SRS stage change parsed from a successful POST /reviews. The
// outbox calls this; the journal patch stays there because only the sender
// knows which record it queued.
void applyReviewUpdate(const kk::ReviewUpdate& update) {
    // SPEED-01: the tracker holds every session record's pre-review stage, so
    // each successful POST can report its own move. A record sent after a
    // reboot may only have the completed item's queued stage.
    uint8_t fromStage = state().review.gStageTracker.fromStage(update.assignmentId);
    if (fromStage == 0) {
        for (const kk::SessionItem& item : state().review.gQueue.completed())
            if (item.assignmentId == update.assignmentId) {
                fromStage = item.srsStage;
                break;
            }
    }
    state().review.gStageChange.valid = true;
    state().review.gStageChange.to = update.srsStage;
    state().review.gStageChange.from = fromStage;
    // REV-11: keep the pair for the summary row before the tracker forgets a
    // successfully sent assignment.
    recordSummaryStage(update.assignmentId, fromStage, update.srsStage);
    if (state().review.gStageChange.from &&
        state().review.gStageChange.to > state().review.gStageChange.from)
        ++state().review.gStageUps;
    if (state().session.gSrs.valid && state().review.gStageChange.from &&
        state().review.gStageChange.to)
        state().session.gSrs.moveStage(state().review.gStageChange.from,
                                       state().review.gStageChange.to);
}

}  // namespace kk::app

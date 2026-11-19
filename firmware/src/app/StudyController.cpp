// Extra study menu, critical-items list and practice sessions.
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
#include "ui/ListView.h"
#include "ui/Theme.h"

namespace kk::app {
namespace {

using tgc::Key;
using tgc::KeyEvent;
using kk::Screen;
using namespace kk::ui;

struct MistakeEntry {
    uint32_t subjectId = 0;
    uint32_t at = 0;
};
struct MistakeCollector {
    uint32_t cutoff = 0;
    std::vector<MistakeEntry> entries;
};

void loadPracticePool();
void mergePracticePool(const std::vector<kk::Assignment>& assignments);
bool collectMistake(const kk::JournalRecord& record, void* user);
std::vector<uint32_t> recentMistakeIds(size_t limit, size_t* total);
bool resolvePracticeSubjects(const std::vector<uint32_t>& ids, std::vector<kk::Subject>* out,
                             std::vector<uint32_t>* missing);
void refreshStudyCounts();
void openCriticalList();
void startPractice(int mode);

void loadPracticePool() {
    if (state().study.gPracticePoolLoaded) return;
    state().study.gPracticePoolLoaded = true;
    state().study.gPracticePool.clear();
    if (state().storage.gFsOk) kk::practicePoolLoad(currentAccountKey(), &state().study.gPracticePool);
}

void mergePracticePool(const std::vector<kk::Assignment>& assignments) {
    for (const kk::Assignment& a : assignments) {
        bool found = false;
        for (kk::Assignment& existing : state().study.gPracticePool)
            if (existing.subjectId == a.subjectId) {
                const uint8_t keepLevel = existing.level;
                existing = a;
                if (existing.level == 0) existing.level = keepLevel;
                found = true;
                break;
            }
        if (!found && state().study.gPracticePool.size() < kk::kPracticePoolMax)
            state().study.gPracticePool.push_back(a);
    }
}

bool collectMistake(const kk::JournalRecord& record, void* user) {
    MistakeCollector* collector = static_cast<MistakeCollector*>(user);
    if (!collector || record.practice || record.completedAt < collector->cutoff) return true;
    if (record.incorrectMeaning + record.incorrectReading == 0) return true;
    collector->entries.push_back({record.subjectId, record.completedAt});
    return true;
}

std::vector<uint32_t> recentMistakeIds(size_t limit, size_t* total) {
    std::vector<uint32_t> ids;
    if (total) *total = 0;
    if (!state().storage.gFsOk || !kk::journalOpen(currentAccountKey(), false)) return ids;
    const uint32_t now = effectiveNow();
    const uint32_t window = static_cast<uint32_t>(kk::settingsPracticeMistakesWindow());
    if (now < window) return ids;
    MistakeCollector collector;
    collector.cutoff = now - window;
    kk::journalForEach(collectMistake, &collector);
    std::sort(collector.entries.begin(), collector.entries.end(),
              [](const MistakeEntry& a, const MistakeEntry& b) { return a.at > b.at; });
    for (const MistakeEntry& entry : collector.entries) {
        if (std::find(ids.begin(), ids.end(), entry.subjectId) != ids.end()) continue;
        ids.push_back(entry.subjectId);
        if (total) ++*total;
        if (limit && ids.size() >= limit) break;
    }
    return ids;
}

bool resolvePracticeSubjects(const std::vector<uint32_t>& ids, std::vector<kk::Subject>* out,
                             std::vector<uint32_t>* missing) {
    out->clear();
    missing->clear();
    const bool catalogOk = state().storage.gSdOk && kk::catalogOpen(SD, currentAccountKey());
    for (uint32_t id : ids) {
        kk::Subject subject;
        if (catalogOk && kk::catalogLoad(SD, id, &subject))
            out->push_back(std::move(subject));
        else
            missing->push_back(id);
    }
    if (!missing->empty() && kk::DevWifi::connected() && kk::DevToken::has()) {
        const std::vector<kk::Subject> fetched =
            kk::fetchSubjectsByIds(kk::DevToken::load(), *missing, 5);
        for (const kk::Subject& subject : fetched) {
            out->push_back(subject);
            if (catalogOk) kk::catalogStore(SD, subject.id, subject);
        }
        for (const kk::Subject& subject : fetched)
            missing->erase(std::remove(missing->begin(), missing->end(), subject.id),
                           missing->end());
    }
    return !out->empty();
}

void refreshStudyCounts() {
    size_t mistakes = 0;
    recentMistakeIds(999, &mistakes);
    snprintf(state().study.gStudyCounts[0], sizeof state().study.gStudyCounts[0], "%u", static_cast<unsigned>(mistakes));
    loadPracticePool();
    const size_t lessons =
        kk::selectRecentLessons(state().study.gPracticePool, kk::settingsPracticeLessonsMode(),
                                effectiveNow(), 999)
            .size();
    const size_t level =
        kk::selectCurrentLevel(state().study.gPracticePool, state().session.gHaveUser ? state().session.gUser.level : 0, 999).size();
    snprintf(state().study.gStudyCounts[1], sizeof state().study.gStudyCounts[1], "%u", static_cast<unsigned>(lessons));
    loadCritical();
    snprintf(state().study.gStudyCounts[2], sizeof state().study.gStudyCounts[2], "%u",
             static_cast<unsigned>(state().study.gCritical.size()));
    snprintf(state().study.gStudyCounts[3], sizeof state().study.gStudyCounts[3], "%u", static_cast<unsigned>(level));
}

void openCriticalList() {
    loadCritical();
    state().study.gCriticalSubjects.clear();
    state().study.gCriticalRows.clear();
    std::vector<uint32_t> ids;
    ids.reserve(state().study.gCritical.size());
    for (const kk::CriticalItem& item : state().study.gCritical) ids.push_back(item.subjectId);
    std::vector<uint32_t> missing;
    if (!ids.empty()) {
        drawLoading("Preparing critical items...", "");
        state().ui.gFreeCanvasForRequest = true;
        resolvePracticeSubjects(ids, &state().study.gCriticalSubjects, &missing);
        state().ui.gFreeCanvasForRequest = false;
    }
    for (const kk::CriticalItem& item : state().study.gCritical) {
        const kk::Subject* subject = nullptr;
        for (const kk::Subject& candidate : state().study.gCriticalSubjects)
            if (candidate.id == item.subjectId) {
                subject = &candidate;
                break;
            }
        if (!subject) continue;
        kk::app::CriticalRowView row;
        row.subject = subject;
        row.meaning = subjectMainMeaning(*subject);
        row.percentage = item.percentageCorrect;
        state().study.gCriticalRows.push_back(std::move(row));
    }
    state().study.gCriticalIndex = 0;
    state().study.gCriticalListActive = true;
    state().study.gStudyMenuActive = false;
    state().display.gDirty = true;
}

void startPractice(int mode) {
    static const char* kNames[4] = {"No recent mistakes", "No recent lessons",
                                    "No critical items", "Current level complete"};
    static const char* kLoading[4] = {"recent mistakes", "recent lessons", "critical items",
                                      "current level"};
    state().study.gStudyNote[0] = 0;
    state().study.gPracticeSkipped = 0;
    state().study.gPracticeSubjects.clear();
    state().study.gPracticeActive = false;
    const size_t limit = static_cast<size_t>(kk::settingsPracticeSize());
    std::vector<kk::Assignment> fetched;
    std::vector<uint32_t> ids;
    if (mode == 0) {
        size_t total = 0;
        ids = recentMistakeIds(limit, &total);
    } else if (mode == 2) {
        loadCritical();
        for (const kk::CriticalItem& item : state().study.gCritical) {
            ids.push_back(item.subjectId);
            if (ids.size() >= limit) break;
        }
    } else {
        loadPracticePool();
        if (mode == 1)
            fetched = kk::selectRecentLessons(state().study.gPracticePool, kk::settingsPracticeLessonsMode(),
                                              effectiveNow(), limit);
        else
            fetched = kk::selectCurrentLevel(state().study.gPracticePool, state().session.gHaveUser ? state().session.gUser.level : 0, limit);
        if (fetched.empty() && kk::DevToken::has() && kk::DevWifi::connected()) {
            drawLoading("Preparing practice...", kLoading[mode]);
            state().ui.gFreeCanvasForRequest = true;
            kk::AssignmentBatch batch = kk::fetchPracticeAssignments(
                kk::DevToken::load(), mode == 3 ? (state().session.gHaveUser ? state().session.gUser.level : 0) : 0, mode == 3);
            state().ui.gFreeCanvasForRequest = false;
            if (batch.ok && !batch.assignments.empty()) {
                if (mode == 3 && state().session.gHaveUser)
                    for (kk::Assignment& a : batch.assignments) a.level = state().session.gUser.level;
                mergePracticePool(batch.assignments);
                if (state().storage.gFsOk) kk::practicePoolSave(currentAccountKey(), state().study.gPracticePool);
                if (mode == 1)
                    fetched = kk::selectRecentLessons(state().study.gPracticePool,
                                                      kk::settingsPracticeLessonsMode(),
                                                      effectiveNow(), limit);
                else
                    fetched = kk::selectCurrentLevel(state().study.gPracticePool, state().session.gHaveUser ? state().session.gUser.level : 0,
                                                     limit);
            }
        }
        if (fetched.size() > limit) fetched.resize(limit);
        for (const kk::Assignment& a : fetched) ids.push_back(a.subjectId);
    }
    if (ids.empty()) {
        snprintf(state().study.gStudyNote, sizeof state().study.gStudyNote, "%s", kNames[mode]);
        state().display.gDirty = true;
        return;
    }
    drawLoading("Preparing practice...", kLoading[mode]);
    std::vector<uint32_t> missing;
    state().ui.gFreeCanvasForRequest = true;
    resolvePracticeSubjects(ids, &state().study.gPracticeSubjects, &missing);
    state().ui.gFreeCanvasForRequest = false;
    if (mode == 3 && state().session.gHaveUser)
        state().study.gPracticeSubjects.erase(
            std::remove_if(state().study.gPracticeSubjects.begin(), state().study.gPracticeSubjects.end(),
                           [](const kk::Subject& s) { return s.level != state().session.gUser.level; }),
            state().study.gPracticeSubjects.end());
    state().study.gPracticeSkipped = static_cast<uint32_t>(missing.size());
    if (state().study.gPracticeSubjects.empty()) {
        snprintf(state().study.gStudyNote, sizeof state().study.gStudyNote, "Nothing cached for this mode");
        state().display.gDirty = true;
        return;
    }

    const uint32_t now = effectiveNow();
    std::vector<kk::Assignment> practice;
    for (const kk::Assignment& source : fetched) {
        bool have = false;
        for (const kk::Subject& subject : state().study.gPracticeSubjects)
            if (subject.id == source.subjectId) {
                have = true;
                break;
            }
        if (!have) continue;
        kk::Assignment a = source;
        a.availableAt = now;
        a.hidden = false;
        a.pendingReview = false;
        practice.push_back(a);
        if (practice.size() >= limit) break;
    }
    if (practice.empty()) {
        for (const kk::Subject& subject : state().study.gPracticeSubjects) {
            kk::Assignment a;
            a.id = 0;
            a.subjectId = subject.id;
            a.type = subject.type;
            a.srsStage = 1;
            a.availableAt = now;
            practice.push_back(a);
            if (practice.size() >= limit) break;
        }
    }
    state().review.gQueue = kk::ReviewQueue{};
    state().review.gQueue.setMeaningFirst(kk::settingsMeaningFirst());
    state().review.gQueue.setLowerLevelsFirst(false);
    state().review.gQueue.build(practice, state().study.gPracticeSubjects, now, limit, esp_random());
    if (state().review.gQueue.empty()) {
        snprintf(state().study.gStudyNote, sizeof state().study.gStudyNote, "%s", kNames[mode]);
        state().display.gDirty = true;
        return;
    }
    state().study.gPracticeActive = true;
    state().study.gPracticeMode = mode;
    state().study.gStudyMenuActive = false;
    state().study.gCriticalListActive = false;
    state().review.detailFromCritical = false;
    state().review.detailSubjectId = 0;
    kk::screenReset(&state().screen);
    state().screen.screen = Screen::Review;
    state().review.feedback = false;
    state().review.feedbackCompleted = false;
    state().review.detail = false;
    state().review.gReviewElapsedMs = 0;
    state().review.gReviewStartMs = millis();
    state().review.gStageChange = StageChange{};
    state().review.gStageUps = 0;
    state().review.gNeedsSubmit = false;
    state().review.gPendingSeq = 0;
    Serial.printf("[practice] mode %d, %u items, %u skipped\n", mode,
                  static_cast<unsigned>(state().review.gQueue.remaining()),
                  static_cast<unsigned>(state().study.gPracticeSkipped));
    state().display.gDirty = true;
}

}  // namespace

void loadCritical() {
    if (state().study.gCriticalLoaded) return;
    state().study.gCriticalLoaded = true;
    state().study.gCritical.clear();
    if (state().storage.gFsOk) kk::criticalLoad(currentAccountKey(), &state().study.gCritical);
}

bool refreshPracticePool() {
    if (!kk::DevToken::has() || !kk::DevWifi::connected()) return false;
    loadPracticePool();
    bool any = false;
    // Current-level entries first so a full 256-entry pool never drops them.
    if (state().session.gHaveUser) {
        kk::AssignmentBatch level =
            kk::fetchPracticeAssignments(kk::DevToken::load(), state().session.gUser.level, true);
        if (level.ok && !level.assignments.empty()) {
            for (kk::Assignment& a : level.assignments) a.level = state().session.gUser.level;
            mergePracticePool(level.assignments);
            any = true;
        }
    }
    kk::AssignmentBatch all = kk::fetchPracticeAssignments(kk::DevToken::load(), 0, false);
    if (all.ok && !all.assignments.empty()) {
        mergePracticePool(all.assignments);
        any = true;
    }
    if (any && state().storage.gFsOk) kk::practicePoolSave(currentAccountKey(), state().study.gPracticePool);
    return any;
}

void openStudyMenu() {
    state().study.gStudyMenuActive = true;
    state().study.gStudyIndex = 0;
    state().study.gStudyNote[0] = 0;
    refreshStudyCounts();
    state().display.gDirty = true;
}

void drawStudyMenu() {
    if (!state().display.gDisp) return;
    kk::app::StudyMenuView view;
    view.selectedIndex = state().study.gStudyIndex;
    for (int i = 0; i < kk::app::kStudyRows; ++i)
        snprintf(view.counts[i], sizeof view.counts[i], "%s", state().study.gStudyCounts[i]);
    snprintf(view.note, sizeof view.note, "%s", state().study.gStudyNote);
    auto& d = target();
    d.startWrite();
    kk::app::drawStudyMenuScreen(d, view);
    d.endWrite();
    present();
}

void studyMenuKey(const KeyEvent& e) {
    if (!e.down) return;
    if (e.key == Key::Esc) {
        state().study.gStudyMenuActive = false;
        state().display.gDirty = true;
        return;
    }
    if (e.key == Key::Up)
        state().study.gStudyIndex =
            listMoveSelection(state().study.gStudyIndex, kk::app::kStudyRows, -1, true);
    else if (e.key == Key::Down)
        state().study.gStudyIndex =
            listMoveSelection(state().study.gStudyIndex, kk::app::kStudyRows, 1, true);
    else if (e.key == Key::Enter) {
        if (state().study.gStudyIndex == 2)
            openCriticalList();
        else
            startPractice(state().study.gStudyIndex);
    }
    state().display.gDirty = true;
}

void drawCriticalList() {
    if (!state().display.gDisp) return;
    kk::app::CriticalListView view;
    view.rows = state().study.gCriticalRows;
    view.selectedIndex = state().study.gCriticalIndex;
    auto& d = target();
    d.startWrite();
    kk::app::drawCriticalListScreen(d, state().storage.gFontPack, state().storage.gRadicalPack,
                                    view);
    d.endWrite();
    present();
}

void criticalListKey(const KeyEvent& e) {
    if (!e.down) return;
    const int total = static_cast<int>(state().study.gCriticalRows.size());
    if (e.key == Key::Esc) {
        state().study.gCriticalListActive = false;
        state().study.gStudyMenuActive = true;
        state().display.gDirty = true;
        return;
    }
    if (total > 0) {
        if (e.key == Key::Up)
            state().study.gCriticalIndex =
                listMoveSelection(state().study.gCriticalIndex, total, -1, true);
        else if (e.key == Key::Down)
            state().study.gCriticalIndex =
                listMoveSelection(state().study.gCriticalIndex, total, 1, true);
        else if (e.key == Key::Enter) {
            const kk::Subject* s = state().study.gCriticalRows[static_cast<size_t>(state().study.gCriticalIndex)].subject;
            if (s) {
                state().review.detailSubjectId = s->id;
                state().review.detailFromCritical = true;
                state().review.detailPage = 0;
                state().review.detailPages = detailPageCount(*s);
                state().review.detail = true;
                state().study.gCriticalListActive = false;
                state().screen.screen = Screen::Review;
                state().display.gDirty = true;
            }
        } else if (e.key == Key::Other) {
            const char c = tgc::cardputerCharAt(e.x, e.y, state().ui.gShift);
            if (c == 'p' || c == 'P') startPractice(2);
        }
    }
}

}  // namespace kk::app

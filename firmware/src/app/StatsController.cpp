// The read-only stats screen: forecast, SRS, level and history.
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

struct HistoryCollector {
    std::vector<kk::DayStats> days;
    uint32_t todayKey = 0;
    uint32_t oldestKey = 0;
};

uint32_t localDayKeyForEpoch(uint32_t epoch);
bool collectJournalRecord(const kk::JournalRecord& record, void* user);
void loadStatsHistory();
kk::app::StatsView buildStatsView();

uint32_t localDayKeyForEpoch(uint32_t epoch) {
    if (epoch < 1700000000u) return 0;
    const time_t t = static_cast<time_t>(epoch);
    struct tm local {};
    if (!localtime_r(&t, &local)) return 0;
    return kk::civilDayKey(local.tm_year + 1900, static_cast<unsigned>(local.tm_mon + 1),
                           static_cast<unsigned>(local.tm_mday));
}

bool collectJournalRecord(const kk::JournalRecord& record, void* user) {
    HistoryCollector* collector = static_cast<HistoryCollector*>(user);
    if (!collector || record.completedAt == 0) return true;
    const uint32_t key = localDayKeyForEpoch(record.completedAt);
    if (!key || key > collector->todayKey || key < collector->oldestKey) return true;
    kk::DayStats* day = nullptr;
    for (kk::DayStats& candidate : collector->days)
        if (candidate.dayKey == key) {
            day = &candidate;
            break;
        }
    if (!day) {
        collector->days.push_back({key, 0, 0, 0});
        day = &collector->days.back();
    }
    if (day->reviews < 0xFFFF) ++day->reviews;
    const uint16_t wrong =
        static_cast<uint16_t>(record.incorrectMeaning) + record.incorrectReading;
    if (wrong == 0) {
        if (day->correct < 0xFFFF) ++day->correct;
    } else if (day->incorrect < 0xFFFF) {
        ++day->incorrect;
    }
    return true;
}

void loadStatsHistory() {
    state().stats.gStatsHistory = kk::app::StatsHistoryView{};
    state().stats.gStatsHistoryLoaded = true;
    const uint32_t todayKey = localDayKeyForEpoch(effectiveNow());
    if (!todayKey) return;
    if (!kk::journalOpen(currentAccountKey(), false)) return;
    HistoryCollector collector;
    collector.todayKey = todayKey;
    collector.oldestKey = todayKey > 400 ? todayKey - 400 : 0;
    collector.days.reserve(64);
    kk::journalForEach(collectJournalRecord, &collector);

    kk::HistoryStats stats;
    kk::buildHistory(collector.days.data(), collector.days.size(), todayKey, &stats);
    state().stats.gStatsHistory.any = stats.any;
    state().stats.gStatsHistory.hasToday = stats.hasToday;
    state().stats.gStatsHistory.todayAccuracy = stats.todayAccuracy;
    state().stats.gStatsHistory.currentStreak = stats.currentStreak;
    state().stats.gStatsHistory.bestStreak = stats.bestStreak;
    for (size_t i = 0; i < kk::kHistoryDays; ++i) {
        state().stats.gStatsHistory.counts[i] = stats.counts[i];
        const uint32_t key = todayKey >= kk::kHistoryDays - 1
                                 ? todayKey - (kk::kHistoryDays - 1) + i
                                 : i;
        static const char* names[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        snprintf(state().stats.gStatsHistory.labels[i], sizeof state().stats.gStatsHistory.labels[i], "%s",
                 names[(key + 4) % 7]);
    }
}

kk::app::StatsView buildStatsView() {
    kk::app::StatsView view;
    view.page = static_cast<uint8_t>(state().stats.gStatsPage);
    view.pages = 4;
    const uint32_t now = effectiveNow();
    kk::buildForecast(state().session.gSummary, now, &view.forecast);
    for (int i = 0; i < 4; ++i)
        localHm(view.forecast.startHour + static_cast<uint32_t>(i) * 6u * 3600u, view.labels[i],
                sizeof view.labels[i]);
    view.srs = state().session.gSrs;

    view.level.valid = state().session.gLevelProgress.valid;
    view.level.level =
        state().session.gLevelProgress.valid ? state().session.gLevelProgress.level : (state().session.gHaveUser ? state().session.gUser.level : 0);
    const uint32_t since =
        state().session.gLevelProgress.unlockedAt ? state().session.gLevelProgress.unlockedAt : state().session.gLevelProgress.startedAt;
    if (since && now > since) {
        const uint32_t days = (now - since) / 86400u;
        snprintf(view.level.days, sizeof view.level.days, "%u day%s", static_cast<unsigned>(days),
                 days == 1 ? "" : "s");
    }
    view.level.kanjiPassed = state().session.gLevelProgress.kanjiPassed;
    view.level.kanjiTotal = state().session.gLevelProgress.kanjiTotal;
    view.level.kanjiPercent =
        kk::levelKanjiPercent(state().session.gLevelProgress.kanjiPassed, state().session.gLevelProgress.kanjiTotal);
    view.level.needed =
        kk::kanjiNeededForNinetyPercent(state().session.gLevelProgress.kanjiPassed, state().session.gLevelProgress.kanjiTotal);
    view.level.ninetyReached = state().session.gLevelProgress.kanjiTotal > 0 && view.level.needed == 0;
    view.level.apprentice = state().session.gSrs.apprentice;
    view.level.srsValid = state().session.gSrs.valid;

    if (state().stats.gStatsPage == 3) {
        if (!state().stats.gStatsHistoryLoaded) loadStatsHistory();
        view.history = state().stats.gStatsHistory;
    }
    return view;
}

}  // namespace

void openStats() {
    state().stats.gStatsActive = true;
    state().stats.gStatsPage = 0;
    state().stats.gStatsHistoryLoaded = false;
    state().display.gDirty = true;
}

void statsKey(const KeyEvent& e) {
    if (!e.down) return;
    if (e.key == Key::Esc) {
        state().stats.gStatsActive = false;
        state().display.gDirty = true;
        return;
    }
    if (e.key == Key::Left && state().stats.gStatsPage > 0) {
        --state().stats.gStatsPage;
        state().display.gDirty = true;
    } else if (e.key == Key::Right && state().stats.gStatsPage < 3) {
        ++state().stats.gStatsPage;
        state().display.gDirty = true;
    }
}

void drawStats() {
    if (!state().display.gDisp) return;
    const kk::app::StatsView view = buildStatsView();
    auto& d = target();
    d.startWrite();
    kk::app::drawStatsScreen(d, view);
    d.endWrite();
    present();
}

}  // namespace kk::app

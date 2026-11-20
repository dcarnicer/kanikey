// Shared UI primitives: canvas, frame target, log, loading/notice frames.
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
#include "ui/UiFont.h"

namespace kk::app {
namespace {

using tgc::Key;
using tgc::KeyEvent;
using namespace kk::ui;

// Set only when requestBefore() actually dropped the canvas. A session keeps it
// released across requests; the single requestAfter() at wkSessionEnd() then
// brings it back even if the caller cleared gFreeCanvasForRequest meanwhile.
bool gCanvasReleasedForRequest = false;

}  // namespace

lgfx::LovyanGFX& target() {
    if (state().display.gCanvas) return *state().display.gCanvas;
    return *state().display.gDisp;
}

void present() {
    if (state().display.gCanvas && state().display.gDisp) state().display.gCanvas->pushSprite(0, 0);
}

bool ensureCanvas() {
    if (state().display.gCanvas || !state().display.gDisp) return state().display.gCanvas != nullptr;
    for (int attempt = 0; attempt < 4; ++attempt) {
        state().display.gCanvas = new lgfx::LGFX_Sprite(state().display.gDisp);
        state().display.gCanvas->setColorDepth(lgfx::rgb332_1Byte);
        if (state().display.gCanvas->createSprite(state().display.gDisp->width(), state().display.gDisp->height())) return true;
        Serial.printf("[kk] canvas attempt %d failed, heap %u largest %u\n", attempt + 1,
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                             MALLOC_CAP_8BIT)));
        delete state().display.gCanvas;
        state().display.gCanvas = nullptr;
        if (attempt < 3) delay(120);
    }
    Serial.println("[kk] drawing directly");
    return false;
}

void releaseCanvas() {
    if (!state().display.gCanvas) return;
    delete state().display.gCanvas;
    state().display.gCanvas = nullptr;
}

void requestBefore() {
    if (!state().ui.gFreeCanvasForRequest) return;
    if (!state().display.gCanvas) return;  // already drawing directly
    releaseCanvas();
    gCanvasReleasedForRequest = true;
}

void requestAfter() {
    if (!gCanvasReleasedForRequest) return;
    // While a keep-alive sequence is open its session restores the canvas
    // once, at wkSessionEnd(), instead of after every request.
    if (kk::wkSessionActive()) return;
    // Keep the flag when allocation still fails, so the next request retries
    // exactly like the old per-request hook did.
    if (ensureCanvas()) gCanvasReleasedForRequest = false;
}

const char* keyName(Key k) {
    switch (k) {
        case Key::Up: return "Up";
        case Key::Down: return "Down";
        case Key::Left: return "Left";
        case Key::Right: return "Right";
        case Key::Enter: return "Enter";
        case Key::Esc: return "Esc";
        case Key::Backspace: return "Backspace";
        case Key::Tab: return "Tab";
        case Key::Other: return "Other";
        case Key::None: return "None";
    }
    return "?";
}

void pushLog(const char* text) {
    for (int i = 0; i < 2; ++i) state().ui.gLog[i] = state().ui.gLog[i + 1];
    snprintf(state().ui.gLog[2].text, sizeof state().ui.gLog[2].text, "%s", text);
}

void updateModifier(const KeyEvent& e) {
    if (e.source != tgc::KeySource::Matrix) {
        // A BLE/button event carries the whole held set, so replace it rather
        // than toggling one key. Fn is matrix-only (HID has no Fn) and keeps
        // whatever the matrix last set. Gui has no Cardputer key of its own;
        // it follows Opt, the extra modifier the app already tracks.
        state().ui.gShift = (e.mods & tgc::kModShift) != 0;
        state().ui.gCtrl = (e.mods & tgc::kModCtrl) != 0;
        state().ui.gOpt = (e.mods & tgc::kModGui) != 0;
        state().ui.gAlt = (e.mods & tgc::kModAlt) != 0;
        return;
    }
    switch (tgc::cardputerModifierAt(e.x, e.y)) {
        case tgc::Modifier::Shift: state().ui.gShift = e.down; break;
        case tgc::Modifier::Fn: state().ui.gFn = e.down; break;
        case tgc::Modifier::Ctrl: state().ui.gCtrl = e.down; break;
        case tgc::Modifier::Opt: state().ui.gOpt = e.down; break;
        case tgc::Modifier::Alt: state().ui.gAlt = e.down; break;
        case tgc::Modifier::None: break;
    }
}

bool shift() { return state().ui.gShift; }

void localHm(uint32_t epoch, char* out, size_t n) {
    const time_t t = static_cast<time_t>(epoch);
    struct tm tm {};
    if (!localtime_r(&t, &tm)) {
        snprintf(out, n, "--:--");
        return;
    }
    snprintf(out, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

void localDate(uint32_t epoch, char* out, size_t n) {
    if (epoch < 1700000000u) {
        if (n) out[0] = '\0';
        return;
    }
    const time_t t = static_cast<time_t>(epoch);
    struct tm tm {};
    if (!localtime_r(&t, &tm) || !std::strftime(out, n, "%d %b", &tm)) {
        if (n) out[0] = '\0';
    }
}

void drawLoading(const char* title, const char* subtitle) {
    if (!state().display.gDisp) return;
    kk::app::LoadingView view;
    view.title = title;
    view.subtitle = subtitle;
    auto& d = target();
    d.startWrite();
    kk::app::drawLoadingScreen(d, view);
    d.endWrite();
    present();
}

// Blocking notice frame: the lessons empty state's shape with a caller-supplied
// title, line and key hint. Used for review empty/error states, a full outbox
// and the storage prompt.
void drawNotice() {
    if (!state().display.gDisp) return;
    const char* header = "REVIEWS";
    const char* title = "";
    const char* hint = "Enter: retry  Esc: home";
    switch (state().ui.gNoticeAction) {
        case NoticeAction::ReviewEmpty:
            title = "No reviews now";
            hint = "Enter: sync  Esc: home";
            break;
        case NoticeAction::ReviewError:
            title = "Could not load reviews";
            break;
        case NoticeAction::LessonError:
            header = "LESSONS";
            title = "No lessons available";
            break;
        case NoticeAction::LessonStart:
            header = "LESSONS";
            title = "Could not start lesson";
            hint = "Enter: retry  Esc: home";
            break;
        case NoticeAction::OutboxFull:
            title = "Outbox full";
            hint = "Enter: sync  Esc: home";
            break;
        case NoticeAction::StorageFormat:
            header = "STORAGE";
            title = "Storage unreadable";
            hint = "Enter: format  Esc: continue";
            break;
        default:
            return;
    }

    auto& d = target();
    d.startWrite();
    d.fillScreen(ui::kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    ui::applyFont(d, ui::UiFont::Base);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString(header, 6, 3);
    d.setTextFont(4);
    d.setTextSize(1);
    d.setTextColor(ui::kText, ui::kBg);
    // "Could not start lesson" is wider than the screen in Font4.
    if (d.textWidth(title) > 228) d.setTextFont(2);
    d.drawString(title, 8, 38);
    ui::applyFont(d, ui::UiFont::Base);
    d.setTextColor(ui::kDim, ui::kBg);
    d.drawString(state().ui.gNoticeLine, 8, 82);
    if (state().ui.gNoticeLine2[0]) d.drawString(state().ui.gNoticeLine2, 8, 102);
    ui::applyFont(d, ui::UiFont::Small);
    d.setTextColor(ui::kFaint, ui::kBg);
    d.drawString(hint, 8, 125);
    d.endWrite();
    present();
}

void noticeKey(const KeyEvent& e) {
    if (!e.down) return;
    if (state().storage.gFsFormatPrompt) {
        if (e.key == Key::Esc) {
            // Continue without storage: the in-memory outbox and cache only.
            state().storage.gFsFormatPrompt = false;
            state().storage.gFsOk = false;
            state().ui.gNoticeActive = false;
            state().ui.gNoticeAction = NoticeAction::None;
            snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Storage unavailable");
            Serial.println("[kk] continuing without storage");
            continueBootAfterStorage();
            return;
        }
        if (e.key == Key::Enter) {
            if (LittleFS.begin(true)) {
                state().storage.gFsOk = true;
                state().storage.gFsFormatPrompt = false;
                state().ui.gNoticeActive = false;
                state().ui.gNoticeAction = NoticeAction::None;
                kk::outboxLoad(state().outbox.gOutbox);
                Serial.printf("[outbox] pending %u\n",
                              static_cast<unsigned>(kk::outboxPendingCount(state().outbox.gOutbox)));
                continueBootAfterStorage();
            } else {
                snprintf(state().ui.gNoticeLine, sizeof state().ui.gNoticeLine, "Format failed; try again");
                state().display.gDirty = true;
            }
            return;
        }
        return;
    }
    if (e.key == Key::Esc) {
        state().ui.gNoticeActive = false;
        if (state().ui.gNoticeAction == NoticeAction::LessonStart) {
            // Leave the lesson like the 401 path does; the card is still the
            // current item until the summary is refreshed by a sync.
            state().ui.gNoticeAction = NoticeAction::None;
            state().screen.screen = kk::Screen::Home;
            releaseLessonData();
        } else if (kk::screenBase(state().screen) == kk::Screen::ReviewSummary) {
            // A failed retry from the summary leaves the summary underneath,
            // exactly as the old summary flag did.
        } else {
            state().screen.screen =
                state().session.gHaveSummary ? kk::Screen::Home : kk::Screen::Boot;
        }
        state().display.gDirty = true;
        return;
    }
    if (e.key != Key::Enter) return;
    const NoticeAction action = state().ui.gNoticeAction;
    state().ui.gNoticeActive = false;
    if (action == NoticeAction::ReviewError) {
        startReview();
    } else if (action == NoticeAction::LessonError) {
        startLessons();
    } else if (action == NoticeAction::LessonStart) {
        // The batch is still in RAM, so the retry opens the start confirmation
        // again; it never sends a start without Enter on that screen.
        state().ui.gNoticeAction = NoticeAction::None;
        startCurrentLesson();
    } else if (action == NoticeAction::OutboxFull || action == NoticeAction::ReviewEmpty) {
        startSync();
    }
    state().display.gDirty = true;
}

}  // namespace kk::app

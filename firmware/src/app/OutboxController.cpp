// Review outbox: queueing, sending, reconciling and the attention screen.
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
using kk::Modal;
using kk::Screen;
using namespace kk::ui;

bool outboxNeedsAttention(const kk::OutboxItem& item);
bool reconcileOutboxItem(kk::OutboxItem& item);
bool sendOutboxItem(uint32_t seq);
std::vector<const kk::OutboxItem*> attentionOutboxItems();
const kk::OutboxItem* selectedOutboxItem();
std::string outboxItemLabel(const kk::OutboxItem& item);
void outboxReasonShort(const kk::OutboxItem& item, char* out, size_t n);
std::string outboxAgeLabel(const kk::OutboxItem& item, uint32_t now);
void outboxReasonDetail(const kk::OutboxItem& item, char* out, size_t n);
void moveOutboxSelection(int delta);
void discardSelectedOutboxItem();

bool outboxNeedsAttention(const kk::OutboxItem& item) {
    return item.state == kk::OutboxState::Rejected || item.state == kk::OutboxState::Stuck;
}

bool reconcileOutboxItem(kk::OutboxItem& item) {
    if (item.createdAt == 0) return false;
    const String token = kk::DevToken::load();
    String path = "/v2/assignments?ids=";
    path += String(static_cast<unsigned long>(item.assignmentId));
    const kk::WkResponse r = kk::wkGet(path, token, 4096, 12000);
    if (r.httpCode != 200) {
        Serial.printf("[outbox] reconcile %u -> HTTP %d\n",
                      static_cast<unsigned>(item.assignmentId), r.httpCode);
        return false;
    }
    kk::JsonParser p;
    kk::JsonValue v;
    if (!p.parse(r.body, &v)) return false;
    const std::vector<kk::Assignment> as = kk::parseAssignments(v);
    for (const kk::Assignment& a : as) {
        if (a.id != item.assignmentId) continue;
        if (a.dataUpdatedAt > item.createdAt && a.availableAt > item.createdAt) {
            const uint32_t seq = item.seq;
            const uint32_t assignmentId = item.assignmentId;
            const uint32_t subjectId = item.subjectId;
            kk::outboxRemove(state().outbox.gOutbox, seq);
            saveOutbox();
            state().review.gStageTracker.forget(assignmentId);
            if (seq == state().review.gPendingSeq) {
                state().review.gPendingSeq = 0;
                state().review.gNeedsSubmit = false;
            }
            markCacheCompleted(subjectId, assignmentId);
            Serial.printf("[outbox] %u was already accepted\n",
                          static_cast<unsigned>(assignmentId));
            return true;
        }
        break;
    }
    return false;
}

bool sendOutboxItem(uint32_t seq) {
    kk::OutboxItem* item = kk::outboxFind(state().outbox.gOutbox, seq);
    if (!item) return true;
    if (outboxNeedsAttention(*item)) return false;
    if (item->state == kk::OutboxState::Sending ||
        item->state == kk::OutboxState::Ambiguous) {
        if (reconcileOutboxItem(*item)) return true;
        item = kk::outboxFind(state().outbox.gOutbox, seq);
        if (!item) return true;
        if (kk::wkRateLimitWaitSeconds() > 0 || kk::wkAuthBlocked()) {
            // The GET was blocked by the shared 429/401 gate, so the POST may
            // have succeeded earlier; keep it ambiguous instead of resending.
            item->state = kk::OutboxState::Ambiguous;
            saveOutbox();
            if (kk::wkAuthBlocked()) state().sync.gDeferredAuthIssue = SyncIssue::Auth;
            Serial.printf("[outbox] reconcile %u gate blocked; keeping ambiguous\n",
                          static_cast<unsigned>(item->assignmentId));
            return false;
        }
        item->state = kk::OutboxState::Pending;
        saveOutbox();
    }

    if (kk::outboxShouldGiveUp(item->attempts)) {
        item->state = kk::OutboxState::Stuck;
        saveOutbox();
        Serial.printf("[outbox] assignment %u stuck after %u attempts\n",
                      static_cast<unsigned>(item->assignmentId),
                      static_cast<unsigned>(item->attempts));
        return false;
    }

    item->state = kk::OutboxState::Sending;
    if (item->attempts < 255) ++item->attempts;
    saveOutbox();

    String body = "{\"review\":{\"assignment_id\":";
    body += String(static_cast<unsigned long>(item->assignmentId));
    body += ",\"incorrect_meaning_answers\":";
    body += String(static_cast<unsigned>(item->incorrectMeaning));
    body += ",\"incorrect_reading_answers\":";
    body += String(static_cast<unsigned>(item->incorrectReading));
    if (item->createdAt) {
        const std::string created = kk::formatIso8601(item->createdAt);
        body += ",\"created_at\":\"";
        body += created.c_str();
        body += "\"";
    }
    body += "}}";

    state().ui.gFreeCanvasForRequest = true;
    const kk::WkResponse r = kk::wkPost("/v2/reviews", kk::DevToken::load(), body, 4096);
    state().ui.gFreeCanvasForRequest = false;

    item = kk::outboxFind(state().outbox.gOutbox, seq);
    if (!item) return true;
    if (r.httpCode >= 200 && r.httpCode < 300) {
        const uint32_t assignmentId = item->assignmentId;
        const uint32_t subjectId = item->subjectId;
        // SPEED-01 keeps one journal handle per queued record; the old single
        // pending handle remains as a fallback for the last completion.
        uint32_t journalHandle = state().review.gStageTracker.tagFor(assignmentId);
        if (!journalHandle && item->seq == state().review.gPendingSeq)
            journalHandle = state().review.gPendingJournalHandle;
        const bool ownJournal = journalHandle != 0;
        kk::JsonParser rp;
        kk::JsonValue rv;
        if (rp.parse(r.body, &rv)) {
            kk::ReviewUpdate update;
            if (kk::parseReviewUpdate(rv, &update)) {
                applyReviewUpdate(update);
                if (ownJournal)
                    kk::journalPatchSrsAfter(journalHandle, update.srsStage);
                Serial.printf("[review] assignment %u SRS %u -> %u\n",
                              static_cast<unsigned>(assignmentId),
                              static_cast<unsigned>(state().review.gStageChange.from),
                              static_cast<unsigned>(state().review.gStageChange.to));
            }
        }
        if (item->seq == state().review.gPendingSeq)
            state().review.gPendingJournalHandle = 0;
        kk::outboxRemove(state().outbox.gOutbox, seq);
        saveOutbox();
        state().review.gStageTracker.forget(assignmentId);
        if (seq == state().review.gPendingSeq) {
            state().review.gPendingSeq = 0;
            state().review.gNeedsSubmit = false;
        }
        markCacheCompleted(subjectId, assignmentId);
        Serial.printf("[outbox] sent assignment %u (%d)\n",
                      static_cast<unsigned>(assignmentId), r.httpCode);
        return true;
    }
    if (!r.connected)
        item->state = kk::OutboxState::Pending;  // never reached the server
    else if (r.httpCode == 0 || r.httpCode >= 500)
        item->state = kk::OutboxState::Ambiguous;  // may have been accepted
    else if (r.httpCode == 401) {
        item->state = kk::OutboxState::Pending;
        state().sync.gDeferredAuthIssue = SyncIssue::Auth;
        setSyncIssue(401, true, 0, "Review");
        state().sync.gOutboxAuthIssue = true;
    } else if (r.httpCode == 403) {
        item->state = kk::OutboxState::Pending;
        state().sync.gDeferredAuthIssue = SyncIssue::Forbidden;
        setSyncIssue(403, true, 0, "Review");
        state().sync.gOutboxAuthIssue = true;
    } else if (r.httpCode == 429)
        item->state = kk::OutboxState::Pending;
    else {
        item->state = kk::OutboxState::Rejected;
        item->httpCode = static_cast<uint16_t>(r.httpCode);
    }
    saveOutbox();
    Serial.printf("[outbox] assignment %u -> HTTP %d, state %u\n",
                  static_cast<unsigned>(item->assignmentId), r.httpCode,
                  static_cast<unsigned>(item->state));
    return false;
}

std::vector<const kk::OutboxItem*> attentionOutboxItems() {
    std::vector<const kk::OutboxItem*> items;
    for (const kk::OutboxItem& it : state().outbox.gOutbox)
        if (outboxNeedsAttention(it)) items.push_back(&it);
    return items;
}

const kk::OutboxItem* selectedOutboxItem() {
    const kk::OutboxItem* selected = kk::outboxFind(state().outbox.gOutbox, state().outbox.gOutboxSelectedSeq);
    if (selected && outboxNeedsAttention(*selected)) return selected;
    for (const kk::OutboxItem& it : state().outbox.gOutbox) {
        if (!outboxNeedsAttention(it)) continue;
        state().outbox.gOutboxSelectedSeq = it.seq;
        return &it;
    }
    state().outbox.gOutboxSelectedSeq = 0;
    return nullptr;
}

std::string outboxItemLabel(const kk::OutboxItem& item) {
    const kk::Subject* subject = subjectById(item.subjectId);
    if (subject) {
        const std::string meaning = subjectMainMeaning(*subject);
        if (!meaning.empty()) {
            if (subject->level) {
                char level[12];
                snprintf(level, sizeof level, " L%u", static_cast<unsigned>(subject->level));
                return meaning + level;
            }
            return meaning;
        }
    }
    char fallback[32];
    if (item.assignmentId)
        snprintf(fallback, sizeof fallback, "assignment %u",
                 static_cast<unsigned>(item.assignmentId));
    else
        snprintf(fallback, sizeof fallback, "subject %u",
                 static_cast<unsigned>(item.subjectId));
    return fallback;
}

void outboxReasonShort(const kk::OutboxItem& item, char* out, size_t n) {
    if (item.state == kk::OutboxState::Stuck)
        snprintf(out, n, "gave up");
    else if (item.httpCode)
        snprintf(out, n, "HTTP %u", static_cast<unsigned>(item.httpCode));
    else
        snprintf(out, n, "rejected");
}

std::string outboxAgeLabel(const kk::OutboxItem& item, uint32_t now) {
    if (!item.createdAt || now < 1700000000u || now < item.createdAt) return "unknown";
    const uint32_t age = now - item.createdAt;
    char out[16];
    if (age < 60)
        snprintf(out, sizeof out, "just now");
    else if (age < 3600)
        snprintf(out, sizeof out, "%um", static_cast<unsigned>(age / 60));
    else if (age < 86400)
        snprintf(out, sizeof out, "%uh", static_cast<unsigned>(age / 3600));
    else
        snprintf(out, sizeof out, "%ud", static_cast<unsigned>(age / 86400));
    return out;
}

void outboxReasonDetail(const kk::OutboxItem& item, char* out, size_t n) {
    const std::string age =
        outboxAgeLabel(item, static_cast<uint32_t>(time(nullptr)));
    const unsigned attempts = item.attempts;
    const char* suffix = attempts == 1 ? "attempt" : "attempts";
    if (item.state == kk::OutboxState::Stuck) {
        snprintf(out, n, "Not sent after %u %s. Age %s.", attempts, suffix, age.c_str());
        return;
    }
    if (item.httpCode == 422)
        snprintf(out, n, "WaniKani rejected this review (HTTP 422). %u %s, age %s.", attempts,
                 suffix, age.c_str());
    else if (item.httpCode == 409)
        snprintf(out, n, "Conflict (409); it may have changed. %u %s, age %s.", attempts,
                 suffix, age.c_str());
    else if (item.httpCode)
        snprintf(out, n, "WaniKani rejected this review (HTTP %u). %u %s, age %s.",
                 static_cast<unsigned>(item.httpCode), attempts, suffix, age.c_str());
    else
        snprintf(out, n, "WaniKani rejected this review. %u %s, age %s.", attempts, suffix,
                 age.c_str());
}

void moveOutboxSelection(int delta) {
    const std::vector<const kk::OutboxItem*> items = attentionOutboxItems();
    if (items.empty()) return;
    int index = -1;
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i]->seq == state().outbox.gOutboxSelectedSeq) index = static_cast<int>(i);
    if (index < 0) index = 0;
    index = (index + delta + static_cast<int>(items.size())) %
            static_cast<int>(items.size());
    state().outbox.gOutboxSelectedSeq = items[static_cast<size_t>(index)]->seq;
    state().display.gDirty = true;
}

void discardSelectedOutboxItem() {
    const kk::OutboxItem* selected = selectedOutboxItem();
    if (!selected) return;
    const uint32_t seq = selected->seq;
    const uint32_t assignmentId = selected->assignmentId;

    int index = 0;
    bool found = false;
    for (const kk::OutboxItem& it : state().outbox.gOutbox) {
        if (!outboxNeedsAttention(it)) continue;
        if (it.seq == seq) {
            found = true;
            break;
        }
        ++index;
    }
    if (!found) return;

    // Dismiss only after the new file is safely written. A failed save keeps
    // the in-memory item and shows a note instead of pretending it is gone.
    std::vector<kk::OutboxItem> next = state().outbox.gOutbox;
    if (!kk::outboxRemove(next, seq)) return;
    if (state().storage.gFsOk && !kk::outboxSave(next)) {
        snprintf(state().outbox.gOutboxNote, sizeof state().outbox.gOutboxNote, "Could not save outbox");
        Serial.println("[outbox] discard save failed");
        state().display.gDirty = true;
        return;
    }

    std::vector<uint32_t> remaining;
    for (const kk::OutboxItem& it : next)
        if (outboxNeedsAttention(it)) remaining.push_back(it.seq);

    state().outbox.gOutbox = std::move(next);
    if (kk::screenTop(state().screen) == Modal::OutboxConfirm)
        kk::screenPop(&state().screen);
    state().outbox.gOutboxNote[0] = 0;
    if (remaining.empty()) {
        state().screen = state().outbox.returnScreen;
        state().outbox.gOutboxSelectedSeq = 0;
    } else {
        if (index >= static_cast<int>(remaining.size()))
            index = static_cast<int>(remaining.size()) - 1;
        state().outbox.gOutboxSelectedSeq = remaining[static_cast<size_t>(index)];
    }
    Serial.printf("[outbox] discarded seq %u assignment %u\n", static_cast<unsigned>(seq),
                  static_cast<unsigned>(assignmentId));
    state().display.gDirty = true;
}

}  // namespace

bool outboxHasAssignment(uint32_t assignmentId) {
    for (const kk::OutboxItem& it : state().outbox.gOutbox)
        if (it.assignmentId == assignmentId) return true;
    return false;
}

// Every record occupies a slot, whatever its state (rejected, stuck, sending
// and ambiguous included). startReview() refuses to start when this is smaller
// than the batch, so a completed review always has a place to go.
size_t outboxFreeSlots() {
    return state().outbox.gOutbox.size() < kk::kOutboxMax ? kk::kOutboxMax - state().outbox.gOutbox.size() : 0;
}

void saveOutbox() {
    if (!state().storage.gFsOk) return;  // no storage: the in-memory outbox is all we have
    if (!kk::outboxSave(state().outbox.gOutbox)) Serial.println("[outbox] save failed");
}

bool queueReviewLocal(const kk::SessionItem& item) {
    uint32_t created = static_cast<uint32_t>(time(nullptr));
    if (created < 1700000000u) created = 0;  // clock not trusted; let the server stamp it
    const uint32_t seq = kk::outboxAdd(state().outbox.gOutbox, item.assignmentId, item.subjectId,
                                       item.incorrectMeaning, item.incorrectReading, created);
    if (!seq) {
        // A session starts only with enough free slots (startReview), so this
        // is a safety net: report loudly, never drop the review silently.
        Serial.println("[outbox] full; review not queued");
        snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Outbox full; review not saved");
        state().review.gPendingSeq = 0;
        return false;
    }
    state().review.gPendingSeq = seq;
    state().review.gPendingJournalHandle = 0;
    if (state().storage.gFsOk && kk::journalOpen(currentAccountKey())) {
        kk::JournalRecord record;
        record.completedAt = created;
        record.subjectId = item.subjectId;
        record.assignmentId = item.assignmentId;
        record.srsBefore = item.srsStage;
        record.wrongAnswerOverride = item.wrongAnswerOverride;
        record.offline = !kk::DevWifi::connected();
        record.incorrectMeaning =
            static_cast<uint8_t>(std::min<uint16_t>(item.incorrectMeaning, 255));
        record.incorrectReading =
            static_cast<uint8_t>(std::min<uint16_t>(item.incorrectReading, 255));
        state().review.gPendingJournalHandle = kk::journalAppend(record);
    }
    // SPEED-01: remember this record's pre-review stage and journal handle for
    // every item, not only the last one, so each POST can patch its own record.
    state().review.gStageTracker.record(seq, item.assignmentId, item.srsStage,
                                        state().review.gPendingJournalHandle);
    saveOutbox();
    Serial.printf("[outbox] queued seq %u assignment %u\n", static_cast<unsigned>(seq),
                  static_cast<unsigned>(item.assignmentId));
    return true;
}

bool sendQueuedReview(uint32_t seq) {
    return seq ? sendOutboxItem(seq) : true;
}

void flushOutbox() {
    if (state().outbox.gOutbox.empty()) {
        clearOutboxAuthIssue();
        return;
    }
    Serial.printf("[outbox] flushing %u\n", static_cast<unsigned>(state().outbox.gOutbox.size()));
    // One TLS connection for all items (SPEED-05). The guard is a no-op when a
    // sync job already owns a session.
    kk::WkSessionGuard session;
    std::vector<uint32_t> seqs;
    for (const kk::OutboxItem& it : state().outbox.gOutbox)
        if (!outboxNeedsAttention(it)) seqs.push_back(it.seq);
    for (uint32_t seq : seqs) {
        if (!sendOutboxItem(seq)) {
            const kk::OutboxItem* it = kk::outboxFind(state().outbox.gOutbox, seq);
            if (!it || outboxNeedsAttention(*it)) continue;
            break;
        }
    }
    if (kk::outboxPendingCount(state().outbox.gOutbox) == 0) clearOutboxAuthIssue();
    Serial.printf("[outbox] %u pending, %u need attention (%u stuck)\n",
                  static_cast<unsigned>(kk::outboxPendingCount(state().outbox.gOutbox)),
                  static_cast<unsigned>(kk::outboxAttentionCount(state().outbox.gOutbox)),
                  static_cast<unsigned>(kk::outboxStuckCount(state().outbox.gOutbox)));
}

void openOutbox() {
    // The empty state explains itself, so the list opens even when nothing
    // needs attention (Settings › Sync & data › Outbox). Opening from Settings
    // must return there, so the whole screen state is restored on Esc.
    state().outbox.returnScreen = state().screen;
    kk::screenReset(&state().screen);
    state().screen.screen = Screen::Outbox;
    state().outbox.gOutboxSelectedSeq = 0;
    state().outbox.gOutboxNote[0] = 0;
    selectedOutboxItem();
    state().display.gDirty = true;
}

void journalPractice(const kk::SessionItem& item) {
    if (!state().storage.gFsOk || !kk::journalOpen(currentAccountKey())) return;
    kk::JournalRecord record;
    uint32_t created = static_cast<uint32_t>(time(nullptr));
    record.completedAt = created >= 1700000000u ? created : 0;
    record.subjectId = item.subjectId;
    record.assignmentId = item.assignmentId;
    record.srsBefore = item.srsStage;
    record.wrongAnswerOverride = item.wrongAnswerOverride;
    record.practice = true;
    record.offline = !kk::DevWifi::connected();
    record.incorrectMeaning =
        static_cast<uint8_t>(std::min<uint16_t>(item.incorrectMeaning, 255));
    record.incorrectReading =
        static_cast<uint8_t>(std::min<uint16_t>(item.incorrectReading, 255));
    kk::journalAppend(record);
}

void outboxKey(const KeyEvent& e) {
    if (!e.down) return;
    if (kk::screenTop(state().screen) == Modal::OutboxConfirm) {
        if (e.key == Key::Esc) {
            kk::screenPop(&state().screen);
            state().outbox.gOutboxNote[0] = 0;
            state().display.gDirty = true;
        } else if (e.key == Key::Enter) {
            discardSelectedOutboxItem();
        }
        return;
    }
    if (e.key == Key::Esc) {
        state().screen = state().outbox.returnScreen;
        state().outbox.gOutboxNote[0] = 0;
        state().display.gDirty = true;
        return;
    }
    if (e.key == Key::Up) {
        state().outbox.gOutboxNote[0] = 0;
        moveOutboxSelection(-1);
    } else if (e.key == Key::Down) {
        state().outbox.gOutboxNote[0] = 0;
        moveOutboxSelection(1);
    } else if (e.key == Key::Enter) {
        if (selectedOutboxItem()) {
            kk::screenPush(&state().screen, Modal::OutboxConfirm);
            state().outbox.gOutboxNote[0] = 0;
            state().display.gDirty = true;
        }
    } else if (e.key == Key::Other) {
        const char c = tgc::cardputerCharAt(e.x, e.y, state().ui.gShift);
        if ((c == 'd' || c == 'D') && selectedOutboxItem()) {
            kk::screenPush(&state().screen, Modal::OutboxConfirm);
            state().outbox.gOutboxNote[0] = 0;
            state().display.gDirty = true;
        }
    }
}

void drawOutbox() {
    if (!state().display.gDisp) return;
    auto& d = target();
    d.startWrite();

    OutboxView view;
    const std::vector<const kk::OutboxItem*> items = attentionOutboxItems();
    const kk::OutboxItem* selected = selectedOutboxItem();
    view.total = static_cast<unsigned>(kk::outboxAttentionCount(state().outbox.gOutbox));
    view.rows.reserve(items.size());
    if (!items.empty()) view.selectedIndex = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        const kk::OutboxItem& item = *items[i];
        OutboxRowView row;
        row.label = outboxItemLabel(item);
        char reason[16];
        outboxReasonShort(item, reason, sizeof reason);
        row.reason = reason;
        row.reasonKind = item.state == kk::OutboxState::Stuck
                             ? OutboxReasonKind::Warn
                             : (item.httpCode ? OutboxReasonKind::Err
                                              : OutboxReasonKind::Faint);
        view.rows.push_back(std::move(row));
        if (item.seq == state().outbox.gOutboxSelectedSeq) view.selectedIndex = static_cast<int>(i);
    }
    if (selected) {
        view.confirm = kk::screenTop(state().screen) == Modal::OutboxConfirm;
        if (view.confirm) {
            view.confirmLabel = outboxItemLabel(*selected);
            char reason[128];
            outboxReasonDetail(*selected, reason, sizeof reason);
            view.confirmReason = reason;
        }
    }
    view.note = state().outbox.gOutboxNote;

    drawOutboxScreen(d, view);
    d.endWrite();
    present();
}

}  // namespace kk::app

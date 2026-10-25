// KaniKey — first hardware milestone.
//
// What this build proves on a Cardputer ADV:
//   - the board layer starts (display, TCA8418 keyboard, battery);
//   - the keyboard matrix delivers down/up events and grid positions;
//   - the microSD mounts on SPI3 and reports its size;
//   - the internal heap is measured before any WaniKani/TLS work.
//
// It does not talk to WaniKani yet. The next milestone adds the Wi-Fi setup and
// a TLS GET /user with heap before/after, as described in docs/design.md.

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <LittleFS.h>
#include <esp_system.h>
#include <time.h>

#include <algorithm>
#include <cstring>

#include "board/Board.h"
#include "platform/DevToken.h"
#include "platform/DevWifi.h"
#include "platform/GlyphPack.h"
#include "platform/WkProbe.h"
#include "platform/OutboxStore.h"
#include "platform/WkClient.h"
#include "platform/WkLessons.h"
#include "platform/WkReview.h"
#include "wk/AnswerChecker.h"
#include "wk/Outbox.h"
#include "wk/ReviewQueue.h"
#include "wk/RomajiIme.h"
#include "wk/Time.h"
#include "wk/Utf8.h"

namespace {

using tgc::Key;
using tgc::KeyEvent;

constexpr uint16_t kBg = 0x0000;
constexpr uint16_t kSurface = 0x0108;   // 8-bit: very dark teal-slate
constexpr uint16_t kSurfaceHi = 0x2128; // 8-bit: dark navy
constexpr uint16_t kLine = 0x2208;      // 8-bit: muted teal line
constexpr uint16_t kText = 0xFFFF;
constexpr uint16_t kDim = 0xA577;
constexpr uint16_t kFaint = 0x636F;
constexpr uint16_t kBrand = 0xE308;  // #FF7A59, 8-bit coral
constexpr uint16_t kOk = 0x5EF1;
constexpr uint16_t kOkBg = 0x0183;
constexpr uint16_t kAccent = 0x2B1B;
constexpr uint16_t kAccentHi = 0x7D5F;
constexpr uint16_t kReading = 0x2D14;
constexpr uint16_t kErr = 0xC945;
constexpr uint16_t kErrBg = 0x3882;
constexpr uint16_t kWarn = 0xFE26;

lgfx::LGFX_Device* gDisp = nullptr;
lgfx::LGFX_Sprite* gCanvas = nullptr;
bool gDirty = true;
uint32_t gLastStatusMs = 0;
uint32_t gLastDrawMs = 0;

bool gSdOk = false;
char gSdLine[48] = "SD: not mounted";
uint32_t gLastInputAt = 0;
bool gDimmed = false;
bool gScreenOff = false;
char gNetLine[48] = "NET: not tested";
String gSerialLine;

kk::User gUser;
kk::Summary gSummary;
bool gHaveUser = false;
bool gHaveSummary = false;
bool gShowHome = false;
uint32_t gDueReviews = 0;
uint32_t gLessons = 0;
uint32_t gSyncedAt = 0;
char gNextReview[12] = "--:--";
char gSyncClock[12] = "--:--";

kk::ReviewBatch gBatch;
kk::ReviewQueue gQueue;
bool gReviewActive = false;
bool gReviewSummary = false;
bool gFeedback = false;
bool gNeedsSubmit = false;
bool gFeedbackCompleted = false;
kk::AnswerCheck gCheck;
kk::ReviewPart gFeedbackPart = kk::ReviewPart::None;
uint32_t gFeedbackSubjectId = 0;
std::string gFeedbackInput;
kk::SessionItem gCompletedItem;
std::string gMeaningInput;
std::string gDirectReading;  // test/serial reading input, bypasses the romaji IME
kk::RomajiIme gIme;
std::vector<kk::OutboxItem> gOutbox;
kk::LessonBatch gLessonBatch;
kk::GlyphPack gPack;
WiFiServer gAssetServer(8080);
bool gAssetServerActive = false;
bool gLessonActive = false;
size_t gLessonIndex = 0;
size_t gLessonPage = 0;

void localHm(uint32_t epoch, char* out, size_t n);
void draw();
void drawLoading(const char* title, const char* subtitle);
void drawLesson();

lgfx::LovyanGFX& target() {
    if (gCanvas) return *gCanvas;
    return *gDisp;
}
void present() {
    if (gCanvas && gDisp) gCanvas->pushSprite(0, 0);
}

bool ensureCanvas() {
    if (gCanvas || !gDisp) return gCanvas != nullptr;
    for (int attempt = 0; attempt < 4; ++attempt) {
        gCanvas = new lgfx::LGFX_Sprite(gDisp);
        gCanvas->setColorDepth(lgfx::rgb332_1Byte);
        if (gCanvas->createSprite(gDisp->width(), gDisp->height())) return true;
        Serial.printf("[kk] canvas attempt %d failed, heap %u largest %u\n", attempt + 1,
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                             MALLOC_CAP_8BIT)));
        delete gCanvas;
        gCanvas = nullptr;
        if (attempt < 3) delay(120);
    }
    Serial.println("[kk] drawing directly");
    return false;
}

void releaseCanvas() {
    if (!gCanvas) return;
    delete gCanvas;
    gCanvas = nullptr;
}

// TLS needs one large block; give it the canvas only for the request and take
// it back before the response is parsed, so the frame buffer never fragments
// the heap while subjects are being decoded.
bool gFreeCanvasForRequest = false;
void requestBefore() {
    if (gFreeCanvasForRequest) releaseCanvas();
}
void requestAfter() {
    if (gFreeCanvasForRequest) ensureCanvas();
}

bool gShift = false;
bool gFn = false;
bool gCtrl = false;
bool gOpt = false;
bool gAlt = false;

struct Line {
    char text[44];
};
Line gLog[3];

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
    for (int i = 0; i < 2; ++i) gLog[i] = gLog[i + 1];
    snprintf(gLog[2].text, sizeof gLog[2].text, "%s", text);
}

void updateModifier(uint8_t x, uint8_t y, bool down) {
    switch (tgc::cardputerModifierAt(x, y)) {
        case tgc::Modifier::Shift: gShift = down; break;
        case tgc::Modifier::Fn: gFn = down; break;
        case tgc::Modifier::Ctrl: gCtrl = down; break;
        case tgc::Modifier::Opt: gOpt = down; break;
        case tgc::Modifier::Alt: gAlt = down; break;
        case tgc::Modifier::None: break;
    }
}

void runProbe() {
    const String token = kk::DevToken::load();
    snprintf(gNetLine, sizeof gNetLine, "API: probing...");
    gDirty = true;
    const kk::WkProbeResult r = kk::probeWaniKaniUser(token);
    if (r.parsed) {
        gUser = r.user;
        gHaveUser = true;
    }
    if (r.connected) {
        if (r.httpCode == 200 && r.parsed)
            snprintf(gNetLine, sizeof gNetLine, "API: 200 %s L%d", r.user.username.c_str(),
                     r.user.level);
        else
            snprintf(gNetLine, sizeof gNetLine, "API: %d  TLS %ums", r.httpCode,
                     static_cast<unsigned>(r.ms));
    } else {
        snprintf(gNetLine, sizeof gNetLine, "API: TLS failed (%d)", r.tlsError);
    }
    gDirty = true;
}

void runSummary() {
    const String token = kk::DevToken::load();
    const kk::SummaryProbeResult r = kk::probeWaniKaniSummary(token);
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    if (r.parsed) {
        gSummary = r.summary;
        gHaveSummary = true;
        gDueReviews = r.summary.dueReviews(now);
        gLessons = r.summary.totalLessons();
        gSyncedAt = now;
        localHm(now, gSyncClock, sizeof gSyncClock);
        const uint32_t nextAt = r.summary.nextReviewAt(now);
        if (nextAt)
            localHm(nextAt, gNextReview, sizeof gNextReview);
        else if (gDueReviews > 0)
            snprintf(gNextReview, sizeof gNextReview, "now");
        else
            snprintf(gNextReview, sizeof gNextReview, "all clear");
        gShowHome = true;
    }
    if (r.parsed)
        Serial.printf("[summary] due %u of %u reviews, %u lessons, next %s\n",
                      static_cast<unsigned>(r.summary.dueReviews(now)),
                      static_cast<unsigned>(r.summary.totalReviews()),
                      static_cast<unsigned>(r.summary.totalLessons()),
                      r.summary.nextReviewsAt.c_str());
    if (r.parsed)
        snprintf(gNetLine, sizeof gNetLine, "Summary: %ur %ul",
                 static_cast<unsigned>(r.summary.totalReviews()),
                 static_cast<unsigned>(r.summary.totalLessons()));
    else if (r.connected)
        snprintf(gNetLine, sizeof gNetLine, "Summary: HTTP %d", r.httpCode);
    else
        snprintf(gNetLine, sizeof gNetLine, "Summary: TLS failed (%d)", r.tlsError);
    gDirty = true;
}

bool runBatch() {
    if (!gHaveSummary) {
        Serial.println("[review] no summary yet");
        return false;
    }
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    const std::vector<uint32_t> ids = gSummary.dueSubjectIds(now, 20);
    Serial.printf("[review] %u due subject ids\n", static_cast<unsigned>(ids.size()));
    gFreeCanvasForRequest = true;
    kk::ReviewBatch batch = kk::fetchReviewBatch(kk::DevToken::load(), ids, now, 20);
    gFreeCanvasForRequest = false;
    if (!batch.ok) {
        Serial.printf("[review] batch failed: %s\n", batch.error.c_str());
        snprintf(gNetLine, sizeof gNetLine, "Batch: %s", batch.error.c_str());
        gBatch = kk::ReviewBatch{};
        gDirty = true;
        return false;
    }
    Serial.printf("[review] %u assignments, %u subjects\n",
                  static_cast<unsigned>(batch.assignments.size()),
                  static_cast<unsigned>(batch.subjects.size()));
    for (const kk::Subject& s : batch.subjects) {
        std::string meaning;
        for (const kk::Meaning& m : s.meanings)
            if (m.accepted) {
                meaning = m.text;
                break;
            }
        if (meaning.empty() && !s.meanings.empty()) meaning = s.meanings[0].text;
        std::string reading;
        for (const kk::Reading& r : s.readings)
            if (r.accepted && r.primary) {
                reading = r.text;
                break;
            }
        if (reading.empty())
            for (const kk::Reading& r : s.readings)
                if (r.accepted) {
                    reading = r.text;
                    break;
                }
        Serial.printf("[review] sub %u %s m=%s r=%s\n", static_cast<unsigned>(s.id),
                      s.characters.c_str(), meaning.c_str(), reading.c_str());
    }
    snprintf(gNetLine, sizeof gNetLine, "Batch: %ua %us",
             static_cast<unsigned>(batch.assignments.size()),
             static_cast<unsigned>(batch.subjects.size()));
    gBatch = std::move(batch);
    gDirty = true;
    return true;
}

const kk::Subject* subjectById(uint32_t id) {
    for (const kk::Subject& s : gBatch.subjects)
        if (s.id == id) return &s;
    return nullptr;
}

std::string correctAnswer(const kk::Subject& s, kk::ReviewPart part) {
    if (part == kk::ReviewPart::Reading) {
        for (const kk::Reading& r : s.readings)
            if (r.accepted) return r.text;
        if (!s.readings.empty()) return s.readings[0].text;
        return "-";
    }
    for (const kk::Meaning& m : s.meanings)
        if (m.accepted) return m.text;
    if (!s.meanings.empty()) return s.meanings[0].text;
    return "-";
}

void clearReviewInput() {
    gMeaningInput.clear();
    gDirectReading.clear();
    gIme.clear();
}

void drawPill(lgfx::LovyanGFX& d, int x, int y, const char* text, uint16_t fg, uint16_t bg) {
    d.setTextFont(0);
    d.setTextSize(1);
    const int w = d.textWidth(text) + 8;
    d.fillRect(x, y, w, 13, bg);
    d.setTextColor(fg, bg);
    d.drawString(text, x + 4, y + 2);
}

void drawWrapped(lgfx::LovyanGFX& d, const std::string& text, int x, int y, int w, int maxLines,
                 uint16_t fg, uint16_t bg) {
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(fg, bg);
    size_t start = 0;
    for (int line = 0; line < maxLines && start < text.size(); ++line) {
        size_t end = text.size();
        while (end > start && d.textWidth(text.substr(start, end - start).c_str()) > w) --end;
        if (end < text.size()) {
            const size_t sp = text.rfind(' ', end);
            if (sp != std::string::npos && sp > start) end = sp;
        }
        d.drawString(text.substr(start, end - start).c_str(), x, y + line * 8);
        start = end;
        while (start < text.size() && text[start] == ' ') ++start;
    }
}

void trimFrontToWidth(lgfx::LovyanGFX& d, std::string& text, int maxWidth) {
    while (text.size() > 1 && d.textWidth(text.c_str()) > maxWidth) {
        const char* p = text.c_str();
        kk::utf8Next(&p);
        const size_t n = static_cast<size_t>(p - text.c_str());
        if (n == 0 || n >= text.size()) break;
        text.erase(0, n);
    }
}

void drawReview() {
    if (!gDisp) return;
    const kk::SessionItem* item = gQueue.current();
    const kk::Subject* s = gFeedback ? subjectById(gFeedbackSubjectId)
                                     : (item ? subjectById(item->subjectId) : nullptr);
    if (!s) return;
    const kk::ReviewPart part = gFeedback ? gFeedbackPart : gQueue.part();

    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(kDim, kBg);
    d.drawString("REVIEWS", 6, 3);
    const int total = static_cast<int>(gQueue.completedCount() + gQueue.remaining());
    int pos = static_cast<int>(gQueue.completedCount()) + 1;
    if (gFeedback && gFeedbackCompleted) pos = static_cast<int>(gQueue.completedCount());
    if (pos > total) pos = total;
    char right[24];
    snprintf(right, sizeof right, "%d/%d", pos, total);
    d.setTextFont(0);
    d.setTextDatum(lgfx::top_right);
    d.drawString(right, 233, 5);
    d.setTextDatum(lgfx::top_left);
    d.fillRect(0, 14, 240, 2, kSurfaceHi);
    if (total > 0) d.fillRect(0, 14, 240 * (pos - 1) / total, 2, kAccentHi);

    d.fillRect(4, 20, 88, 88, kSurface);
    d.drawRect(4, 20, 88, 88, kLine);
    if (!gPack.drawTextCentered(d, s->characters, 64, 48, 56, 78, 80, kText, kSurface,
                                kk::ClipRect{6, 22, 90, 106})) {
        d.setFont(&fonts::efontJA_16);
        d.setTextSize(1);
        const int base = d.textWidth(s->characters.c_str());
        int scale = 3;
        while (scale > 1 && base * scale > 80) --scale;
        d.setTextSize(scale);
        d.setTextDatum(lgfx::middle_center);
        d.setTextColor(kText, kSurface);
        d.drawString(s->characters.c_str(), 48, 56);
        d.setTextDatum(lgfx::top_left);
    }
    char type[24];
    const char* kindName = s->type == kk::SubjectType::Vocabulary ? "VOCAB"
                           : s->type == kk::SubjectType::Radical ? "RADICAL"
                                                                  : "KANJI";
    snprintf(type, sizeof type, "%s L%d", kindName, s->level);
    d.setTextFont(0);
    d.setTextSize(1);
    drawPill(d, 48 - (d.textWidth(type) + 8) / 2, 92, type, kDim, kSurfaceHi);

    if (!gFeedback) {
        const bool meaning = part == kk::ReviewPart::Meaning;
        drawPill(d, 100, 20, meaning ? "MEANING" : "READING",
                 meaning ? kAccentHi : kReading, kSurfaceHi);
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(kDim, kBg);
        d.drawString(meaning ? "Type the meaning" : "Type the reading", 100, 38);

        d.fillRect(100, 52, 136, 32, kSurfaceHi);
        d.drawRect(100, 52, 136, 32, kLine);
        std::string shown = meaning ? gMeaningInput
                                     : (!gDirectReading.empty() ? gDirectReading : gIme.text());
        int caret = 106;
        if (shown.empty()) {
            d.setTextFont(1);
            d.setTextSize(1);
            d.setTextColor(kFaint, kSurfaceHi);
            d.drawString(meaning ? "answer" : "kana", 106, 60);
        } else if (!meaning &&
                   gPack.drawTextRight(d, shown, 230, 80, 32, kText, kSurfaceHi,
                                       kk::ClipRect{102, 54, 234, 86})) {
            caret = 230;
        } else {
            if (meaning) {
                d.setTextFont(1);
                d.setTextSize(1);
            } else {
                d.setFont(&fonts::efontJA_16);
                d.setTextSize(1);
            }
            trimFrontToWidth(d, shown, 128);
            d.setTextColor(kText, kSurfaceHi);
            d.drawString(shown.c_str(), 106, 60);
            caret = 106 + d.textWidth(shown.c_str());
        }
        if (caret < 232) d.fillRect(caret, 58, 2, 16, kAccent);

        if (!meaning) {
            d.setTextFont(0);
            d.setTextSize(1);
            d.setTextColor(kFaint, kBg);
            std::string raw = "romaji: " + gIme.raw();
            d.drawString(raw.c_str(), 100, 88);
        }
    } else {
        const bool ok = gCheck.status == kk::AnswerStatus::Correct;
        const bool warn = gCheck.status == kk::AnswerStatus::Warning;
        drawPill(d, 100, 20, ok ? "CORRECT" : warn ? "CHECK" : "NOT QUITE",
                 ok ? kOk : warn ? kWarn : kErr, ok ? kOkBg : warn ? kSurfaceHi : kErrBg);
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(kFaint, kBg);
        d.drawString("You typed", 100, 38);
        const bool japanese = part == kk::ReviewPart::Reading;
        std::string typed = gFeedbackInput;
        bool typedDrawn = false;
        if (japanese && !typed.empty())
            typedDrawn = gPack.drawTextFit(d, typed, 100, 64, 20, 136, kDim, kBg,
                                           kk::ClipRect{100, 44, 236, 68}) != 0;
        if (!typedDrawn) {
            if (japanese) {
                d.setFont(&fonts::efontJA_16);
            } else {
                d.setTextFont(1);
            }
            d.setTextSize(1);
            d.setTextColor(kDim, kBg);
            if (japanese) {
                trimFrontToWidth(d, typed, 136);
            } else {
                while (typed.size() > 1 && d.textWidth(typed.c_str()) > 136)
                    typed.erase(0, 1);
            }
            d.drawString(typed.empty() ? "-" : typed.c_str(), 100, 47);
        }
        d.setTextFont(0);
        d.setTextSize(1);
        d.setTextColor(kFaint, kBg);
        d.drawString("Answer", 100, 65);
        std::string answer = correctAnswer(*s, part);
        bool answerDrawn = false;
        if (japanese)
            answerDrawn = gPack.drawTextFit(d, answer, 100, 90, 20, 136,
                                            ok ? kOk : warn ? kWarn : kErr, kBg,
                                            kk::ClipRect{100, 70, 236, 94}) != 0;
        if (!answerDrawn) {
            if (japanese) {
                d.setFont(&fonts::efontJA_16);
            } else {
                d.setTextFont(1);
            }
            d.setTextSize(1);
            d.setTextColor(ok ? kOk : warn ? kWarn : kErr, kBg);
            if (japanese) {
                trimFrontToWidth(d, answer, 136);
            } else {
                if (d.textWidth(answer.c_str()) > 136) d.setTextFont(0);
                while (answer.size() > 1 && d.textWidth(answer.c_str()) > 136) answer.pop_back();
                if (answer.size() < correctAnswer(*s, part).size()) answer += "...";
            }
            d.drawString(answer.c_str(), 100, 74);
        }
        if (gCheck.status != kk::AnswerStatus::Correct)
            drawWrapped(d, gCheck.message, 100, 92, 136, 2, kFaint, kBg);
    }

    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(kFaint, kBg);
    d.drawString(gFeedback ? "Enter: next" : "Enter: check", 6, 125);
    d.setTextDatum(lgfx::top_right);
    d.drawString("Esc: quit", 233, 125);
    d.setTextDatum(lgfx::top_left);
    d.endWrite();
    present();
}

void drawReviewSummary() {
    if (!gDisp) return;
    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(kDim, kBg);
    d.drawString("SESSION", 6, 3);
    d.setTextFont(2);
    d.setTextSize(2);
    d.setTextColor(kText, kBg);
    char n[20];
    snprintf(n, sizeof n, "%u", static_cast<unsigned>(gQueue.completedCount()));
    d.drawString(n, 8, 30);
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(kDim, kBg);
    d.drawString("REVIEWS", 8, 66);
    unsigned mistakes = 0;
    for (const kk::SessionItem& it : gQueue.completed())
        mistakes += it.incorrectMeaning + it.incorrectReading;
    char m[40];
    snprintf(m, sizeof m, "%u mistakes", mistakes);
    d.drawString(m, 8, 84);
    const size_t pending = kk::outboxPendingCount(gOutbox);
    const size_t rejected = kk::outboxRejectedCount(gOutbox);
    if (rejected)
        snprintf(m, sizeof m, "%u need attention", static_cast<unsigned>(rejected));
    else if (pending)
        snprintf(m, sizeof m, "%u pending", static_cast<unsigned>(pending));
    else
        snprintf(m, sizeof m, "All reviews sent");
    d.setTextColor(rejected ? kErr : pending ? kWarn : kFaint, kBg);
    d.drawString(m, 8, 98);
    d.setTextColor(kFaint, kBg);
    d.drawString("Enter: home", 8, 125);
    d.endWrite();
    present();
}

void saveOutbox() {
    if (!kk::outboxSave(gOutbox)) Serial.println("[outbox] save failed");
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
            kk::outboxRemove(gOutbox, item.seq);
            saveOutbox();
            Serial.printf("[outbox] %u was already accepted\n",
                          static_cast<unsigned>(item.assignmentId));
            return true;
        }
        break;
    }
    return false;
}

bool sendOutboxItem(uint32_t seq) {
    kk::OutboxItem* item = kk::outboxFind(gOutbox, seq);
    if (!item) return true;
    if (item->state == kk::OutboxState::Rejected) return false;
    if (item->state == kk::OutboxState::Sending ||
        item->state == kk::OutboxState::Ambiguous) {
        if (reconcileOutboxItem(*item)) return true;
        item = kk::outboxFind(gOutbox, seq);
        if (!item) return true;
        item->state = kk::OutboxState::Pending;
        saveOutbox();
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

    gFreeCanvasForRequest = true;
    const kk::WkResponse r = kk::wkPost("/v2/reviews", kk::DevToken::load(), body, 4096);
    gFreeCanvasForRequest = false;

    item = kk::outboxFind(gOutbox, seq);
    if (!item) return true;
    if (r.httpCode >= 200 && r.httpCode < 300) {
        kk::outboxRemove(gOutbox, seq);
        saveOutbox();
        Serial.printf("[outbox] sent assignment %u (%d)\n",
                      static_cast<unsigned>(item->assignmentId), r.httpCode);
        return true;
    }
    if (!r.connected)
        item->state = kk::OutboxState::Pending;  // never reached the server
    else if (r.httpCode == 0 || r.httpCode >= 500)
        item->state = kk::OutboxState::Ambiguous;  // may have been accepted
    else if (r.httpCode == 401 || r.httpCode == 403 || r.httpCode == 429)
        item->state = kk::OutboxState::Pending;
    else
        item->state = kk::OutboxState::Rejected;
    saveOutbox();
    Serial.printf("[outbox] assignment %u -> HTTP %d, state %u\n",
                  static_cast<unsigned>(item->assignmentId), r.httpCode,
                  static_cast<unsigned>(item->state));
    return false;
}

void queueReview(const kk::SessionItem& item) {
    uint32_t created = static_cast<uint32_t>(time(nullptr));
    if (created < 1700000000u) created = 0;  // clock not trusted; let the server stamp it
    const uint32_t seq = kk::outboxAdd(gOutbox, item.assignmentId, item.subjectId,
                                       item.incorrectMeaning, item.incorrectReading, created);
    if (!seq) {
        Serial.println("[outbox] full; review not queued");
        return;
    }
    saveOutbox();
    Serial.printf("[outbox] queued seq %u assignment %u\n", static_cast<unsigned>(seq),
                  static_cast<unsigned>(item.assignmentId));
    sendOutboxItem(seq);
}

void flushOutbox() {
    if (gOutbox.empty()) return;
    Serial.printf("[outbox] flushing %u\n", static_cast<unsigned>(gOutbox.size()));
    std::vector<uint32_t> seqs;
    for (const kk::OutboxItem& it : gOutbox)
        if (it.state != kk::OutboxState::Rejected) seqs.push_back(it.seq);
    for (uint32_t seq : seqs) {
        if (!sendOutboxItem(seq)) {
            const kk::OutboxItem* it = kk::outboxFind(gOutbox, seq);
            if (!it || it->state == kk::OutboxState::Rejected) continue;
            break;
        }
    }
    Serial.printf("[outbox] %u pending, %u rejected\n",
                  static_cast<unsigned>(kk::outboxPendingCount(gOutbox)),
                  static_cast<unsigned>(kk::outboxRejectedCount(gOutbox)));
}

void checkCurrent() {
    const kk::SessionItem* item = gQueue.current();
    if (!item) return;
    const kk::Subject* s = subjectById(item->subjectId);
    if (!s) return;
    const bool meaning = gQueue.part() == kk::ReviewPart::Meaning;
    const std::string input = meaning ? gMeaningInput
                                     : (!gDirectReading.empty() ? gDirectReading : gIme.text());
    if (input.empty()) return;
    gFeedbackSubjectId = item->subjectId;
    gFeedbackPart = gQueue.part();
    gFeedbackInput = input;
    gCheck = kk::checkAnswer(*s, meaning ? kk::AnswerKind::Meaning : kk::AnswerKind::Reading,
                             input, true);
    gFeedback = true;
    gFeedbackCompleted = false;
    if (gCheck.status != kk::AnswerStatus::Warning) {
        const kk::AnswerOutcome o = gQueue.answer(gCheck.status);
        if (o.completed) {
            gCompletedItem = gQueue.completed().back();
            gNeedsSubmit = true;
            gFeedbackCompleted = true;
            if (gDueReviews > 0) --gDueReviews;
        }
    }
    gDirty = true;
}

void advanceFeedback() {
    if (gCheck.status == kk::AnswerStatus::Warning) {
        clearReviewInput();
        gFeedback = false;
        gDirty = true;
        return;
    }
    if (gNeedsSubmit) {
        queueReview(gCompletedItem);
        gNeedsSubmit = false;
    }
    gFeedback = false;
    gFeedbackCompleted = false;
    clearReviewInput();
    if (gQueue.empty()) {
        gReviewActive = false;
        gReviewSummary = true;
    }
    gDirty = true;
}

void endReview() {
    if (gNeedsSubmit) {
        queueReview(gCompletedItem);
        gNeedsSubmit = false;
    }
    gReviewActive = false;
    gReviewSummary = false;
    gShowHome = true;
    gFeedback = false;
    gFeedbackCompleted = false;
    gBatch = kk::ReviewBatch{};
    gQueue = kk::ReviewQueue{};
    gDirty = true;
}

void startReview() {
    if (!gHaveSummary) return;
    if (gBatch.assignments.empty()) {
        drawLoading("Fetching reviews...", "20 due items, please wait");
        flushOutbox();
        if (!runBatch()) {
            drawLoading("Could not load reviews", gNetLine);
            return;
        }
    }
    drawLoading("Starting session...", "");
    std::vector<kk::Assignment> due;
    for (const kk::Assignment& a : gBatch.assignments) {
        const kk::Subject* s = subjectById(a.subjectId);
        if (s && !s->characters.empty()) due.push_back(a);
    }
    gQueue.build(due, gBatch.subjects, static_cast<uint32_t>(time(nullptr)), 20);
    if (gQueue.empty()) {
        Serial.println("[review] no reviewable items");
        return;
    }
    gReviewActive = true;
    gReviewSummary = false;
    gShowHome = false;
    gFeedback = false;
    gFeedbackCompleted = false;
    gNeedsSubmit = false;
    clearReviewInput();
    Serial.printf("[review] session with %u items\n", static_cast<unsigned>(gQueue.remaining()));
    for (size_t i = 0; i < gQueue.queue().size() && i < 5; ++i) {
        const kk::SessionItem& it = gQueue.queue()[i];
        const kk::Subject* s = subjectById(it.subjectId);
        Serial.printf("[review] queue %u %s\n", static_cast<unsigned>(it.subjectId),
                      s ? s->characters.c_str() : "?");
    }
    gDirty = true;
}

bool reviewKey(const KeyEvent& e) {
    if (!e.down) return false;
    if (gFeedback) {
        if (e.key == Key::Enter) advanceFeedback();
        else if (e.key == Key::Esc) endReview();
        return true;
    }
    if (e.key == Key::Enter) {
        checkCurrent();
        return true;
    }
    if (e.key == Key::Esc) {
        endReview();
        return true;
    }
    if (e.key == Key::Backspace) {
        if (gQueue.part() == kk::ReviewPart::Meaning) {
            if (!gMeaningInput.empty()) gMeaningInput.pop_back();
        } else if (!gDirectReading.empty()) {
            gDirectReading.pop_back();
        } else {
            gIme.backspace();
        }
        gDirty = true;
        return true;
    }
    const char c = tgc::cardputerCharAt(e.x, e.y, gShift);
    if (c) {
        if (gQueue.part() == kk::ReviewPart::Meaning) {
            gMeaningInput.push_back(c);
        } else {
            gDirectReading.clear();
            gIme.feed(c);
        }
        gDirty = true;
    }
    return true;
}

const kk::Subject* lessonSubjectById(uint32_t id) {
    for (const kk::Subject& s : gLessonBatch.subjects)
        if (s.id == id) return &s;
    return nullptr;
}

const kk::Subject* currentLessonSubject() {
    if (!gLessonActive || gLessonIndex >= gLessonBatch.assignments.size()) return nullptr;
    return lessonSubjectById(gLessonBatch.assignments[gLessonIndex].subjectId);
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

void drawLesson() {
    if (!gDisp) return;
    const kk::Subject* s = currentLessonSubject();
    if (!s) return;
    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(kDim, kBg);
    d.drawString("LESSON", 6, 3);
    d.setTextFont(0);
    d.setTextDatum(lgfx::top_right);
    char pos[20];
    snprintf(pos, sizeof pos, "%u/%u", static_cast<unsigned>(gLessonIndex + 1),
             static_cast<unsigned>(gLessonBatch.assignments.size()));
    d.drawString(pos, 233, 5);
    d.setTextDatum(lgfx::top_left);

    d.fillRect(4, 20, 74, 64, kSurface);
    d.drawRect(4, 20, 74, 64, kLine);
    if (!gPack.drawTextCentered(d, s->characters, 64, 41, 52, 70, 62, kText, kSurface,
                                kk::ClipRect{6, 22, 76, 82})) {
        d.setFont(&fonts::efontJA_16);
        d.setTextSize(1);
        const int base = d.textWidth(s->characters.c_str());
        int scale = 3;
        while (scale > 1 && base * scale > 66) --scale;
        d.setTextSize(scale);
        d.setTextDatum(lgfx::middle_center);
        d.setTextColor(kText, kSurface);
        d.drawString(s->characters.c_str(), 41, 52);
        d.setTextDatum(lgfx::top_left);
    }

    const char* kind = s->type == kk::SubjectType::Vocabulary ? "VOCAB"
                       : s->type == kk::SubjectType::Radical ? "RADICAL"
                                                              : "KANJI";
    char type[24];
    snprintf(type, sizeof type, "%s L%d", kind, s->level);
    d.setTextFont(0);
    d.setTextSize(1);
    drawPill(d, 86, 20, type, kDim, kSurfaceHi);

    std::string meaning;
    for (const kk::Meaning& m : s->meanings)
        if (m.accepted) {
            meaning = m.text;
            break;
        }
    if (meaning.empty() && !s->meanings.empty()) meaning = s->meanings[0].text;
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(kText, kBg);
    while (meaning.size() > 1 && d.textWidth(meaning.c_str()) > 148) meaning.pop_back();
    d.drawString(meaning.c_str(), 86, 38);

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
    if (!reading.empty()) {
        if (!gPack.drawTextFit(d, reading, 86, 80, 32, 148, kReading, kBg,
                               kk::ClipRect{86, 50, 236, 88})) {
            d.setFont(&fonts::efontJA_16);
            d.setTextSize(1);
            d.setTextColor(kReading, kBg);
            d.drawString(reading.c_str(), 86, 58);
        }
    }

    std::string mnemonic = s->meaningMnemonic;
    if (mnemonic.empty()) mnemonic = s->readingMnemonic;
    if (mnemonic.empty()) mnemonic = s->meaningHint;
    if (mnemonic.empty()) mnemonic = "No mnemonic cached.";
    const std::vector<std::string> lines = wrapLines(d, mnemonic, 232);
    const size_t pages = (lines.size() + 2) / 3;
    if (gLessonPage >= pages) gLessonPage = pages - 1;
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(kDim, kBg);
    for (size_t i = 0; i < 3; ++i) {
        const size_t idx = gLessonPage * 3 + i;
        if (idx >= lines.size()) break;
        d.drawString(lines[idx].c_str(), 5, 92 + static_cast<int>(i) * 9);
    }
    char page[20];
    snprintf(page, sizeof page, "p%u/%u", static_cast<unsigned>(gLessonPage + 1),
             static_cast<unsigned>(pages));
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(kFaint, kBg);
    d.drawString(page, 233, 117);
    d.setTextDatum(lgfx::top_left);

    d.setTextColor(kFaint, kBg);
    d.drawString("Space: page  Enter: start  Esc: home", 5, 125);
    d.endWrite();
    present();
}

void startLessons() {
    if (!gHaveSummary) return;
    drawLoading("Fetching lessons...", "5 at a time, please wait");
    gFreeCanvasForRequest = true;
    gLessonBatch = kk::fetchLessonBatch(kk::DevToken::load(), gSummary.lessonSubjectIds(5), 5);
    gFreeCanvasForRequest = false;
    if (!gLessonBatch.ok) {
        Serial.printf("[lesson] failed: %s\n", gLessonBatch.error.c_str());
        drawLoading("No lessons available", gLessonBatch.error.c_str());
        gShowHome = true;
        gDirty = true;
        return;
    }
    gLessonActive = true;
    gShowHome = false;
    gLessonIndex = 0;
    gLessonPage = 0;
    Serial.printf("[lesson] %u assignments, %u subjects\n",
                  static_cast<unsigned>(gLessonBatch.assignments.size()),
                  static_cast<unsigned>(gLessonBatch.subjects.size()));
    gDirty = true;
}

void startCurrentLesson() {
    if (!gLessonActive || gLessonIndex >= gLessonBatch.assignments.size()) return;
    const kk::Assignment& a = gLessonBatch.assignments[gLessonIndex];
    const kk::Subject* s = lessonSubjectById(a.subjectId);
    drawLoading("Starting lesson...", s ? s->characters.c_str() : "");
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    gFreeCanvasForRequest = true;
    const bool ok = kk::startAssignment(kk::DevToken::load(), a.id,
                                        now >= 1700000000u ? now : 0);
    gFreeCanvasForRequest = false;
    if (ok) {
        if (gLessons > 0) --gLessons;
        ++gLessonIndex;
        gLessonPage = 0;
        if (gLessonIndex >= gLessonBatch.assignments.size()) {
            gLessonActive = false;
            gShowHome = true;
        }
    }
    gDirty = true;
}

void lessonKey(const KeyEvent& e) {
    if (!e.down) return;
    if (e.key == Key::Enter) {
        startCurrentLesson();
        return;
    }
    if (e.key == Key::Esc) {
        gLessonActive = false;
        gShowHome = true;
        gDirty = true;
        return;
    }
    if (e.key == Key::Right || (e.key == Key::Other && tgc::cardputerCharAt(e.x, e.y, false) == ' ')) {
        ++gLessonPage;
        gDirty = true;
    } else if (e.key == Key::Left || e.key == Key::Backspace) {
        if (gLessonPage > 0) --gLessonPage;
        gDirty = true;
    }
}

void onKey(const KeyEvent& e) {
    updateModifier(e.x, e.y, e.down);
    if (gLessonActive) {
        lessonKey(e);
        return;
    }
    if (gReviewActive) {
        reviewKey(e);
        return;
    }
    if (gReviewSummary) {
        if (e.down && (e.key == Key::Enter || e.key == Key::Esc)) endReview();
        return;
    }
    if (e.down && e.key == Key::Enter && gShowHome && gHaveSummary) {
        startReview();
        return;
    }
    if (e.down && gShowHome && e.key == Key::Other &&
        (tgc::cardputerCharAt(e.x, e.y, gShift) == 'l' ||
         tgc::cardputerCharAt(e.x, e.y, gShift) == 'L')) {
        startLessons();
        return;
    }
    if (e.down && e.key == Key::Tab && gHaveSummary) {
        gShowHome = !gShowHome;
        gDirty = true;
        return;
    }
    const char c = tgc::cardputerCharAt(e.x, e.y, gShift);

    char line[44];
    if (c) {
        snprintf(line, sizeof line, "%s '%c'  code %u  %u,%u", e.down ? "v" : "^", c,
                 static_cast<unsigned>(e.code), static_cast<unsigned>(e.x),
                 static_cast<unsigned>(e.y));
    } else {
        snprintf(line, sizeof line, "%s %-9s code %u  %u,%u", e.down ? "v" : "^",
                 keyName(e.key), static_cast<unsigned>(e.code),
                 static_cast<unsigned>(e.x), static_cast<unsigned>(e.y));
    }
    pushLog(line);
    gDirty = true;

    char cs[2] = {c, 0};
    Serial.printf("[key] %s code=%u grid=%u,%u key=%s char=%s shift=%d fn=%d ctrl=%d opt=%d "
                  "alt=%d\n",
                  e.down ? "down" : "up", static_cast<unsigned>(e.code),
                  static_cast<unsigned>(e.x), static_cast<unsigned>(e.y), keyName(e.key),
                  c ? cs : "-", gShift, gFn, gCtrl, gOpt, gAlt);
}

bool mountSd() {
    tgc::board::SdPins pins;
    if (!tgc::board::sdPins(&pins)) {
        snprintf(gSdLine, sizeof gSdLine, "SD: no slot");
        return false;
    }
    if (pins.otherCs >= 0) {
        pinMode(pins.otherCs, OUTPUT);
        digitalWrite(pins.otherCs, HIGH);
    }
    static SPIClass spi(HSPI);
    spi.begin(pins.sck, pins.miso, pins.mosi, pins.cs);
    const size_t heapBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const uint32_t t0 = millis();
    if (!SD.begin(pins.cs, spi, 20000000, "/sd", 2)) {
        snprintf(gSdLine, sizeof gSdLine, "SD: no card / not FAT32");
        Serial.printf("[sd] mount failed in %u ms, heap %u -> %u\n",
                      static_cast<unsigned>(millis() - t0),
                      static_cast<unsigned>(heapBefore),
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
        return false;
    }
    const uint64_t mb = SD.cardSize() / (1024ull * 1024ull);
    snprintf(gSdLine, sizeof gSdLine, "SD: %llu MB", static_cast<unsigned long long>(mb));
    Serial.printf("[sd] mounted in %u ms, %llu MB, heap %u -> %u, largest %u\n",
                  static_cast<unsigned>(millis() - t0), static_cast<unsigned long long>(mb),
                  static_cast<unsigned>(heapBefore),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                         MALLOC_CAP_8BIT)));
    return true;
}

void localHm(uint32_t epoch, char* out, size_t n) {
    const time_t t = static_cast<time_t>(epoch);
    struct tm tm {};
    if (!localtime_r(&t, &tm)) {
        snprintf(out, n, "--:--");
        return;
    }
    snprintf(out, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

void drawLoading(const char* title, const char* subtitle) {
    if (!gDisp) return;
    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);
    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(kBrand, kBg);
    d.drawString("KaniKey", 6, 2);
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(kText, kBg);
    d.drawString(title, 8, 56);
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(kDim, kBg);
    d.drawString(subtitle, 8, 76);
    d.setTextColor(kFaint, kBg);
    d.drawString("The first request can take a few seconds", 8, 98);
    d.endWrite();
    present();
}

void drawHome() {
    if (!gDisp) return;
    static bool logged = false;
    if (!logged) {
        logged = true;
        Serial.println("[kk] home drawn");
    }
    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);
    d.setTextDatum(lgfx::top_left);
    d.setTextWrap(false);

    d.setTextFont(2);
    d.setTextSize(1);
    d.setTextColor(kBrand, kBg);
    d.drawString("KaniKey", 6, 2);

    const int bat = tgc::board::batteryPercent();
    char right[20];
    if (bat >= 0)
        snprintf(right, sizeof right, "%d%%", bat);
    else
        snprintf(right, sizeof right, "USB");
    d.setTextFont(1);
    d.setTextColor(bat >= 0 && bat < 20 ? kWarn : kDim, kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString(right, 233, 5);

    d.setTextDatum(lgfx::top_left);
    d.setTextColor(GOLD, kBg);
    char lvl[16];
    snprintf(lvl, sizeof lvl, "LEVEL %d", gHaveUser ? gUser.level : 0);
    d.drawString(lvl, 7, 21);
    d.setTextColor(kDim, kBg);
    d.drawString(gHaveUser ? gUser.username.c_str() : "not signed in", 80, 21);

    d.setTextFont(2);
    d.setTextSize(2);
    d.setTextColor(kText, kBg);
    char due[16];
    snprintf(due, sizeof due, "%u", static_cast<unsigned>(gDueReviews));
    d.drawString(due, 8, 36);
    char lessons[16];
    snprintf(lessons, sizeof lessons, "%u", static_cast<unsigned>(gLessons));
    d.drawString(lessons, 164, 36);

    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(kDim, kBg);
    d.drawString("REVIEWS DUE", 8, 73);
    d.drawString("LESSONS", 164, 73);

    d.drawFastHLine(4, 85, 232, kLine);
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(kDim, kBg);
    d.drawString("Next review", 8, 91);
    d.drawString("Synced", 8, 105);
    d.setTextDatum(lgfx::top_right);
    d.setTextColor(kText, kBg);
    d.drawString(gNextReview, 232, 91);
    d.setTextColor(kFaint, kBg);
    d.drawString(gSyncClock, 232, 105);
    d.setTextDatum(lgfx::top_left);

    d.setTextFont(0);
    d.setTextSize(1);
    const size_t pending = kk::outboxPendingCount(gOutbox);
    const size_t rejected = kk::outboxRejectedCount(gOutbox);
    if (rejected) {
        char line[40];
        snprintf(line, sizeof line, "Outbox: %u need attention",
                 static_cast<unsigned>(rejected));
        d.setTextColor(kErr, kBg);
        d.drawString(line, 8, 117);
    } else if (pending) {
        char line[40];
        snprintf(line, sizeof line, "Outbox: %u pending", static_cast<unsigned>(pending));
        d.setTextColor(kWarn, kBg);
        d.drawString(line, 8, 117);
    }
    d.setTextColor(kFaint, kBg);
    d.drawString("Enter: reviews  L: lessons  Tab: debug", 8, 125);
    d.endWrite();
    present();
}

void draw() {
    if (gReviewActive) {
        drawReview();
        return;
    }
    if (gReviewSummary) {
        drawReviewSummary();
        return;
    }
    if (gLessonActive) {
        drawLesson();
        return;
    }
    if (gShowHome) {
        drawHome();
        return;
    }
    if (!gDisp) return;
    auto& d = target();
    d.startWrite();
    d.fillScreen(kBg);

    d.setTextDatum(top_left);
    d.setTextWrap(false);
    d.setTextFont(2);
    d.setTextSize(2);
    d.setTextColor(kBrand, kBg);
    d.drawString("KaniKey", 6, 2);

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(kDim, kBg);
    d.drawString("unofficial WaniKani companion", 7, 38);

    const int bat = tgc::board::batteryPercent();
    char right[24];
    if (bat >= 0)
        snprintf(right, sizeof right, "%d%%", bat);
    else
        snprintf(right, sizeof right, "USB");
    d.setTextColor(bat >= 0 && bat < 20 ? kWarn : kDim, kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString(right, 233, 7);
    d.setTextDatum(lgfx::top_left);

    d.setTextColor(kText, kBg);
    d.setTextFont(1);
    d.drawString("M0 hardware check", 7, 54);
    d.setTextColor(kFaint, kBg);
    d.drawString(gNetLine, 7, 66);

    d.fillRect(4, 78, 232, 34, kSurface);
    d.drawRect(4, 78, 232, 34, kLine);
    d.setTextFont(1);
    for (int i = 0; i < 3; ++i) {
        d.setTextColor(i == 2 ? kText : kFaint, kSurface);
        d.drawString(gLog[i].text, 9, 81 + i * 10);
    }

    d.setTextFont(0);
    d.setTextColor(gSdOk ? kOk : kWarn, kBg);
    d.drawString(gSdLine, 5, 117);
    char heap[40];
    snprintf(heap, sizeof heap, "heap %u  largest %u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                     MALLOC_CAP_8BIT)));
    d.setTextColor(kFaint, kBg);
    d.drawString(heap, 5, 126);

    char mods[24] = "";
    if (gShift) strncat(mods, "S", sizeof mods - strlen(mods) - 1);
    if (gFn) strncat(mods, "F", sizeof mods - strlen(mods) - 1);
    if (gCtrl) strncat(mods, "C", sizeof mods - strlen(mods) - 1);
    if (gOpt) strncat(mods, "O", sizeof mods - strlen(mods) - 1);
    if (gAlt) strncat(mods, "A", sizeof mods - strlen(mods) - 1);
    if (mods[0]) {
        d.setTextColor(kDim, kBg);
        d.setTextDatum(lgfx::top_right);
        d.drawString(mods, 233, 126);
        d.setTextDatum(lgfx::top_left);
    }

    if (!gPack.drawTextCentered(d, "食", 32, 216, 100, 42, 42, kText, kBg,
                                kk::ClipRect{190, 82, 238, 118})) {
        d.setFont(&fonts::efontJA_16);
        d.setTextColor(kText, kBg);
        d.drawString("食", 200, 100);
    }
    d.endWrite();
    present();
}

void drawStatus() {
    if (!gDisp || gShowHome || gReviewActive || gReviewSummary || gLessonActive) return;
    // The status line changes on its own (battery/heap), so redraw just it.
    auto& d = target();
    d.startWrite();
    d.setTextDatum(top_left);
    d.setTextFont(0);
    d.setTextColor(gSdOk ? kOk : kWarn, kBg);
    d.fillRect(5, 117, 230, 8, kBg);
    d.drawString(gSdLine, 5, 117);
    char heap[40];
    snprintf(heap, sizeof heap, "heap %u  largest %u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                     MALLOC_CAP_8BIT)));
    d.fillRect(5, 126, 230, 8, kBg);
    d.setTextColor(kFaint, kBg);
    d.drawString(heap, 5, 126);
    d.fillRect(5, 66, 230, 12, kBg);
    d.setTextFont(1);
    d.setTextColor(kFaint, kBg);
    d.drawString(gNetLine, 7, 66);
    const int bat = tgc::board::batteryPercent();
    char right[24];
    if (bat >= 0)
        snprintf(right, sizeof right, "%d%%", bat);
    else
        snprintf(right, sizeof right, "USB");
    d.fillRect(206, 7, 28, 12, kBg);
    d.setTextFont(1);
    d.setTextColor(bat >= 0 && bat < 20 ? kWarn : kDim, kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString(right, 233, 7);
    d.setTextDatum(lgfx::top_left);
    d.endWrite();
}

void wakeScreen() {
    if (!gDimmed && !gScreenOff) return;
    tgc::board::setDisplayOn(true);
    tgc::board::setBrightness(128);
    gDimmed = false;
    gScreenOff = false;
    gDirty = true;
}

void dumpCanvas() {
    if (!gCanvas) {
        Serial.println("#shot 0 0 none");
        return;
    }
    static const char kHex[] = "0123456789abcdef";
    static char line[16 + 4 * 240 + 2];
    const int w = gCanvas->width();
    const int h = gCanvas->height();
    // The sprite buffer is RGB332 (one byte per pixel); dump it raw.
    const bool rgb332 = gCanvas->getColorDepth() == lgfx::rgb332_1Byte;
    const uint8_t* px = static_cast<const uint8_t*>(gCanvas->getBuffer());
    Serial.printf("\n#shot %d %d %s\n", w, h, rgb332 ? "rgb332" : "rgb565");
    for (int y = 0; y < h; ++y) {
        int n = snprintf(line, sizeof line, "#r %d ", y);
        for (int x = 0; x < w; ++x) {
            if (rgb332) {
                const uint8_t v = px[static_cast<size_t>(y) * w + x];
                line[n++] = kHex[v >> 4];
                line[n++] = kHex[v & 15];
            } else {
                const uint8_t* p = px + (static_cast<size_t>(y) * w + x) * 2;
                line[n++] = kHex[p[0] >> 4];
                line[n++] = kHex[p[0] & 15];
                line[n++] = kHex[p[1] >> 4];
                line[n++] = kHex[p[1] & 15];
            }
        }
        line[n++] = '\n';
        Serial.write(reinterpret_cast<const uint8_t*>(line), n);
    }
    Serial.print("#end\n");
}

void assetRespond(WiFiClient& client, int code, const char* reason) {
    client.printf("HTTP/1.1 %d %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", code,
                  reason);
    client.flush();
    delay(20);
    client.stop();
}

void stopAssetServer() {
    gAssetServerActive = false;
    gAssetServer.end();
    gDirty = true;
}

void serviceAssetServer() {
    if (!gAssetServerActive) return;
    WiFiClient client = gAssetServer.available();
    if (!client) return;
    client.setTimeout(10);
    client.setNoDelay(true);

    String request = client.readStringUntil('\n');
    request.trim();
    Serial.printf("[asset] request %s\n", request.c_str());
    const int sp1 = request.indexOf(' ');
    const int sp2 = sp1 < 0 ? -1 : request.indexOf(' ', sp1 + 1);
    if (sp1 < 0 || sp2 < 0) {
        assetRespond(client, 400, "Bad Request");
        stopAssetServer();
        return;
    }
    const String method = request.substring(0, sp1);
    const String path = request.substring(sp1 + 1, sp2);
    int contentLength = -1;
    const uint32_t headerStart = millis();
    while (millis() - headerStart < 10000) {
        String line = client.readStringUntil('\n');
        if (line == "\r" || line.isEmpty()) break;
        if (line.startsWith("Content-Length:")) contentLength = line.substring(15).toInt();
    }
    if (method != "PUT" || !path.startsWith("/kanikey/") || path.indexOf("..") >= 0 ||
        path.length() > 60 || contentLength <= 0 || contentLength > 32 * 1024 * 1024) {
        Serial.printf("[asset] rejected %s %d\n", method.c_str(), contentLength);
        assetRespond(client, 400, "Bad Request");
        stopAssetServer();
        return;
    }
    const int slash = path.lastIndexOf('/');
    if (slash > 0) {
        const String dir = path.substring(0, slash);
        if (!SD.exists(dir)) SD.mkdir(dir);
    }
    const bool isPack = path == "/kanikey/glyphs.kgp";
    if (isPack) gPack.close();
    if (SD.exists(path)) SD.remove(path);
    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        Serial.printf("[asset] cannot open %s\n", path.c_str());
        assetRespond(client, 500, "Cannot Open");
        stopAssetServer();
        return;
    }

    drawLoading("Wi-Fi upload", path.c_str());
    Serial.printf("[asset] writing %s (%d bytes)\n", path.c_str(), contentLength);
    uint8_t buf[1024];
    uint32_t got = 0;
    uint32_t lastData = millis();
    while (got < static_cast<uint32_t>(contentLength)) {
        const int avail = client.available();
        if (avail > 0) {
            const size_t take = std::min<size_t>(
                sizeof buf, std::min<size_t>(static_cast<size_t>(avail),
                                            contentLength - static_cast<int>(got)));
            const int n = client.read(buf, take);
            if (n > 0) {
                f.write(buf, n);
                got += static_cast<uint32_t>(n);
                lastData = millis();
                continue;
            }
        }
        if (!client.connected() && !client.available()) break;
        if (millis() - lastData > 10000) break;
        delay(1);
    }
    f.close();
    if (got != static_cast<uint32_t>(contentLength)) SD.remove(path);
    Serial.printf("[asset] done %u/%d\n", got, contentLength);
    if (got == static_cast<uint32_t>(contentLength))
        assetRespond(client, 201, "Created");
    else
        assetRespond(client, 500, "Short Body");

    if (isPack && got == static_cast<uint32_t>(contentLength)) {
        gPack.open(SD, "/kanikey/glyphs.kgp");
        Serial.printf("[pack] %s\n", gPack.statusLine());
    }
    stopAssetServer();
}

void startAssetServer() {
    if (gReviewActive || gLessonActive) {
        Serial.println("[asset] finish the review/lesson first");
        return;
    }
    if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
        Serial.println("[asset] no Wi-Fi");
        return;
    }
    gAssetServer.begin();
    gAssetServer.setNoDelay(true);
    gAssetServerActive = true;
    char url[64];
    snprintf(url, sizeof url, "http://%s:8080", kk::DevWifi::ip().c_str());
    drawLoading("Asset server", url);
    Serial.printf("[asset] ready %s\n", url);
}

void uploadSdFile(const String& line) {
    // up <bytes> <path>: the host sends the raw bytes right after the ack.
    const int sep = line.indexOf(' ', 3);
    if (sep < 0) {
        Serial.println("[up] use: up <bytes> <path>");
        return;
    }
    const long requested = line.substring(3, sep).toInt();
    String path = line.substring(sep + 1);
    path.trim();
    const uint32_t size = requested > 0 ? static_cast<uint32_t>(requested) : 0;
    if (!gSdOk || size == 0 || size > 32u * 1024u * 1024u || !path.startsWith("/") ||
        path.indexOf("..") >= 0 || path.length() > 60) {
        Serial.println("[up] bad size, path or SD");
        return;
    }

    const int slash = path.lastIndexOf('/');
    if (slash > 0) {
        const String dir = path.substring(0, slash);
        if (!SD.exists(dir)) SD.mkdir(dir);
    }
    const bool isPack = path == "/kanikey/glyphs.kgp";
    if (isPack) gPack.close();
    if (SD.exists(path)) SD.remove(path);
    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        Serial.printf("[up] cannot open %s\n", path.c_str());
        return;
    }

    drawLoading("Writing SD...", path.c_str());
    Serial.printf("[up] ready %s %u\n", path.c_str(), static_cast<unsigned>(size));
    static uint8_t buf[8192];
    uint32_t got = 0;
    uint32_t lastLog = millis();
    while (got < size) {
        const size_t want = std::min<size_t>(sizeof buf, size - got);
        size_t have = 0;
        uint32_t lastByte = millis();
        while (have < want) {
            const int available = Serial.available();
            if (available > 0) {
                const size_t take =
                    std::min<size_t>(static_cast<size_t>(available), want - have);
                const size_t n = Serial.readBytes(buf + have, take);
                if (n) {
                    have += n;
                    lastByte = millis();
                    continue;
                }
            }
            if (millis() - lastByte > 5000) break;
            delay(1);
        }
        if (have == 0) {
            Serial.println("[up] timeout");
            break;
        }
        f.write(buf, have);
        got += have;
        Serial.printf("[up] ack %u\n", static_cast<unsigned>(got));
        if (millis() - lastLog > 1000) {
            lastLog = millis();
            Serial.printf("[up] %u/%u\n", static_cast<unsigned>(got),
                          static_cast<unsigned>(size));
        }
        if (have < want) break;
    }
    f.close();
    if (got != size) SD.remove(path);
    Serial.printf("[up] done %u/%u\n", static_cast<unsigned>(got),
                  static_cast<unsigned>(size));
    if (isPack && got == size) {
        if (gPack.open(SD, "/kanikey/glyphs.kgp"))
            Serial.printf("[pack] %s\n", gPack.statusLine());
        else
            Serial.printf("[pack] %s\n", gPack.statusLine());
    }
    gDirty = true;
}

void handleSerial() {
    if (Serial.available()) {
        gLastInputAt = millis();
        wakeScreen();
    }
    while (Serial.available()) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\r') continue;
        if (c != '\n') {
            if (gSerialLine.length() < 80) gSerialLine += c;
            continue;
        }
        gSerialLine.trim();
        if (gSerialLine.startsWith("up ")) {
            uploadSdFile(gSerialLine);
        } else if (gSerialLine == "w") {
            snprintf(gNetLine, sizeof gNetLine, "NET: joining...");
            gDirty = true;
            if (kk::DevWifi::connect(30000))
                snprintf(gNetLine, sizeof gNetLine, "NET: %s", kk::DevWifi::ip().c_str());
            else
                snprintf(gNetLine, sizeof gNetLine, "NET: join failed");
        } else if (gSerialLine.startsWith("w ")) {
            String rest = gSerialLine.substring(2);
            const int sep = rest.indexOf('|');
            if (sep < 0) {
                Serial.println("[wifi] use: w <ssid>|<password>");
            } else {
                const String ssid = rest.substring(0, sep);
                const String pass = rest.substring(sep + 1);
                if (!kk::DevWifi::saveCredentials(ssid, pass)) {
                    Serial.println("[wifi] not saved (bad SSID or password length)");
                } else if (kk::DevWifi::connect(30000)) {
                    snprintf(gNetLine, sizeof gNetLine, "NET: %s", kk::DevWifi::ip().c_str());
                } else {
                    snprintf(gNetLine, sizeof gNetLine, "NET: join failed");
                }
            }
        } else if (gSerialLine == "t") {
            if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                snprintf(gNetLine, sizeof gNetLine, "API: no Wi-Fi");
            } else {
                runProbe();
            }
        } else if (gSerialLine == "u") {
            if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                snprintf(gNetLine, sizeof gNetLine, "API: no Wi-Fi");
            } else {
                runProbe();
            }
        } else if (gSerialLine.startsWith("k ")) {
            String token = gSerialLine.substring(2);
            token.trim();
            if (kk::DevToken::save(token)) {
                Serial.printf("[auth] token saved (%u chars)\n", static_cast<unsigned>(token.length()));
                if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                    snprintf(gNetLine, sizeof gNetLine, "API: no Wi-Fi");
                } else {
                    runProbe();
                }
            } else {
                Serial.println("[auth] token not saved (empty or too long)");
            }
        } else if (gSerialLine == "c") {
            kk::DevToken::clear();
            Serial.println("[auth] token cleared");
            snprintf(gNetLine, sizeof gNetLine, "API: token cleared");
        } else if (gSerialLine == "d") {
            kk::DevWifi::disconnect();
            snprintf(gNetLine, sizeof gNetLine, "NET: off");
            Serial.println("[wifi] disconnected");
        } else if (gSerialLine == "o") {
            Serial.printf("[outbox] %u items\n", static_cast<unsigned>(gOutbox.size()));
            for (const kk::OutboxItem& it : gOutbox)
                Serial.printf("[outbox] seq %u assignment %u state %u attempts %u\n",
                              static_cast<unsigned>(it.seq),
                              static_cast<unsigned>(it.assignmentId),
                              static_cast<unsigned>(it.state), static_cast<unsigned>(it.attempts));
        } else if (gSerialLine == "f") {
            if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                snprintf(gNetLine, sizeof gNetLine, "Outbox: no Wi-Fi");
            } else {
                flushOutbox();
            }
        } else if (gSerialLine == "x") {
            const uint32_t seq = kk::outboxAdd(gOutbox, 999999999, 1, 0, 0,
                                               static_cast<uint32_t>(time(nullptr)));
            if (seq) {
                saveOutbox();
                Serial.printf("[outbox] synthetic seq %u queued\n", static_cast<unsigned>(seq));
            }
        } else if (gSerialLine == "z") {
            gOutbox.clear();
            saveOutbox();
            Serial.println("[outbox] cleared");
        } else if (gSerialLine == "l") {
            if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                snprintf(gNetLine, sizeof gNetLine, "Lesson: no Wi-Fi");
            } else {
                startLessons();
            }
        } else if (gSerialLine == "g") {
            if (gLessonActive) {
                ++gLessonPage;
                gDirty = true;
            }
        } else if (gSerialLine == "v") {
            if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                snprintf(gNetLine, sizeof gNetLine, "Review: no Wi-Fi");
            } else {
                startReview();
            }
        } else if (gSerialLine == "e") {
            if (gLessonActive) {
                startCurrentLesson();
            } else if (gReviewActive) {
                if (gFeedback) advanceFeedback();
                else checkCurrent();
            }
        } else if (gSerialLine == "q") {
            if (gLessonActive) {
                gLessonActive = false;
                gShowHome = true;
                gDirty = true;
            } else if (gReviewActive) {
                endReview();
            }
        } else if (gSerialLine.startsWith("y ")) {
            if (gReviewActive && !gFeedback) {
                const String text = gSerialLine.substring(2);
                if (gQueue.part() == kk::ReviewPart::Meaning)
                    gMeaningInput += text.c_str();
                else
                    gDirectReading += text.c_str();
                gDirty = true;
            }
        } else if (gSerialLine == "b") {
            if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                snprintf(gNetLine, sizeof gNetLine, "Batch: no Wi-Fi");
            } else {
                runBatch();
            }
        } else if (gSerialLine == "m") {
            if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                snprintf(gNetLine, sizeof gNetLine, "Summary: no Wi-Fi");
            } else {
                runSummary();
            }
        } else if (gSerialLine == "p") {
            dumpCanvas();
        } else if (gSerialLine == "h") {
            Serial.printf("[heap] free %u, largest %u, min %u\n",
                          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                          static_cast<unsigned>(heap_caps_get_largest_free_block(
                              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                          static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
        } else if (gSerialLine == "i") {
            Serial.printf("[pack] %s\n", gPack.statusLine());
        } else if (gSerialLine == "S") {
            startAssetServer();
        } else if (gSerialLine == "s") {
            if (kk::DevWifi::connected()) {
                Serial.println("[wifi] disconnect first");
            } else {
                WiFi.mode(WIFI_STA);
                const int n = WiFi.scanNetworks();
                Serial.printf("[wifi] %d networks\n", n);
                for (int i = 0; i < n; ++i)
                    Serial.printf("[wifi] %2d  %4d dBm  %s\n", i, WiFi.RSSI(i),
                                  WiFi.SSID(i).c_str());
                WiFi.scanDelete();
                WiFi.mode(WIFI_OFF);
            }
        } else if (gSerialLine == "r") {
            Serial.println("[kk] restart");
            delay(100);
            ESP.restart();
        } else if (!gSerialLine.isEmpty()) {
            Serial.printf("[kk] commands: w=wifi w <ssid>|<pass> t=tls u=user m=summary b=batch v=review l=lessons g=page y<text> e=enter q=quit d=wifi-off o=outbox f=flush x=test-queue z=clear k <token> c=clear h=heap i=pack S=asset-server p=picture s=scan r=restart up <bytes> <path>=sd-upload\n");
        }
        gSerialLine = "";
        gDirty = true;
    }
}

}  // namespace

void setup() {
    Serial.setRxBufferSize(16384);
    Serial.begin(115200);
    delay(250);

    tgc::board::begin();
    gDisp = &tgc::board::display();
    gDisp->setRotation(1);
    gDisp->setBrightness(128);

    ensureCanvas();
    kk::wkSetRequestHooks(requestBefore, requestAfter);

    Serial.printf("\n[kk] KaniKey M0\n");
    Serial.printf("[kk] canvas: %s (%u bytes, %d bpp)\n", gCanvas ? "ok" : "none",
                  static_cast<unsigned>(gCanvas ? gCanvas->bufferLength() : 0),
                  static_cast<int>(gCanvas ? gCanvas->getColorDepth() : 0));
    Serial.printf("[kk] board: %s\n", tgc::board::name());
    Serial.printf("[kk] flash: %u MB, PSRAM: %u MB\n",
                  static_cast<unsigned>(ESP.getFlashChipSize() / (1024 * 1024)),
                  static_cast<unsigned>(ESP.getPsramSize() / (1024 * 1024)));
    Serial.printf("[kk] internal heap: %u free, %u largest\n",
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                         MALLOC_CAP_8BIT)));
    Serial.printf("[kk] keyboard: %s\n", tgc::board::hasKeyboard() ? "yes" : "no");

    if (!LittleFS.begin(true)) {
        Serial.println("[kk] LittleFS mount failed");
    } else {
        kk::outboxLoad(gOutbox);
        Serial.printf("[outbox] pending %u\n",
                      static_cast<unsigned>(kk::outboxPendingCount(gOutbox)));
    }

    gSdOk = mountSd();
    if (gSdOk) {
        gPack.open(SD, "/kanikey/glyphs.kgp");
        Serial.printf("[pack] %s\n", gPack.statusLine());
    }
    gLastInputAt = millis();
    gDirty = true;
    Serial.println("[kk] first draw");
    draw();
    Serial.println("[kk] first draw done");
    gDirty = false;
    gLastDrawMs = millis();

    Serial.printf("[wifi] saved credentials: %s\n", kk::DevWifi::hasSaved() ? "yes" : "no");
    if (kk::DevWifi::hasSaved()) {
        snprintf(gNetLine, sizeof gNetLine, "NET: joining %s",
                 kk::DevWifi::savedSsid().c_str());
        drawStatus();
        if (kk::DevWifi::connect(30000)) {
            snprintf(gNetLine, sizeof gNetLine, "NET: %s", kk::DevWifi::ip().c_str());
            drawStatus();
            setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
            tzset();
            configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
            const uint32_t ntpStart = millis();
            while (time(nullptr) < 1700000000 && millis() - ntpStart < 5000) delay(100);
            const uint32_t utc = static_cast<uint32_t>(time(nullptr));
            Serial.printf("[time] %s: %u\n", utc >= 1700000000 ? "synced" : "not synced",
                          static_cast<unsigned>(utc));
            flushOutbox();
            runProbe();
            runSummary();
        } else {
            snprintf(gNetLine, sizeof gNetLine, "NET: join failed");
            gDirty = true;
        }
    } else {
        snprintf(gNetLine, sizeof gNetLine, "NET: no saved Wi-Fi");
    }
    drawStatus();
    gLastStatusMs = millis();
}

void loop() {
    tgc::board::update();
    handleSerial();
    serviceAssetServer();

    const uint32_t now = millis();
    if (gScreenOff) {
        bool wake = false;
        KeyEvent ignored;
        while (tgc::board::nextKeyEvent(&ignored)) wake = true;
        if (tgc::board::wasPressed(tgc::board::Button::Select) ||
            tgc::board::wasHeld(tgc::board::Button::Select))
            wake = true;
        if (wake) {
            gLastInputAt = now;
            wakeScreen();
        } else {
            delay(10);
            return;
        }
    }

    if (gDimmed && (tgc::board::wasPressed(tgc::board::Button::Select) ||
                     tgc::board::wasHeld(tgc::board::Button::Select))) {
        gLastInputAt = now;
        wakeScreen();
    }

    bool firstKey = true;
    KeyEvent e;
    while (tgc::board::nextKeyEvent(&e)) {
        gLastInputAt = now;
        if (firstKey) {
            firstKey = false;
            if (gDimmed) wakeScreen();
        }
        if (e.key == Key::None) {
            // The TCA8418 lost events: treat every key as released.
            gShift = gFn = gCtrl = gOpt = gAlt = false;
            pushLog("^ (all keys released)");
            gDirty = true;
            continue;
        }
        onKey(e);
    }

    if (gDirty && now - gLastDrawMs >= 16) {
        draw();
        gDirty = false;
        gLastDrawMs = now;
    }
    if (now - gLastStatusMs >= 1000) {
        drawStatus();
        gLastStatusMs = now;
    }

    // G0: a short press wakes the screen; a long press is the future power
    // action. For M0 it just logs, so the button can be checked too.
    if (tgc::board::wasPressed(tgc::board::Button::Select)) {
        Serial.println("[key] G0 pressed");
    }
    if (tgc::board::wasHeld(tgc::board::Button::Select)) {
        Serial.println("[key] G0 held");
    }

    if (!gDimmed && now - gLastInputAt > 30000) {
        tgc::board::setBrightness(20);
        gDimmed = true;
    }
    if (!gScreenOff && now - gLastInputAt > 60000) {
        tgc::board::setDisplayOn(false);
        gScreenOff = true;
    }
    if (!gReviewActive && !gReviewSummary && !gFeedback && !gLessonActive && !gAssetServerActive &&
        now - gLastInputAt > 120000 && !tgc::board::externalPower()) {
        Serial.println("[power] idle sleep");
        tgc::board::deepSleep();
    }

    static uint32_t lastBeat = 0;
    if (now - lastBeat > 5000) {
        lastBeat = now;
        Serial.printf("[kk] alive heap=%u\n",
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    }
    delay(4);
}

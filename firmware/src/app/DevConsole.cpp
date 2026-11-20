// The serial console and the KANIKEY_DEV asset server.
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
#include "app/RemindDev.h"
#include "app/RemProbe.h"
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
#include "wk/Text.h"
#include "wk/Time.h"
#include "ui/Theme.h"

namespace kk::app {

void onKey(const tgc::KeyEvent& e);

namespace {

using tgc::Key;
using tgc::KeyEvent;
using kk::Modal;
using kk::Screen;
using namespace kk::ui;

#ifdef KANIKEY_DEV
// Seeds a small cache to verify CACHE-03/04 without WaniKani credentials.
// Dev-only: `C` writes it, `X` removes it.
void seedDevCache();
// Dev-only `L`: a one-item lesson whose subject is a radical without a Unicode
// character, so the card must use the pack's radical bitmap. It bypasses the
// network and the summary so the radical-only path can be exercised without a
// low-level account.
void seedDevLesson();
void clearDevCache();
// Dev-only `J`: two fixed rejected items and one stuck item for the outbox
// screen. Reusing the assignment IDs updates an earlier seed instead of piling
// up duplicates.
void seedDevRejectedOutbox();
#endif
void dumpCanvas();
void assetRespond(WiFiClient& client, int code, const char* reason);
void stopAssetServer();
void startAssetServer();
void uploadSdFile(const String& line);

// REV-10: the `y` injection is an answer input like the keyboard, so it stops
// at the same 64-byte cap.
void appendAnswerText(std::string& dst, const String& text) {
    for (unsigned i = 0; i < text.length(); ++i)
        kk::appendCapped(dst, text[i], kk::kMaxAnswerBytes);
}

// ONB-01: a serial/asset upload may replace either pack. Only a matching
// pack file is closed for the transfer; any pack upload re-opens both because
// a v1 combined glyphs.kgp can also serve the radical role.
struct PackTarget {
    kk::GlyphPack* pack = nullptr;
    kk::PackRole role = kk::PackRole::Font;
};

PackTarget packTarget(const String& path) {
    if (path == "/kanikey/glyphs.kgp")
        return {&state().storage.gFontPack, kk::PackRole::Font};
    if (path == "/kanikey/radicals.kgp")
        return {&state().storage.gRadicalPack, kk::PackRole::Radical};
    return {};
}

void reopenPacksAfterUpload() {
    if (state().storage.gSdOk) openGlyphPacks();
}
#ifdef KANIKEY_DEV
// "K <keys>" presses grid keys as if typed, so hardware checks can drive the
// real key handlers and the romaji IME. \n Enter, \e Esc, \t Tab, \b
// Backspace; any other character is found on the main or Shift layer.
void injectKeys(const String& keys);
#endif

#ifdef KANIKEY_DEV
// Seeds a small cache to verify CACHE-03/04 without WaniKani credentials.
// Dev-only: `C` writes it, `X` removes it.
void seedDevCache() {
    if (!kk::DevToken::has()) {
        Serial.println("[cache] set a token first: k <token>");
        return;
    }
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    const uint32_t stamp = now >= 1700000000u ? now : 1760000000u;

    kk::CacheSnapshot snapshot;
    snapshot.meta.savedAt = stamp;
    snapshot.meta.accountKey = currentAccountKey();
    snapshot.summary.reviews.push_back(
        kk::SummaryBucket{stamp - 60, 6, {1, 2, 3, 4, 5, 6}});
    snapshot.user.username = "dev";
    snapshot.user.level = 3;
    snapshot.user.subscriptionActive = true;
    snapshot.user.maxLevelGranted = 3;
    snapshot.user.lessonsBatchSize = 5;
    snapshot.levelProgress.level = 3;
    snapshot.levelProgress.unlockedAt = stamp - 7 * 86400;
    snapshot.levelProgress.startedAt = stamp - 7 * 86400;
    snapshot.levelProgress.valid = true;
    snapshot.srs = kk::SrsCounts{42, 18, 9, 4, 2, true};

    const auto addAssignment = [&snapshot, stamp](uint32_t id, uint32_t subjectId,
                                                  kk::SubjectType type) {
        kk::Assignment a;
        a.id = id;
        a.subjectId = subjectId;
        a.type = type;
        a.srsStage = 1;
        a.availableAt = stamp - 120;
        a.dataUpdatedAt = stamp - 240;
        snapshot.assignments.push_back(a);
    };
    addAssignment(1001, 1, kk::SubjectType::Radical);
    addAssignment(1002, 2, kk::SubjectType::Kanji);
    addAssignment(1003, 3, kk::SubjectType::Vocabulary);
    addAssignment(1004, 4, kk::SubjectType::Kanji);
    addAssignment(1005, 5, kk::SubjectType::Vocabulary);
    addAssignment(1006, 6, kk::SubjectType::Vocabulary);

    kk::SubjectCache subjects;
    subjects.meta = snapshot.meta;
    kk::Subject radical;
    radical.id = 1;
    radical.type = kk::SubjectType::Radical;
    radical.level = 1;
    radical.meanings = {{"ground", true, true}};
    subjects.subjects.push_back(radical);
    kk::Subject kanji;
    kanji.id = 2;
    kanji.type = kk::SubjectType::Kanji;
    kanji.level = 1;
    kanji.characters = "日";
    kanji.meanings = {{"day", true, true}};
    kanji.readings = {{"にち", true, true, kk::ReadingType::Onyomi}};
    kanji.meaningSynonyms = {"sun"};
    kanji.meaningMnemonic = "A window with the sun in it.";
    kanji.readingMnemonic = "Nichi sounds like a sunny day.";
    kanji.meaningHint = "Think of daylight.";
    kanji.readingHint = "The sun is nichi.";
    kanji.meaningNote = "Remember the sun.";
    kanji.readingNote = "Nichi, not ni.";
    kanji.componentSubjectIds = {1};
    kanji.visuallySimilarSubjectIds = {3};
    kanji.amalgamationSubjectIds = {3};
    subjects.subjects.push_back(kanji);
    kk::Subject vocabulary;
    vocabulary.id = 3;
    vocabulary.type = kk::SubjectType::Vocabulary;
    vocabulary.level = 1;
    vocabulary.characters = "日本";
    vocabulary.meanings = {{"Japan", true, true}};
    vocabulary.readings = {{"にほん", true, true, kk::ReadingType::Kunyomi}};
    vocabulary.meaningMnemonic = "The sun's origin is Japan.";
    vocabulary.readingMnemonic = "Nihon is Japan.";
    vocabulary.partsOfSpeech = {"noun"};
    vocabulary.contextSentences = {{"これは日本です。", "This is Japan."}};
    vocabulary.componentSubjectIds = {2};
    vocabulary.visuallySimilarSubjectIds = {2};
    subjects.subjects.push_back(vocabulary);

    // Answer-checker seeds: a kanji with two accepted on'yomi and one rejected
    // kun'yomi, plus two vocabulary items with a single reading.
    kk::Subject person;
    person.id = 4;
    person.type = kk::SubjectType::Kanji;
    person.level = 1;
    person.characters = "人";
    person.meanings = {{"person", true, true}};
    person.readings = {
        {"じん", true, true, kk::ReadingType::Onyomi},
        {"にん", false, true, kk::ReadingType::Onyomi},
        {"ひと", false, false, kk::ReadingType::Kunyomi},
    };
    subjects.subjects.push_back(person);
    kk::Subject tonight;
    tonight.id = 5;
    tonight.type = kk::SubjectType::Vocabulary;
    tonight.level = 1;
    tonight.characters = "今夜";
    tonight.meanings = {{"tonight", true, true}};
    tonight.readings = {{"こんや", true, true, kk::ReadingType::Kunyomi}};
    subjects.subjects.push_back(tonight);
    kk::Subject photograph;
    photograph.id = 6;
    photograph.type = kk::SubjectType::Vocabulary;
    photograph.level = 1;
    photograph.characters = "写真";
    photograph.meanings = {{"photograph", true, true}};
    photograph.readings = {{"しゃしん", true, true, kk::ReadingType::Kunyomi}};
    subjects.subjects.push_back(photograph);

    auto stats = [](kk::Subject& s, uint32_t meaningCorrect, uint32_t meaningIncorrect,
                    uint32_t readingCorrect, uint32_t readingIncorrect,
                    uint8_t percentageCorrect) {
        s.meaningCorrect = meaningCorrect;
        s.meaningIncorrect = meaningIncorrect;
        s.readingCorrect = readingCorrect;
        s.readingIncorrect = readingIncorrect;
        s.percentageCorrect = percentageCorrect;
        s.hasReviewStats = true;
    };
    stats(subjects.subjects[0], 12, 2, 0, 0, 86);
    stats(subjects.subjects[1], 10, 2, 8, 1, 86);
    stats(subjects.subjects[2], 9, 1, 7, 1, 89);

    const bool snapshotOk = kk::cacheSaveSnapshot(snapshot);
    const bool subjectsOk = state().storage.gSdOk && kk::cacheSaveSubjects(SD, subjects);
    Serial.printf("[cache] dev seed snapshot=%d subjects=%d\n", snapshotOk ? 1 : 0,
                  subjectsOk ? 1 : 0);
    if (snapshotOk && loadCache()) state().display.gDirty = true;
}

// Dev-only `L`: a one-item lesson whose subject is a radical without a Unicode
// character, so the card must use the pack's radical bitmap. It bypasses the
// network and the summary so the radical-only path can be exercised without a
// low-level account.
void seedDevLesson() {
    state().lesson.gLessonBatch = kk::LessonBatch{};
    state().lesson.gLessonComponents.clear();
    kk::Assignment a;
    a.id = 2001;
    a.subjectId = 1;
    a.type = kk::SubjectType::Radical;
    a.srsStage = 0;
    a.lessonPosition = 0;
    a.availableAt = static_cast<uint32_t>(time(nullptr));
    state().lesson.gLessonBatch.assignments.push_back(a);
    kk::Subject radical;
    radical.id = 1;
    radical.type = kk::SubjectType::Radical;
    radical.level = 1;
    radical.meanings = {{"ground", true, true}};
    radical.meaningMnemonic = "The ground under your feet.";
    radical.meaningHint = "A flat line.";
    state().lesson.gLessonBatch.subjects.push_back(radical);
    state().lesson.gLessonBatch.ok = true;
    state().screen.screen = Screen::LessonList;
    state().lesson.startConfirm = false;
    state().lesson.quizBatch = false;
    state().lesson.gLessonBatchLimit = kk::settingsLessonBatchSize();
    state().lesson.gLessonBrowse.begin(1, state().lesson.gLessonBatchLimit);
    state().lesson.gLessonNote[0] = 0;
    state().display.gDirty = true;
    Serial.println("[lesson] dev radical-only lesson seeded");
}

void clearDevCache() {
    kk::cacheClearSnapshot();
    if (state().storage.gSdOk) kk::cacheClearSubjects(SD);
    state().session.gHaveSummary = false;
    state().session.gHaveUser = false;
    state().cache.gBatch = kk::ReviewBatch{};
    state().cache.gBatchEvicted = false;
    state().cache.gCachedBatchCount = 0;
    state().session.gSrs = kk::SrsCounts{};
    state().session.gDueReviews = 0;
    state().session.gLessons = 0;
    state().cache.gOffline = false;
    if (kk::screenBase(state().screen) == Screen::Home)
        state().screen.screen = Screen::Boot;
    state().session.gSyncedAt = 0;
    state().cache.gCacheSavedAt = 0;
    state().display.gDirty = true;
    Serial.println("[cache] dev cache cleared");
}

// Dev-only `J`: two fixed rejected items and one stuck item for the outbox
// screen. Reusing the assignment IDs updates an earlier seed instead of piling
// up duplicates.
void seedDevRejectedOutbox() {
    struct Seed {
        uint32_t assignmentId;
        uint32_t subjectId;
        kk::OutboxState state;
        uint8_t attempts;
        uint16_t httpCode;
    };
    static const Seed kSeeds[] = {
        {999999901, 999999001, kk::OutboxState::Rejected, 1, 422},
        {999999902, 999999002, kk::OutboxState::Rejected, 1, 409},
        {999999903, 999999003, kk::OutboxState::Stuck, kk::kOutboxMaxAttempts, 0},
    };
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    const uint32_t createdAt = now >= 1700000000u ? now : 1793000000u;
    for (const Seed& seed : kSeeds) {
        kk::OutboxItem* item = nullptr;
        for (kk::OutboxItem& it : state().outbox.gOutbox)
            if (it.assignmentId == seed.assignmentId) {
                item = &it;
                break;
            }
        if (!item) {
            const uint32_t seq = kk::outboxAdd(state().outbox.gOutbox, seed.assignmentId, seed.subjectId, 0, 0,
                                               createdAt);
            if (!seq) {
                Serial.println("[outbox] seed failed: full");
                continue;
            }
            item = kk::outboxFind(state().outbox.gOutbox, seq);
        }
        if (!item) continue;
        item->state = seed.state;
        item->httpCode = seed.httpCode;
        item->attempts = seed.attempts;
        item->createdAt = createdAt;
        if (seed.state == kk::OutboxState::Stuck)
            Serial.printf("[outbox] synthetic stuck assignment %u after %u attempts\n",
                          static_cast<unsigned>(seed.assignmentId),
                          static_cast<unsigned>(seed.attempts));
        else
            Serial.printf("[outbox] synthetic rejected assignment %u HTTP %u\n",
                          static_cast<unsigned>(seed.assignmentId),
                          static_cast<unsigned>(seed.httpCode));
    }
    saveOutbox();
    const Screen base = kk::screenBase(state().screen);
    const Modal top = kk::screenTop(state().screen);
    const bool sessionScreen = base == Screen::Review || base == Screen::ReviewSummary ||
                               base == Screen::LessonEmpty || base == Screen::LessonList ||
                               base == Screen::LessonCard || base == Screen::LessonQuiz ||
                               base == Screen::Sync || base == Screen::Setup;
    const bool settingsBelow = top == Modal::Settings || top == Modal::Pause ||
                               top == Modal::About || top == Modal::SignOut;
    if (!sessionScreen && !settingsBelow) {
        if (base == Screen::Outbox) {
            if (top == Modal::OutboxConfirm) kk::screenPop(&state().screen);
            state().screen.screen = Screen::Home;
        } else if (base == Screen::Boot && top == Modal::None) {
            state().screen.screen = Screen::Home;
        }
    }
    state().display.gDirty = true;
}
#endif

void dumpCanvas() {
    if (!state().display.gCanvas) {
        Serial.println("#shot 0 0 none");
        return;
    }
    static const char kHex[] = "0123456789abcdef";
    static char line[16 + 4 * 240 + 2];
    const int w = state().display.gCanvas->width();
    const int h = state().display.gCanvas->height();
    // The sprite buffer is RGB332 (one byte per pixel); dump it raw.
    const bool rgb332 = state().display.gCanvas->getColorDepth() == lgfx::rgb332_1Byte;
    const uint8_t* px = static_cast<const uint8_t*>(state().display.gCanvas->getBuffer());
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
    state().storage.gAssetServerActive = false;
    state().storage.gAssetServer.end();
    state().display.gDirty = true;
}

void startAssetServer() {
    const Screen base = kk::screenBase(state().screen);
    if (base == Screen::Review || base == Screen::LessonCard ||
        base == Screen::LessonList || base == Screen::LessonEmpty ||
        (base == Screen::LessonQuiz && state().lesson.quizBatch)) {
        Serial.println("[asset] finish the review/lesson first");
        return;
    }
    if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
        Serial.println("[asset] no Wi-Fi");
        return;
    }
    state().storage.gAssetServer.begin();
    state().storage.gAssetServer.setNoDelay(true);
    state().storage.gAssetServerActive = true;
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
    if (!state().storage.gSdOk || size == 0 || size > 32u * 1024u * 1024u || !path.startsWith("/") ||
        path.indexOf("..") >= 0 || path.length() > 60) {
        Serial.println("[up] bad size, path or SD");
        return;
    }

    const int slash = path.lastIndexOf('/');
    if (slash > 0) {
        const String dir = path.substring(0, slash);
        if (!SD.exists(dir)) SD.mkdir(dir);
    }
    const PackTarget target = packTarget(path);
    const bool isPack = target.pack != nullptr;
    if (isPack) target.pack->close();
    // Same tmp-and-rename as the Wi-Fi upload: a short transfer keeps the old
    // file, a bad pack CRC never replaces a working pack.
    const String tmpPath = path + ".tmp";
    if (SD.exists(tmpPath)) SD.remove(tmpPath);
    File f = SD.open(tmpPath, FILE_WRITE);
    if (!f) {
        Serial.printf("[up] cannot open %s\n", tmpPath.c_str());
        if (isPack) reopenPacksAfterUpload();
        return;
    }

    drawLoading("Writing SD...", path.c_str());
    Serial.printf("[up] ready %s %u\n", path.c_str(), static_cast<unsigned>(size));
    static uint8_t buf[8192];
    uint32_t got = 0;
    bool writeOk = true;
    uint32_t lastLog = millis();
    while (got < size) {
        // Keep the block at the host helper's 2 KiB chunk: the USB RX ring is
        // 4 KiB, and tools/device.py waits for one ack per chunk. A larger
        // `want` made both sides wait for each other (5 s timeout, then the
        // transfer failed on anything over 2 KiB).
        const size_t want = std::min<size_t>(2048, size - got);
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
        if (f.write(buf, have) != have) {
            Serial.println("[up] write failed");
            writeOk = false;
            break;
        }
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
    Serial.printf("[up] done %u/%u\n", static_cast<unsigned>(got),
                  static_cast<unsigned>(size));
    if (!writeOk || got != size) {
        SD.remove(tmpPath);
        Serial.printf("[up] failed, kept %s\n", path.c_str());
        if (isPack) reopenPacksAfterUpload();
        state().display.gDirty = true;
        return;
    }

    if (isPack) {
        const bool crcOk = target.pack->open(SD, tmpPath.c_str(), target.role, true);
        char status[48];
        snprintf(status, sizeof status, "%s", target.pack->statusLine());
        target.pack->close();
        Serial.printf("[pack] tmp %s\n", status);
        if (!crcOk) {
            SD.remove(tmpPath);
            Serial.println("[up] bad pack crc, kept the old pack");
            reopenPacksAfterUpload();
            state().display.gDirty = true;
            return;
        }
    }
    if (SD.exists(path)) SD.remove(path);
    if (!SD.rename(tmpPath, path)) {
        SD.remove(tmpPath);
        Serial.printf("[up] rename failed, %s removed\n", path.c_str());
        if (isPack) reopenPacksAfterUpload();
        state().display.gDirty = true;
        return;
    }
    if (isPack) reopenPacksAfterUpload();
    state().display.gDirty = true;
}

#ifdef KANIKEY_DEV
// "K <keys>" presses grid keys as if typed, so hardware checks can drive the
// real key handlers and the romaji IME. \n Enter, \e Esc, \t Tab, \b
// Backspace; any other character is found on the main or Shift layer.
void injectKeys(const String& keys) {
    const auto press = [](int x, int y, bool shift) {
        const bool savedShift = state().ui.gShift;
        state().ui.gShift = shift;
        KeyEvent e;
        e.key = tgc::cardputerKeyAt(x, y);
        e.x = static_cast<uint8_t>(x);
        e.y = static_cast<uint8_t>(y);
        e.down = true;
        onKey(e);
        e.down = false;
        onKey(e);
        state().ui.gShift = savedShift;
    };
    for (unsigned i = 0; i < keys.length(); ++i) {
        char c = keys[i];
        if (c == '\\' && i + 1 < keys.length()) {
            const char next = keys[++i];
            if (next == 'n') press(13, 2, false);
            else if (next == 'e') press(0, 0, false);
            else if (next == 't') press(0, 1, false);
            else if (next == 'b') press(13, 0, false);
            else if (next == '\\') c = '\\';
            if (next != '\\') continue;
        }
        bool found = false;
        for (int layer = 0; layer < 2 && !found; ++layer)
            for (int y = 0; y < tgc::kCardputerRows && !found; ++y)
                for (int x = 0; x < tgc::kCardputerCols && !found; ++x)
                    if (tgc::cardputerCharAt(x, y, layer == 1) == c) {
                        press(x, y, layer == 1);
                        found = true;
                    }
        if (!found) Serial.printf("[key] no key for '%c'\n", c);
    }
    state().ui.gLastInputAt = millis();
}
#endif

}  // namespace

#ifdef KANIKEY_DEV
void showDebug() {
    if (!state().display.gDisp) return;
    DebugView view;
    view.batteryPercent = tgc::board::batteryPercent();
    view.netLine = state().ui.gNetLine;
    view.sdLine = state().storage.gSdLine;
    view.sdOk = state().storage.gSdOk;
    for (int i = 0; i < 3; ++i) view.log[i] = state().ui.gLog[i].text;
    view.shift = state().ui.gShift;
    view.fn = state().ui.gFn;
    view.ctrl = state().ui.gCtrl;
    view.opt = state().ui.gOpt;
    view.alt = state().ui.gAlt;
    snprintf(view.heapLine, sizeof view.heapLine, "heap %u  largest %u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                    MALLOC_CAP_8BIT)));

    auto& d = target();
    d.startWrite();
    drawDebugScreen(d, view);
    d.endWrite();
    present();
}

void showRadicalTest() {
    if (!state().display.gDisp) return;
    RadicalTestView view;
    view.subjectId = state().dev.gRadicalTestId;

    auto& d = target();
    d.startWrite();
    drawRadicalTestScreen(d, state().storage.gFontPack, state().storage.gRadicalPack, view);
    d.endWrite();
    present();
}
#endif

void serviceAssetServer() {
    if (!state().storage.gAssetServerActive) return;
    WiFiClient client = state().storage.gAssetServer.available();
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
    const PackTarget target = packTarget(path);
    const bool isPack = target.pack != nullptr;
    if (isPack) target.pack->close();
    // Write a tmp file and rename it over the old one, so an interrupted
    // upload never leaves a broken pack or asset behind (FIX-24 c).
    const String tmpPath = path + ".tmp";
    if (SD.exists(tmpPath)) SD.remove(tmpPath);
    File f = SD.open(tmpPath, FILE_WRITE);
    if (!f) {
        Serial.printf("[asset] cannot open %s\n", tmpPath.c_str());
        assetRespond(client, 500, "Cannot Open");
        stopAssetServer();
        if (isPack) reopenPacksAfterUpload();
        return;
    }

    drawLoading("Wi-Fi upload", path.c_str());
    Serial.printf("[asset] writing %s (%d bytes)\n", path.c_str(), contentLength);
    uint8_t buf[1024];
    uint32_t got = 0;
    bool writeOk = true;
    uint32_t lastData = millis();
    while (got < static_cast<uint32_t>(contentLength)) {
        const int avail = client.available();
        if (avail > 0) {
            const size_t take = std::min<size_t>(
                sizeof buf, std::min<size_t>(static_cast<size_t>(avail),
                                            contentLength - static_cast<int>(got)));
            const int n = client.read(buf, take);
            if (n > 0) {
                if (f.write(buf, n) != static_cast<size_t>(n)) {
                    writeOk = false;
                    break;
                }
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
    const bool complete = writeOk && got == static_cast<uint32_t>(contentLength);
    Serial.printf("[asset] done %u/%d\n", got, contentLength);
    if (!complete) {
        // Keep the old file; only the tmp file goes away.
        SD.remove(tmpPath);
        assetRespond(client, 500, writeOk ? "Short Body" : "Write Failed");
        stopAssetServer();
        if (isPack) reopenPacksAfterUpload();
        return;
    }

    if (isPack) {
        // A new pack is verified before it replaces the working one. The CRC
        // runs here, not at boot (3,587 ms for the 3.7 MB pack).
        const bool crcOk = target.pack->open(SD, tmpPath.c_str(), target.role, true);
        char status[48];
        snprintf(status, sizeof status, "%s", target.pack->statusLine());
        target.pack->close();
        Serial.printf("[pack] tmp %s\n", status);
        if (!crcOk) {
            SD.remove(tmpPath);
            assetRespond(client, 422, "Bad Pack");
            stopAssetServer();
            reopenPacksAfterUpload();
            return;
        }
    }
    if (SD.exists(path)) SD.remove(path);
    if (!SD.rename(tmpPath, path)) {
        SD.remove(tmpPath);
        assetRespond(client, 500, "Rename Failed");
        stopAssetServer();
        if (isPack) reopenPacksAfterUpload();
        return;
    }
    assetRespond(client, 201, "Created");
    // The client is closed before the pack is opened again (FIX-25).
    stopAssetServer();
    if (isPack) reopenPacksAfterUpload();
}

void handleSerial() {
    if (Serial.available()) {
        state().ui.gLastInputAt = millis();
        wakeScreen();
    }
    while (Serial.available()) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\r') continue;
        if (c != '\n') {
            if (state().ui.gSerialLine.length() < 256) state().ui.gSerialLine += c;  // U <url> takes up to 200
            continue;
        }
        state().ui.gSerialLine.trim();
        if (state().ui.gSerialLine.startsWith("up ")) {
            uploadSdFile(state().ui.gSerialLine);
        } else if (state().ui.gSerialLine == "p") {
            dumpCanvas();
        } else if (state().ui.gSerialLine == "h") {
            Serial.printf("[heap] free %u, largest %u, min %u, stack %u\n",
                          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                          static_cast<unsigned>(heap_caps_get_largest_free_block(
                              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                          static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
                          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr) *
                                                sizeof(StackType_t)));
#ifdef KANIKEY_DEV
            {
                auto& display = tgc::board::display();
                const auto& pc = display.getPanel()->config();
                Serial.printf("[disp] rot %u %dx%d panel %dx%d off %u mem %dx%d\n",
                              static_cast<unsigned>(display.getRotation()),
                              static_cast<int>(display.width()),
                              static_cast<int>(display.height()),
                              static_cast<int>(pc.panel_width), static_cast<int>(pc.panel_height),
                              static_cast<unsigned>(pc.offset_rotation),
                              static_cast<int>(pc.memory_width), static_cast<int>(pc.memory_height));
            }
#endif
        } else if (state().ui.gSerialLine == "i") {
            Serial.printf("[pack] font: %s\n", state().storage.gFontPack.statusLine());
            Serial.printf("[pack] radicals: %s\n", state().storage.gRadicalPack.statusLine());
        } else if (state().ui.gSerialLine == "S") {
            startAssetServer();
        } else if (state().ui.gSerialLine == "r") {
            Serial.println("[kk] restart");
            delay(100);
            ESP.restart();
#ifdef KANIKEY_DEV
        } else if (state().ui.gSerialLine == "w") {
            snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "NET: joining...");
            state().display.gDirty = true;
            if (kk::DevWifi::connect(30000))
                snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "NET: %s", kk::DevWifi::ip().c_str());
            else
                snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "NET: join failed");
        } else if (state().ui.gSerialLine.startsWith("w ")) {
            String rest = state().ui.gSerialLine.substring(2);
            const int sep = rest.indexOf('|');
            if (sep < 0) {
                Serial.println("[wifi] use: w <ssid>|<password>");
            } else {
                const String ssid = rest.substring(0, sep);
                const String pass = rest.substring(sep + 1);
                if (!kk::DevWifi::saveCredentials(ssid, pass)) {
                    Serial.println("[wifi] not saved (bad SSID or password length)");
                } else if (kk::DevWifi::connect(30000)) {
                    snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "NET: %s", kk::DevWifi::ip().c_str());
                } else {
                    snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "NET: join failed");
                }
            }
        } else if (state().ui.gSerialLine == "t") {
            if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "API: no Wi-Fi");
            } else {
                runProbe();
            }
        } else if (state().ui.gSerialLine == "u") {
            if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "API: no Wi-Fi");
            } else {
                runProbe();
            }
        } else if (state().ui.gSerialLine.startsWith("k ")) {
            String token = state().ui.gSerialLine.substring(2);
            token.trim();
            const String previousToken = kk::DevToken::load();
            if (kk::DevToken::save(token)) {
                if (previousToken != token) {
                    clearLevelUpNotice();
                    state().session.gHaveUser = false;
                }
                Serial.printf("[auth] token saved (%u chars)\n", static_cast<unsigned>(token.length()));
                state().sync.gDeferredAuthIssue = SyncIssue::None;
                state().sync.gOutboxAuthIssue = false;
                kk::wkClearAuthBlock();
                if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                    snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "API: no Wi-Fi");
                } else {
                    runProbe();
                }
            } else {
                Serial.println("[auth] token not saved (empty or too long)");
            }
        } else if (state().ui.gSerialLine == "c") {
            kk::DevToken::clear();
            kk::wkClearAuthBlock();
            kk::journalClear();
            kk::criticalClear();
            kk::practicePoolClear();
            state().study.gPracticePool.clear();
            state().study.gPracticePoolLoaded = false;
            state().study.gCritical.clear();
            state().study.gCriticalLoaded = false;
            state().stats.gStatsHistoryLoaded = false;
            state().review.gPendingJournalHandle = 0;
            state().sync.gDeferredAuthIssue = SyncIssue::None;
            state().sync.gOutboxAuthIssue = false;
            clearLevelUpNotice();
            state().session.gHaveUser = false;
            Serial.println("[auth] token cleared");
            snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "API: token cleared");
        } else if (state().ui.gSerialLine == "d") {
            kk::DevWifi::disconnect();
            snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "NET: off");
            Serial.println("[wifi] disconnected");
        } else if (state().ui.gSerialLine == "o") {
            Serial.printf("[outbox] %u items\n", static_cast<unsigned>(state().outbox.gOutbox.size()));
            for (const kk::OutboxItem& it : state().outbox.gOutbox)
                Serial.printf("[outbox] seq %u assignment %u state %u attempts %u http %u\n",
                              static_cast<unsigned>(it.seq),
                              static_cast<unsigned>(it.assignmentId),
                              static_cast<unsigned>(it.state), static_cast<unsigned>(it.attempts),
                              static_cast<unsigned>(it.httpCode));
        } else if (state().ui.gSerialLine == "f") {
            if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Outbox: no Wi-Fi");
            } else {
                flushOutbox();
            }
        } else if (state().ui.gSerialLine == "x") {
            const uint32_t seq = kk::outboxAdd(state().outbox.gOutbox, 999999999, 1, 0, 0,
                                               static_cast<uint32_t>(time(nullptr)));
            if (seq) {
                saveOutbox();
                Serial.printf("[outbox] synthetic seq %u queued\n", static_cast<unsigned>(seq));
            }
        } else if (state().ui.gSerialLine == "z") {
            state().outbox.gOutbox.clear();
            clearOutboxAuthIssue();
            saveOutbox();
            Serial.println("[outbox] cleared");
        } else if (state().ui.gSerialLine == "l") {
            if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Lesson: no Wi-Fi");
            } else {
                startLessons();
            }
        } else if (state().ui.gSerialLine == "g") {
            if (kk::screenBase(state().screen) == Screen::LessonCard) {
                KeyEvent ev;
                ev.down = true;
                ev.key = Key::Right;
                lessonKey(ev);
                state().display.gDirty = true;
            } else if (state().review.detail) {
                if (state().review.detailPage + 1 < state().review.detailPages) ++state().review.detailPage;
                state().display.gDirty = true;
            }
        } else if (state().ui.gSerialLine == "<") {
            if (kk::screenBase(state().screen) == Screen::LessonCard) {
                KeyEvent ev;
                ev.down = true;
                ev.key = Key::Left;
                lessonKey(ev);
                state().display.gDirty = true;
            }
        } else if (state().ui.gSerialLine == "j") {
            // Mirror Space on the lesson list: toggle the current row.
            if (kk::screenBase(state().screen) == Screen::LessonList &&
                !state().lesson.startConfirm) {
                KeyEvent ev;
                ev.down = true;
                ev.key = Key::Other;
                ev.x = 13;
                ev.y = 3;
                lessonListKey(ev);
                state().display.gDirty = true;
            }
        } else if (state().ui.gSerialLine == "D") {
            if (kk::screenBase(state().screen) == Screen::Review && state().review.feedback) {
                const kk::Subject* detail = subjectById(state().review.feedbackSubjectId);
                state().review.detailPages = detail ? detailPageCount(*detail) : 5;
                state().review.detail = !state().review.detail;
                state().review.detailPage = 0;
                state().display.gDirty = true;
            }
        } else if (state().ui.gSerialLine == "Q") {
            if (kk::screenBase(state().screen) == Screen::LessonCard) {
                startLessonQuiz();
            } else {
                Serial.println("[lesson] open a lesson card first");
            }
        } else if (state().ui.gSerialLine == "v") {
            if (state().cache.gBatch.assignments.empty() && !state().cache.gBatchEvicted && !kk::DevWifi::connected() &&
                !kk::DevWifi::connect(30000)) {
                snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Review: no Wi-Fi");
            } else {
                startReview();
            }
        } else if (state().ui.gSerialLine == "e") {
            KeyEvent ev;
            ev.down = true;
            ev.key = Key::Enter;
            const Screen base = kk::screenBase(state().screen);
            if (base == Screen::LessonList && state().lesson.startConfirm) {
                lessonStartKey(ev);
            } else if (base == Screen::LessonList) {
                lessonListKey(ev);
            } else if (base == Screen::LessonQuiz && state().lesson.quizBatch) {
                lessonBatchQuizKey(ev);
            } else if (base == Screen::LessonQuiz) {
                if (state().lesson.gLessonQuiz.complete()) {
                    state().screen.screen = Screen::LessonCard;
                } else if (state().lesson.gLessonQuiz.feedback()) {
                    state().lesson.gLessonQuiz.advance();
                    clearLessonQuizInput();
                } else {
                    checkLessonQuiz();
                }
                state().display.gDirty = true;
            } else if (base == Screen::LessonCard) {
                lessonKey(ev);
            } else if (base == Screen::Review) {
                if (state().review.feedback) advanceFeedback();
                else checkCurrent();
            }
        } else if (state().ui.gSerialLine == "q") {
            const Screen base = kk::screenBase(state().screen);
            if (base == Screen::Firmware) {
                // Same return path as Esc on the firmware screen: Settings at
                // the firmware row, so the serial back command matches the UI.
                state().screen = state().ota.firmwareReturn;
                if (kk::screenTop(state().screen) != Modal::Settings) {
                    state().screen.screen = Screen::Home;
                    state().screen.depth = 0;
                    kk::screenPush(&state().screen, Modal::Settings);
                }
                settingsSelectEntry("system.firmware");
                if (kk::screenTop(state().screen) == Modal::FirmwareConfirm)
                    kk::screenPop(&state().screen);
                state().display.gDirty = true;
            } else if (kk::screenTop(state().screen) == Modal::Settings) {
                closeSettings();
            } else if (kk::screenTop(state().screen) == Modal::About ||
                       kk::screenTop(state().screen) == Modal::SignOut) {
                // The old serial back path closed Settings while About or the
                // sign-out confirmation was on top.
                kk::screenPop(&state().screen);
                closeSettings();
            } else if (base == Screen::LessonEmpty) {
                state().screen.screen = Screen::Home;
                releaseLessonData();
                state().display.gDirty = true;
            } else if (base == Screen::LessonList && !state().lesson.startConfirm) {
                state().screen.screen = Screen::Home;
                releaseLessonData();
                state().display.gDirty = true;
            } else if (base == Screen::LessonList && state().lesson.startConfirm) {
                KeyEvent ev;
                ev.down = true;
                ev.key = Key::Esc;
                lessonStartKey(ev);
            } else if (base == Screen::LessonQuiz && state().lesson.quizBatch) {
                KeyEvent ev;
                ev.down = true;
                ev.key = Key::Esc;
                lessonBatchQuizKey(ev);
            } else if (base == Screen::LessonQuiz) {
                state().screen.screen = Screen::LessonCard;
                state().display.gDirty = true;
            } else if (base == Screen::LessonCard) {
                KeyEvent ev;
                ev.down = true;
                ev.key = Key::Esc;
                lessonKey(ev);
            } else if (base == Screen::Review) {
                endReview();
            }
        } else if (state().ui.gSerialLine.startsWith("y ")) {
            const String text = state().ui.gSerialLine.substring(2);
            if (kk::screenBase(state().screen) == Screen::LessonQuiz &&
                state().lesson.quizBatch && !state().lesson.gLessonBatchFeedback) {
                if (state().lesson.gLessonBatchQueue.part() == kk::ReviewPart::Meaning)
                    appendAnswerText(state().lesson.gLessonBatchInput, text);
                else
                    appendAnswerText(state().lesson.gLessonBatchDirectReading, text);
                state().display.gDirty = true;
            } else if (kk::screenBase(state().screen) == Screen::LessonQuiz &&
                       !state().lesson.quizBatch && !state().lesson.gLessonQuiz.feedback()) {
                if (state().lesson.gLessonQuiz.part() == kk::AnswerKind::Meaning)
                    appendAnswerText(state().lesson.gLessonQuizInput, text);
                else
                    appendAnswerText(state().lesson.gLessonQuizDirectReading, text);
                state().display.gDirty = true;
            } else if (kk::screenBase(state().screen) == Screen::Review && !state().review.feedback) {
                if (state().review.gQueue.part() == kk::ReviewPart::Meaning)
                    appendAnswerText(state().review.gMeaningInput, text);
                else
                    appendAnswerText(state().review.gDirectReading, text);
                state().display.gDirty = true;
            }
        } else if (state().ui.gSerialLine == "b") {
            if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Batch: no Wi-Fi");
            } else {
                runBatch();
            }
        } else if (state().ui.gSerialLine == "m") {
            if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                snprintf(state().ui.gNetLine, sizeof state().ui.gNetLine, "Summary: no Wi-Fi");
            } else {
                runSummary();
            }
        } else if (state().ui.gSerialLine == "P") {
            startSetupPortal();
        } else if (state().ui.gSerialLine.startsWith("U ")) {
            const String url = state().ui.gSerialLine.substring(2);
            if (kk::ota::setManifestUrl(url))
                Serial.printf("[ota] manifest URL %s\n", kk::ota::manifestUrl().c_str());
            else
                Serial.println("[ota] manifest URL not saved");
        } else if (state().ui.gSerialLine == "F") {
            firmwareCheck(true);
        } else if (state().ui.gSerialLine == "I") {
            startFirmwareInstall();
        } else if (state().ui.gSerialLine == "V") {
            // A portal, sync or outbox screen owns device state; opening the
            // firmware screen over it would abandon that flow.
            const Screen base = kk::screenBase(state().screen);
            if (base == Screen::Setup || base == Screen::Sync || base == Screen::Outbox) {
                Serial.println("[ota] firmware screen unavailable now");
            } else {
                openFirmware();
            }
        } else if (state().ui.gSerialLine == "s") {
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
        } else if (state().ui.gSerialLine == "C") {
            seedDevCache();
        } else if (state().ui.gSerialLine == "X") {
            clearDevCache();
        } else if (state().ui.gSerialLine == "J") {
            seedDevRejectedOutbox();
        } else if (state().ui.gSerialLine == "T") {
            if (kk::DevToken::save("dev-token")) {
                Serial.println("[auth] dev token saved (no probe)");
                state().session.gHaveUser = false;
            } else {
                Serial.println("[auth] dev token save failed");
            }
        } else if (state().ui.gSerialLine == "L") {
            seedDevLesson();
        } else if (state().ui.gSerialLine == "E") {
            const uint32_t now = static_cast<uint32_t>(time(nullptr));
            for (kk::SummaryBucket& bucket : state().session.gSummary.reviews)
                bucket.availableAt = now + 3600;
            state().session.gDueReviews = 0;
            if (kk::screenBase(state().screen) == Screen::Boot)
                state().screen.screen = Screen::Home;
            state().display.gDirty = true;
            Serial.println("[summary] dev due cleared");
        } else if (state().ui.gSerialLine == "H") {
            // Failed-fetch path: keep the summary but drop the cached batch and
            // add one lesson id, so Enter/l with Wi-Fi off reaches the notices.
            state().lesson.gLessonBatch = kk::LessonBatch{};
            state().lesson.gLessonComponents.clear();
            state().cache.gBatch = kk::ReviewBatch{};
            state().cache.gBatchEvicted = false;
            state().cache.gCachedBatchCount = 0;
            if (state().storage.gSdOk) kk::cacheClearSubjects(SD);
            state().session.gSummary.lessons.clear();
            kk::SummaryBucket lesson;
            lesson.availableAt = static_cast<uint32_t>(time(nullptr));
            lesson.count = 1;
            lesson.subjectIds = {1};
            state().session.gSummary.lessons.push_back(lesson);
            state().session.gLessons = 1;
            if (kk::screenBase(state().screen) == Screen::Boot)
                state().screen.screen = Screen::Home;
            state().display.gDirty = true;
            Serial.println("[dev] cached batch dropped, one lesson bucket seeded");
        } else if (state().ui.gSerialLine.startsWith("A ")) {
            state().lesson.gDevLessonStartCode = state().ui.gSerialLine.substring(2).toInt();
            Serial.printf("[lesson] fake start code %d\n", state().lesson.gDevLessonStartCode);
        } else if (state().ui.gSerialLine == "A") {
            state().lesson.gDevLessonStartCode = -1;
            Serial.println("[lesson] fake start code off");
        } else if (state().ui.gSerialLine == "B") {
            if (!state().storage.gSdOk) {
                Serial.println("[pack] no SD");
            } else {
                kk::GlyphPack bad;
                const bool opened =
                    bad.open(SD, "/kanikey/glyphs-bad.kgp", kk::PackRole::Font, true);
                Serial.printf("[pack] bad-crc test opened=%d status=%s\n", opened ? 1 : 0,
                              bad.statusLine());
                state().storage.gFontPack.close();
                reopenPacksAfterUpload();
                Serial.printf("[pack] restored: %s\n",
                              state().storage.gFontPack.statusLine());
            }
        } else if (state().ui.gSerialLine == "O") {
            state().storage.gDevForceBattery = !state().storage.gDevForceBattery;
            Serial.printf("[power] force battery %d\n", state().storage.gDevForceBattery ? 1 : 0);
        } else if (state().ui.gSerialLine == "Z") {
            // Hardware spike (SPEED-06): enter light sleep on demand. A 10 s
            // timer bounds the test; a key or G0 wakes it sooner. rtcMicros()
            // keeps counting through light sleep, so the difference is the
            // time actually slept. USB power must be forced to battery first:
            // light sleep drops the USB CDC and can strand the serial session.
            if (!state().storage.gDevForceBattery && tgc::board::externalPower()) {
                Serial.println("[power] light-sleep test refuses on USB; use O first");
            } else {
                Serial.println("[power] light-sleep test: 10 s timer, press a key or G0");
                const uint64_t startedUs = tgc::board::rtcMicros();
                const bool keyWake = tgc::board::lightSleep(10000);
                const uint64_t sleptUs = tgc::board::rtcMicros() - startedUs;
                Serial.printf("[power] light-sleep test: %s wake after %llu us (timer %d)\n",
                              keyWake ? "key" : "timer",
                              static_cast<unsigned long long>(sleptUs),
                              tgc::board::wokeFromTimer() ? 1 : 0);
                state().ui.gLastInputAt = millis();
                state().display.gDirty = true;
            }
        } else if (state().ui.gSerialLine.startsWith("Z ")) {
            const int seconds = state().ui.gSerialLine.substring(2).toInt();
            if (kk::settingsSetSleepSecondsDev(seconds))
                Serial.printf("[settings] sleep %d s\n", kk::settingsSleepSeconds());
            else
                Serial.println("[settings] sleep value rejected");
        } else if (state().ui.gSerialLine.startsWith("G ")) {
            const int count = state().ui.gSerialLine.substring(2).toInt();
            state().lesson.gDevKeyTest = true;
            for (int i = 0; i < count; ++i) {
                KeyEvent e;
                e.key = Key::Other;
                e.x = 255;
                e.y = 255;
                e.down = (i % 2) == 0;
                tgc::board::devPushKeyEvent(e);
            }
            Serial.printf("[key] pushed %d events\n", count);
        } else if (state().ui.gSerialLine == "n") {
            const bool played = tgc::board::playTone(880, 150);
            Serial.printf("[audio] test tone %s\n", played ? "played" : "unavailable");
        } else if (state().ui.gSerialLine == "a") {
            if (!state().storage.gSdOk || !kk::audioCacheOpen(SD, currentAccountKey())) {
                Serial.println("[audio] cache unavailable");
            } else {
                const std::vector<kk::Subject>& subjects =
                    !state().cache.gBatch.subjects.empty() ? state().cache.gBatch.subjects : state().lesson.gLessonBatch.subjects;
                const kk::AudioPrefetchStats stats =
                    kk::audioCachePrefetch(SD, subjects, 0, 8000, 32768);
                Serial.printf("[audio] cache %s, %u clips, %u bytes\n",
                              stats.downloaded ? "updated" : "unchanged",
                              static_cast<unsigned>(kk::audioCacheCount()),
                              static_cast<unsigned>(kk::audioCacheBytes()));
            }
        } else if (state().ui.gSerialLine == "Y") {
            state().ota.gOtaState.failedVersion.clear();
            kk::ota::saveFailedVersion("");
            kk::ota::clearRollbackRecord();
            firmwareNote("Rollback record cleared", kOk);
            Serial.println("[ota] rollback record cleared");
        } else if (state().ui.gSerialLine == "cap" || state().ui.gSerialLine.startsWith("cap ")) {
            // FIX-14 hook: `cap <bytes>` makes WkClient treat a body as truncated
            // after <bytes> (0 = off); `cap` alone prints it. RAM only.
            String arg = state().ui.gSerialLine.substring(3);
            arg.trim();
            if (arg.isEmpty()) {
                Serial.printf("[dev] body cap %u (0 = off, buffer 32768)\n",
                              static_cast<unsigned>(kk::wkDevBodyCap()));
            } else {
                const int32_t bytes = arg.toInt();
                if (bytes < 0 || bytes > 1000000 || String(bytes) != arg) {
                    Serial.println("[dev] use: cap <bytes>  (0 = off)");
                } else {
                    kk::wkDevSetBodyCap(static_cast<uint32_t>(bytes));
                    Serial.printf("[dev] body cap %d (0 = off)\n", static_cast<int>(bytes));
                }
            }
        } else if (state().ui.gSerialLine == "devhost" || state().ui.gSerialLine.startsWith("devhost ")) {
            // FIX-06 hook: `devhost <host> <port>` sends every WkClient request to
            // that host with certificate checks off; `devhost off` restores
            // WaniKani; `devhost` alone prints. RAM only.
            String arg = state().ui.gSerialLine.substring(7);
            arg.trim();
            if (arg.isEmpty()) {
                if (kk::wkDevHostActive())
                    Serial.printf("[dev] devhost %s:%u (active)\n", kk::wkDevHostName(),
                                  static_cast<unsigned>(kk::wkDevHostPort()));
                else
                    Serial.println("[dev] devhost off (api.wanikani.com:443)");
            } else if (arg == "off") {
                kk::wkDevClearHost();
                Serial.println("[dev] devhost off (api.wanikani.com:443)");
            } else {
                const int sp = arg.lastIndexOf(' ');
                String host = sp > 0 ? arg.substring(0, sp) : String();
                host.trim();
                const int32_t port = sp > 0 ? arg.substring(sp + 1).toInt() : 0;
                if (kk::wkDevSetHost(std::string_view(host.c_str(), host.length()),
                                     static_cast<uint32_t>(port))) {
                    Serial.printf("[dev] devhost %s:%d: requests go there, certificate check OFF "
                                  "(RAM only, reboot clears)\n",
                                  host.c_str(), static_cast<int>(port));
                } else {
                    Serial.println("[dev] devhost refused: use <host> <port>; host 1-63 letters, "
                                   "digits, '.', '-', not WaniKani; port 1-65535");
                }
            }
#ifdef KANIKEY_DEV
        } else if (state().ui.gSerialLine == "fs") {
            // FLASH-01 probe: LittleFS total and used bytes, then every entry of
            // the top-level /state directory with its size. Read-only.
            if (!state().storage.gFsOk) {
                Serial.println("[fs] LittleFS not mounted");
            } else {
                const size_t total = LittleFS.totalBytes();
                const size_t used = LittleFS.usedBytes();
                Serial.printf("[fs] LittleFS total %u B, used %u B, free %u B\n",
                              static_cast<unsigned>(total), static_cast<unsigned>(used),
                              static_cast<unsigned>(used <= total ? total - used : 0));
                File dir = LittleFS.open("/state");
                if (!dir || !dir.isDirectory()) {
                    Serial.println("[fs] /state: missing");
                } else {
                    size_t files = 0;
                    size_t bytes = 0;
                    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
                        if (f.isDirectory()) {
                            Serial.printf("[fs]   %s/ (directory)\n", f.name());
                            continue;
                        }
                        ++files;
                        bytes += f.size();
                        Serial.printf("[fs]   %-24s %8u B\n", f.name(),
                                      static_cast<unsigned>(f.size()));
                    }
                    Serial.printf("[fs] /state: %u files, %u B\n", static_cast<unsigned>(files),
                                  static_cast<unsigned>(bytes));
                }
                dir.close();
            }
#endif
        } else if (state().ui.gSerialLine.startsWith("K ")) {
            injectKeys(state().ui.gSerialLine.substring(2));
        } else if (state().ui.gSerialLine == "N") {
            // Drop the canvas so the next frames take the "drawing directly"
            // path that a fragmented heap forces.
            releaseCanvas();
            state().display.gDirty = true;
            Serial.println("[kk] canvas released; drawing directly");
        } else if (state().ui.gSerialLine == "W") {
            // Debug aid: a loop blocked for 5 s panics with a backtrace.
            // Long requests and the pack CRC trip it too, so arm it only
            // right before the step under test.
            enableLoopWDT();
            Serial.println("[kk] loop watchdog on (5 s)");
        } else if (state().ui.gSerialLine == "rem" || state().ui.gSerialLine.startsWith("rem ")) {
            // REM-01 probe: `rem <s> [n]` sleeps (timer + key wake), `rem` logs
            // the record, `rem tone` measures the tone's heap.
            remSerialCommand(state().ui.gSerialLine);
        } else if (state().ui.gSerialLine == "remind" || state().ui.gSerialLine.startsWith("remind ")) {
            // REM-02 dev check: `remind <s>` arms the reminder timer and sleeps,
            // `remind` logs the RTC history of the runs.
            remindSerialCommand(state().ui.gSerialLine);
        } else if (state().ui.gSerialLine == "cue" || state().ui.gSerialLine.startsWith("cue ")) {
            // REM-03 dev check: `cue <name>` plays one cue, `cue vol <level>` sets its level.
            cueSerialCommand(state().ui.gSerialLine);
        } else if (state().ui.gSerialLine.startsWith("R")) {
            String arg = state().ui.gSerialLine.substring(1);
            arg.trim();
            state().dev.gRadicalTestId = arg.length() ? static_cast<uint32_t>(arg.toInt()) : 1;
            // R is an overlay like Tab: remember the screen underneath so Esc
            // reveals it again, even when Tab's debug screen was on. A portal,
            // sync, outbox or firmware screen owns the device state, so R must
            // not replace it; the old flag was hidden behind those screens too.
            const Screen base = kk::screenBase(state().screen);
            if (base != Screen::Setup && base != Screen::Sync &&
                base != Screen::Outbox && base != Screen::Firmware) {
                state().dev.radicalReturn =
                    base == Screen::Debug ? state().dev.debugReturn : base;
                state().screen.screen = Screen::RadicalTest;
            }
            state().display.gDirty = true;
            Serial.printf("[test] radical %u\n", static_cast<unsigned>(state().dev.gRadicalTestId));
#endif
        } else if (!state().ui.gSerialLine.isEmpty()) {
#ifdef KANIKEY_DEV
            Serial.printf("[kk] commands: w=wifi w <ssid>|<pass> t=tls u=user m=summary b=batch v=review l=lessons g=page-right <=page-left j=lesson-select D=review-detail Q=lesson-quiz y<text> e=enter q=quit d=wifi-off o=outbox f=flush x=test-queue z=clear J=attention-outbox Y=clear-rollback C=dev-cache X=clear-cache K <keys>=type R<id>=radical U <url>=manifest F=firmware-check I=firmware-install V=firmware-screen k <token> c=clear h=heap i=pack S=asset-server p=picture s=scan r=restart up <bytes> <path>=sd-upload T=dev-token L=radical-lesson E=zero-due H=drop-batch A<code>=fake-start B=bad-pack-crc O=force-battery Z=light-sleep-test Z<s>=sleep G<n>=key-storm n=test-tone a=audio-cache rem <s> [n]=timer-wake-probe rem=probe-log rem tone=tone-heap cap <bytes>=body-cap (0 off) devhost <host> <port>|off=send-requests-to-host fs=littlefs-usage remind <s>=reminder-wake remind=reminder-log cue <chime|correct|wrong|end>=sound-cue cue vol <low|med|high>=cue-level\n");
#else
            Serial.printf("[kk] commands: h=heap i=pack S=asset-server p=picture r=restart n=test-tone a=audio-cache up <bytes> <path>=sd-upload\n");
#endif
        }
        state().ui.gSerialLine = "";
        state().display.gDirty = true;
    }
}

}  // namespace kk::app

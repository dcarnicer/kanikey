#include "platform/WkLevelProgress.h"

#include "platform/WkClient.h"
#include "platform/WkJson.h"
#include "wk/Json.h"
#include "wk/SrsCounts.h"

namespace kk {

namespace {
bool fetchCount(const String& token, const String& path, uint16_t* out) {
    const WkJsonResult r = wkGetJsonRetry(path, token, 4096, 12000);
    if (r.http.httpCode != 200) {
        Serial.printf("[level] %s -> HTTP %d\n", path.c_str(), r.http.httpCode);
        return false;
    }
    if (!r.parsed) {
        Serial.printf("[level] JSON failed: len=%u trunc=%d\n",
                      static_cast<unsigned>(r.http.body.size()), r.http.truncated ? 1 : 0);
        return false;
    }
    uint32_t count = 0;
    if (!parseCollectionTotal(r.json, &count)) {
        Serial.printf("[level] %s has no total_count\n", path.c_str());
        return false;
    }
    *out = static_cast<uint16_t>(count > UINT16_MAX ? UINT16_MAX : count);
    return true;
}
}  // namespace

LevelProgress fetchLevelProgress(const String& token, uint8_t currentLevel) {
    LevelProgress progress;
    const WkJsonResult r = wkGetJsonRetry("/v2/level_progressions", token, 32768, 12000);
    if (r.http.httpCode != 200) {
        Serial.printf("[level] HTTP %d\n", r.http.httpCode);
        if (r.http.httpCode == 401 || r.http.httpCode == 403)
            Serial.println("[level] continuing without level progress");
        return progress;
    }
    if (!r.parsed) {
        Serial.printf("[level] JSON failed: len=%u trunc=%d\n",
                      static_cast<unsigned>(r.http.body.size()), r.http.truncated ? 1 : 0);
        return progress;
    }
    if (!parseLevelProgressions(r.json, currentLevel, &progress)) {
        Serial.println("[level] no matching progression");
        return progress;
    }

    // Two tiny collection requests read only total_count: every kanji in the
    // level, and those that reached Guru (stages 5-9). A failure keeps the
    // timestamps but leaves both counts at 0.
    String totalPath = "/v2/subjects?levels=";
    totalPath += String(static_cast<unsigned>(progress.level));
    totalPath += "&types=kanji&hidden=false&page_after_id=999999999";
    String passedPath = "/v2/assignments?levels=";
    passedPath += String(static_cast<unsigned>(progress.level));
    passedPath += "&subject_types=kanji&srs_stages=5,6,7,8,9&hidden=false&page_after_id=999999999";
    uint16_t total = 0;
    uint16_t passed = 0;
    if (fetchCount(token, totalPath, &total) && fetchCount(token, passedPath, &passed)) {
        progress.kanjiTotal = total;
        progress.kanjiPassed = passed;
    } else {
        Serial.println("[level] kanji counts unavailable; keeping timestamps only");
    }

    Serial.printf("[level] %u unlocked %u, started %u, passed %u, completed %u, kanji %u/%u\n",
                  static_cast<unsigned>(progress.level),
                  static_cast<unsigned>(progress.unlockedAt),
                  static_cast<unsigned>(progress.startedAt),
                  static_cast<unsigned>(progress.passedAt),
                  static_cast<unsigned>(progress.completedAt),
                  static_cast<unsigned>(progress.kanjiPassed),
                  static_cast<unsigned>(progress.kanjiTotal));
    return progress;
}

}  // namespace kk

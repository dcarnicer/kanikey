#include "platform/WkLevelProgress.h"

#include "platform/WkClient.h"
#include "platform/WkJson.h"
#include "wk/Json.h"

namespace kk {

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
    Serial.printf("[level] %u unlocked %u, started %u, passed %u, completed %u\n",
                  static_cast<unsigned>(progress.level),
                  static_cast<unsigned>(progress.unlockedAt),
                  static_cast<unsigned>(progress.startedAt),
                  static_cast<unsigned>(progress.passedAt),
                  static_cast<unsigned>(progress.completedAt));
    return progress;
}

}  // namespace kk

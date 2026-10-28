#include "platform/WkLevelProgress.h"

#include <string_view>

#include "platform/WkClient.h"
#include "wk/Json.h"

namespace kk {

LevelProgress fetchLevelProgress(const String& token, uint8_t currentLevel) {
    LevelProgress progress;
    const WkResponse r = wkGetRetry("/v2/level_progressions", token, 32768, 12000);
    if (r.httpCode != 200) {
        Serial.printf("[level] HTTP %d\n", r.httpCode);
        if (r.httpCode == 401 || r.httpCode == 403)
            Serial.println("[level] continuing without level progress");
        return progress;
    }
    JsonParser p;
    JsonValue v;
    if (!p.parse(std::string_view(r.body.data(), r.body.size()), &v)) {
        Serial.printf("[level] JSON failed: len=%u trunc=%d\n",
                      static_cast<unsigned>(r.body.size()), r.truncated ? 1 : 0);
        return progress;
    }
    if (!parseLevelProgressions(v, currentLevel, &progress)) {
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

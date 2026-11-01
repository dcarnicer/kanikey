#include "wk/LevelProgress.h"

#include "wk/JsonStream.h"
#include "wk/Time.h"

namespace kk {

namespace {
// The collection is an array of level_progression objects; each one has a
// nested "data" object with the fields below. Only the entry for the current
// level is copied out, so a 60-level body costs the same as a 1-level body.
class ProgressSink : public JsonSink {
public:
    explicit ProgressSink(uint8_t currentLevel) : currentLevel_(currentLevel) {}

    const LevelProgress& found() const { return found_; }
    const LevelProgress& highest() const { return highest_; }

    bool onObjectStart() override {
        if (depth_ == 2 && inData_) {
            current_ = LevelProgress{};
            active_ = true;
        } else if (depth_ == 3 && active_ && pending_ == Key::Data) {
            inEntryData_ = true;
        }
        pending_ = Key::None;
        ++depth_;
        return true;
    }

    bool onObjectEnd() override {
        if (depth_ == 4 && inEntryData_) {
            inEntryData_ = false;
        } else if (depth_ == 3 && active_) {
            if (current_.valid) {
                if (current_.level == currentLevel_) found_ = current_;
                if (!highest_.valid || current_.level > highest_.level) highest_ = current_;
            }
            active_ = false;
            inEntryData_ = false;
        }
        --depth_;
        return true;
    }

    bool onArrayStart() override {
        if (depth_ == 1 && pending_ == Key::Data) inData_ = true;
        pending_ = Key::None;
        ++depth_;
        return true;
    }

    bool onArrayEnd() override {
        if (depth_ == 2 && inData_) inData_ = false;
        --depth_;
        return true;
    }

    bool onKey(std::string_view key) override {
        pending_ = Key::None;
        if (depth_ == 1) {
            if (key == "data") pending_ = Key::Data;
        } else if (depth_ == 3 && active_ && !inEntryData_) {
            if (key == "data") pending_ = Key::Data;
        } else if (depth_ == 4 && inEntryData_) {
            if (key == "level")
                pending_ = Key::Level;
            else if (key == "unlocked_at")
                pending_ = Key::UnlockedAt;
            else if (key == "started_at")
                pending_ = Key::StartedAt;
            else if (key == "passed_at")
                pending_ = Key::PassedAt;
            else if (key == "completed_at")
                pending_ = Key::CompletedAt;
        }
        return true;
    }

    bool onString(std::string_view value) override {
        if (depth_ == 4 && inEntryData_) {
            if (pending_ == Key::UnlockedAt)
                parseIso8601(value, &current_.unlockedAt);
            else if (pending_ == Key::StartedAt)
                parseIso8601(value, &current_.startedAt);
            else if (pending_ == Key::PassedAt)
                parseIso8601(value, &current_.passedAt);
            else if (pending_ == Key::CompletedAt)
                parseIso8601(value, &current_.completedAt);
        }
        pending_ = Key::None;
        return true;
    }

    bool onNumber(double value) override {
        if (depth_ == 4 && inEntryData_ && pending_ == Key::Level) {
            if (value > 0) {
                const double capped = value > 255 ? 255 : value;
                current_.level = static_cast<uint8_t>(capped);
                current_.valid = true;
            }
        }
        pending_ = Key::None;
        return true;
    }

    bool onBool(bool) override {
        pending_ = Key::None;
        return true;
    }

    bool onNull() override {
        pending_ = Key::None;
        return true;
    }

private:
    enum class Key : uint8_t {
        None,
        Data,
        Level,
        UnlockedAt,
        StartedAt,
        PassedAt,
        CompletedAt,
    };

    uint8_t currentLevel_;
    LevelProgress current_;
    LevelProgress found_;
    LevelProgress highest_;
    Key pending_ = Key::None;
    int depth_ = 0;
    bool inData_ = false;
    bool inEntryData_ = false;
    bool active_ = false;
};
}  // namespace

bool parseLevelProgressionsText(std::string_view body, uint8_t currentLevel, LevelProgress* out) {
    if (!out) return false;
    *out = LevelProgress{};
    ProgressSink sink(currentLevel);
    JsonStreamParser parser(&sink);
    if (!parser.parse(body)) return false;
    // As before the streaming parser: the current level's entry, or the highest
    // level in the response when the current one is missing.
    *out = sink.found().valid ? sink.found() : sink.highest();
    return true;
}

uint8_t levelKanjiPercent(uint16_t passed, uint16_t total) {
    if (total == 0) return 0;
    const uint32_t percent = (static_cast<uint32_t>(passed) * 100u + total / 2u) / total;
    return static_cast<uint8_t>(percent > 100u ? 100u : percent);
}

}  // namespace kk

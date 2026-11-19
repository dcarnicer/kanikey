#include "wk/AnswerDisplay.h"

#include "wk/Text.h"

namespace kk {
namespace {

// On'yomi first, then kun'yomi, then nanori; unknown types sort last.
int readingRank(ReadingType type) {
    switch (type) {
        case ReadingType::Onyomi: return 0;
        case ReadingType::Kunyomi: return 1;
        case ReadingType::Nanori: return 2;
        default: return 3;
    }
}

// WaniKani writes okurigana as "た.べる"; the dot is notation, not a glyph the
// pack can draw.
std::string displayReading(const Reading& r) {
    return stripReadingMarks(r.text);
}

}  // namespace

std::vector<std::string> acceptedMeaningTokens(const Subject& s) {
    std::vector<std::string> out;
    for (const Meaning& m : s.meanings)
        if (m.accepted && !m.text.empty()) out.push_back(m.text);
    return out;
}

std::vector<std::string> acceptedReadingTokens(const Subject& s) {
    std::vector<std::string> out;
    bool hasTyped = false;
    for (const Reading& r : s.readings)
        if (r.accepted && r.type != ReadingType::Unknown) hasTyped = true;

    if (s.type != SubjectType::Kanji || !hasTyped) {
        for (const Reading& r : s.readings) {
            if (!r.accepted) continue;
            const std::string text = displayReading(r);
            if (!text.empty()) out.push_back(text);
        }
        return out;
    }

    // Walk the WaniKani order once per type so readings inside a group keep
    // their original order. Unknown readings form one last flat group.
    for (int rank = 0; rank < 4; ++rank) {
        bool group = false;
        for (const Reading& r : s.readings) {
            if (!r.accepted || readingRank(r.type) != rank) continue;
            const std::string text = displayReading(r);
            if (text.empty()) continue;
            if (!group && !out.empty()) out.emplace_back();  // group break
            group = true;
            out.push_back(text);
        }
    }
    return out;
}

}  // namespace kk

#include "wk/AnswerChecker.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "wk/RomajiIme.h"
#include "wk/Text.h"

namespace kk {

namespace {

bool isAccepted(const Meaning& m, bool anyAccepted) {
    return anyAccepted ? m.accepted : true;
}

bool isAccepted(const Reading& r, bool anyAccepted) {
    return anyAccepted ? r.accepted : true;
}

bool anyAcceptedMeaning(const Subject& s) {
    return std::any_of(s.meanings.begin(), s.meanings.end(),
                       [](const Meaning& m) { return m.accepted; });
}

bool anyAcceptedReading(const Subject& s) {
    return std::any_of(s.readings.begin(), s.readings.end(),
                       [](const Reading& r) { return r.accepted; });
}

std::string normalizedReading(std::string_view input) {
    std::string s = trim(input);
    // If the user typed romaji, convert it. Kana input passes through. The table
    // is lowercase-only, so lowercase ASCII first: "Ichi" is いち, not Iち.
    bool hasAsciiLetter = false;
    for (char c : s)
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) hasAsciiLetter = true;
    if (hasAsciiLetter) s = romajiToHiragana(lowerAscii(s));
    s = toHiragana(s);
    s = stripReadingMarks(s);
    return s;
}

bool matchesMeaning(const Subject& s, const std::string& normalized) {
    const bool any = anyAcceptedMeaning(s);
    for (const Meaning& m : s.meanings)
        if (isAccepted(m, any) && normalizeMeaning(m.text) == normalized) return true;
    for (const std::string& m : s.meaningSynonyms)
        if (normalizeMeaning(m) == normalized) return true;
    for (const std::string& m : s.whitelist)
        if (normalizeMeaning(m) == normalized) return true;
    return false;
}

bool matchesBlacklist(const Subject& s, const std::string& normalized) {
    for (const std::string& m : s.blacklist)
        if (normalizeMeaning(m) == normalized) return true;
    return false;
}

bool closeMeaning(const Subject& s, const std::string& normalized, std::string* closest) {
    const bool any = anyAcceptedMeaning(s);
    int best = 1000;
    std::string bestText;
    auto consider = [&](const std::string& text) {
        const std::string n = normalizeMeaning(text);
        if (n.empty()) return;
        const int d = levenshtein(normalized, n);
        // The allowance belongs to the expected answer, not to the typed text:
        // a long wrong answer must not become "close" to a short one.
        if (d > typoTolerance(n.size())) return;
        if (d < best) {
            best = d;
            bestText = text;
        }
    };
    for (const Meaning& m : s.meanings)
        if (isAccepted(m, any)) consider(m.text);
    for (const std::string& m : s.meaningSynonyms) consider(m);
    for (const std::string& m : s.whitelist) consider(m);
    if (best == 1000) return false;
    if (closest) *closest = bestText;
    return true;
}

bool matchesCharacters(const Subject& s, std::string_view input) {
    if (s.characters.empty()) return false;
    std::string a = trim(input);
    std::string b = trim(s.characters);
    if (a == b) return true;
    // WaniKani sometimes leads with a wave dash (〜中).
    auto stripWave = [](std::string& x) {
        if (x.rfind("〜", 0) == 0) x.erase(0, 3);
    };
    stripWave(a);
    stripWave(b);
    return !a.empty() && a == b;
}

bool matchesReading(const Subject& s, const std::string& normalized, std::string* matched,
                    bool* primary, bool* stemOnly) {
    const bool any = anyAcceptedReading(s);
    for (const Reading& r : s.readings) {
        if (!isAccepted(r, any)) continue;
        const std::string full = stripReadingMarks(toHiragana(r.text));
        if (full == normalized) {
            if (matched) *matched = full;
            if (primary) *primary = r.primary;
            if (stemOnly) *stemOnly = false;
            return true;
        }
        if (r.text.find('.') != std::string::npos) {
            const std::string stem = readingStem(toHiragana(r.text));
            if (!stem.empty() && stem == normalized) {
                if (matched) *matched = stem;
                if (primary) *primary = r.primary;
                if (stemOnly) *stemOnly = true;
                return true;
            }
        }
    }
    return false;
}

// A reading that is on the subject but not accepted. `matchesReading()` already
// rejected the accepted ones, so only the accepted=false entries are checked.
bool matchesRejectedReading(const Subject& s, const std::string& normalized) {
    const bool any = anyAcceptedReading(s);
    for (const Reading& r : s.readings) {
        if (isAccepted(r, any)) continue;
        if (stripReadingMarks(toHiragana(r.text)) == normalized) return true;
    }
    return false;
}

// The type WaniKani wants: the single known type shared by the accepted
// readings. Unknown when the item accepts several types (or none is typed).
ReadingType wantedReadingType(const Subject& s) {
    const bool any = anyAcceptedReading(s);
    ReadingType type = ReadingType::Unknown;
    for (const Reading& r : s.readings) {
        if (!isAccepted(r, any) || r.type == ReadingType::Unknown) continue;
        if (type == ReadingType::Unknown) {
            type = r.type;
        } else if (type != r.type) {
            return ReadingType::Unknown;
        }
    }
    return type;
}

std::string readingTypeHint(ReadingType type) {
    switch (type) {
        case ReadingType::Onyomi: return "WaniKani wants the on'yomi";
        case ReadingType::Kunyomi: return "WaniKani wants the kun'yomi";
        case ReadingType::Nanori: return "WaniKani wants the nanori";
        default: return "That reading is not accepted for this item";
    }
}

// The IME reads a romaji "n" before a vowel as な/に/… (konya → こにゃ, like
// WanaKana), while the reading usually needs ん (こんや). Try the ん + vowel
// spelling at each n-row kana: one variant per kana, a second for に + small
// ya/yu/yo.
bool nHintMatches(const std::string& input, const Subject& s) {
    static const struct {
        uint32_t first;
        uint32_t second;  // 0 = single kana
        const char* fixed;
    } kFixes[] = {
        {0x306B, 0x3083, "んや"},  // にゃ -> んや
        {0x306B, 0x3085, "んゆ"},  // にゅ -> んゆ
        {0x306B, 0x3087, "んよ"},  // にょ -> んよ
        {0x306A, 0, "んあ"},       // な -> んあ
        {0x306B, 0, "んい"},       // に -> んい
        {0x306C, 0, "んう"},       // ぬ -> んう
        {0x306D, 0, "んえ"},       // ね -> んえ
        {0x306E, 0, "んお"},       // の -> んお
    };
    if (input.size() > 64 * 3) return false;  // readings are short; bound the work
    const char* p = input.data();
    const char* end = p + input.size();
    while (p < end) {
        const char* start = p;
        const uint32_t first = utf8Next(&p);
        if (first == 0) break;
        const char* afterFirst = p;
        uint32_t second = 0;
        if (p < end) second = utf8Next(&p);
        const size_t pos = static_cast<size_t>(start - input.data());
        const size_t firstLen = static_cast<size_t>(afterFirst - start);
        const size_t secondLen = second ? static_cast<size_t>(p - afterFirst) : 0;
        for (const auto& f : kFixes) {
            if (f.first != first) continue;
            if (f.second != 0 && f.second != second) continue;
            const size_t replaceLen = firstLen + (f.second ? secondLen : 0);
            std::string fixed = input.substr(0, pos);
            fixed += f.fixed;
            fixed += input.substr(pos + replaceLen);
            if (matchesReading(s, fixed, nullptr, nullptr, nullptr)) return true;
        }
        p = afterFirst;
    }
    return false;
}

bool matchesMeaningText(const Subject& s, std::string_view input) {
    const std::string normalized = normalizeMeaning(input);
    if (normalized.empty()) return false;
    const bool any = anyAcceptedMeaning(s);
    for (const Meaning& m : s.meanings)
        if (isAccepted(m, any) && normalizeMeaning(m.text) == normalized) return true;
    for (const std::string& m : s.meaningSynonyms)
        if (normalizeMeaning(m) == normalized) return true;
    for (const std::string& m : s.whitelist)
        if (normalizeMeaning(m) == normalized) return true;
    return false;
}

AnswerCheck correct(std::string msg = "Correct") {
    return {AnswerStatus::Correct, std::move(msg), false};
}

AnswerCheck warning(std::string msg, bool close = true) {
    return {AnswerStatus::Warning, std::move(msg), close};
}

AnswerCheck incorrect(std::string msg) {
    return {AnswerStatus::Incorrect, std::move(msg), false};
}

}  // namespace

AnswerCheck checkAnswer(const Subject& subject, AnswerKind kind, std::string_view input,
                        bool strict) {
    const std::string raw = trim(input);
    if (raw.empty()) return incorrect("Type an answer");

    if (kind == AnswerKind::Meaning) {
        const std::string normalized = normalizeMeaning(raw);
        if (normalized.empty()) return incorrect("Type an answer");

        if (matchesBlacklist(subject, normalized))
            return incorrect("That meaning is not accepted for this item");

        if (matchesMeaning(subject, normalized)) return correct();

        std::string closest;
        if (closeMeaning(subject, normalized, &closest)) {
            if (strict)
                return warning("Close. Check the spelling");
            return correct("Correct, with a small typo");
        }

        // A reading where a meaning is expected.
        const std::string asReading = normalizedReading(raw);
        std::string matched;
        bool primary = false, stemOnly = false;
        if (!asReading.empty() && matchesReading(subject, asReading, &matched, &primary, &stemOnly))
            return warning("That is the reading. Type the meaning");

        if (hasJapanese(raw)) return warning("Type the meaning in English");
        return incorrect("Incorrect meaning");
    }

    // Reading.
    if (!subject.needsReading()) return incorrect("This item has no reading question");
    if (matchesCharacters(subject, raw))
        return warning("You typed the characters. Type the reading");

    if (matchesMeaningText(subject, raw))
        return warning("You typed the meaning. Type the reading");

    const std::string normalized = normalizedReading(raw);
    if (normalized.empty()) return incorrect("Type the reading");
    if (!isKanaString(normalized)) return warning("Type the reading in kana");

    bool stemOnly = false;
    if (matchesReading(subject, normalized, nullptr, nullptr, &stemOnly)) {
        if (stemOnly) return warning("Check the okurigana");
        return correct();
    }

    if (nHintMatches(normalized, subject))
        return warning("Use n' before a vowel or y");

    // WaniKani shakes without a penalty for a reading the item lists but does
    // not accept; name the type it wants when the accepted readings share one.
    if (subject.type == SubjectType::Kanji && matchesRejectedReading(subject, normalized))
        return warning(readingTypeHint(wantedReadingType(subject)));

    return incorrect("Incorrect reading");
}

}  // namespace kk

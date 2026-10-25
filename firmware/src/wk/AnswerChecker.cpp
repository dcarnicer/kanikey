#include "wk/AnswerChecker.h"

#include <algorithm>
#include <cstring>
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
    // If the user typed romaji, convert it. Kana input passes through.
    bool hasAsciiLetter = false;
    for (char c : s)
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) hasAsciiLetter = true;
    if (hasAsciiLetter) s = romajiToHiragana(s);
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
    if (normalized.size() < 4) return false;
    const bool close = best <= typoTolerance(normalized.size());
    if (close && closest) *closest = bestText;
    return close;
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

bool nConversionFix(const std::string& input, const Subject& s) {
    static const struct {
        const char* from;
        const char* to;
    } kFixes[] = {
        {"にゃ", "んや"}, {"にゅ", "んゆ"}, {"にょ", "んよ"}, {"にあ", "んあ"},
        {"にい", "んい"}, {"にう", "んう"}, {"にえ", "んえ"}, {"にお", "んお"},
    };
    const bool any = anyAcceptedReading(s);
    for (const auto& f : kFixes) {
        std::string fixed = input;
        size_t pos = 0;
        while ((pos = fixed.find(f.from, pos)) != std::string::npos) {
            fixed.replace(pos, std::strlen(f.from), f.to);
            pos += std::strlen(f.to);
        }
        if (fixed == input) continue;
        for (const Reading& r : s.readings) {
            if (!isAccepted(r, any)) continue;
            if (stripReadingMarks(toHiragana(r.text)) == fixed) return true;
        }
    }
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
                return warning("Close. Check the spelling: " + closest);
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

    std::string matched;
    bool primary = false, stemOnly = false;
    if (matchesReading(subject, normalized, &matched, &primary, &stemOnly)) {
        if (stemOnly) return warning("Check the okurigana");
        if (subject.type == SubjectType::Kanji && !primary && strict)
            return warning("That is a valid reading, but WaniKani wants the primary one");
        return correct();
    }

    if (nConversionFix(normalized, subject))
        return warning("Try nn before a vowel or y");

    return incorrect("Incorrect reading");
}

}  // namespace kk

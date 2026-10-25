#pragma once
// Small UTF-8/text helpers for the pure WaniKani domain modules. No Arduino.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "wk/Utf8.h"

namespace kk {

inline char asciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

inline std::string lowerAscii(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back(asciiLower(c));
    return out;
}

inline bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

inline std::string trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && isSpace(s[b])) ++b;
    while (e > b && isSpace(s[e - 1])) --e;
    return std::string(s.substr(b, e - b));
}

inline std::string collapseSpaces(std::string_view s) {
    std::string out;
    bool space = false;
    for (char c : s) {
        if (isSpace(c)) {
            space = true;
        } else {
            if (space && !out.empty()) out.push_back(' ');
            space = false;
            out.push_back(c);
        }
    }
    return out;
}

inline bool isKanaCp(uint32_t cp) {
    return (cp >= 0x3040 && cp <= 0x309F) || (cp >= 0x30A0 && cp <= 0x30FF);
}

inline bool isKanjiCp(uint32_t cp) {
    return (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) ||
           (cp >= 0xF900 && cp <= 0xFAFF);
}

inline bool isKanaString(std::string_view s) {
    const char* p = s.data();
    const char* end = s.data() + s.size();
    bool any = false;
    while (p < end) {
        uint32_t cp = utf8Next(&p);
        if (cp == 0) break;
        if (!isKanaCp(cp)) return false;
        any = true;
    }
    return any;
}

inline bool hasKanji(std::string_view s) {
    const char* p = s.data();
    const char* end = s.data() + s.size();
    while (p < end) {
        uint32_t cp = utf8Next(&p);
        if (cp == 0) break;
        if (isKanjiCp(cp)) return true;
    }
    return false;
}

inline bool hasJapanese(std::string_view s) {
    const char* p = s.data();
    const char* end = s.data() + s.size();
    while (p < end) {
        uint32_t cp = utf8Next(&p);
        if (cp == 0) break;
        if (isKanaCp(cp) || isKanjiCp(cp)) return true;
    }
    return false;
}

// Katakana -> hiragana; other code points are copied unchanged.
inline std::string toHiragana(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    const char* p = s.data();
    const char* end = s.data() + s.size();
    while (p < end) {
        uint32_t cp = utf8Next(&p);
        if (cp == 0) break;
        if (cp >= 0x30A1 && cp <= 0x30F6) cp -= 0x60;
        char buf[5];
        utf8Encode(cp, buf);
        out += buf;
    }
    return out;
}

// Reading marks: WaniKani writes okurigana as "た.べる"; the dot and the hyphen
// are not part of the answer.
inline std::string stripReadingMarks(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        if (c != '.' && c != '-' && c != ' ' && c != '\t') out.push_back(c);
    return out;
}

// The part before the okurigana dot ("" when there is no dot).
inline std::string readingStem(std::string_view s) {
    const size_t dot = s.find('.');
    return stripReadingMarks(dot == std::string_view::npos ? s : s.substr(0, dot));
}

inline std::string normalizeMeaning(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool space = false;
    for (char c : s) {
        c = asciiLower(c);
        if (c == '-') {
            space = true;
            continue;
        }
        if (c == '.' || c == ',' || c == '!' || c == '?' || c == ';' || c == ':' ||
            c == '\'' || c == '"' || c == '/' || c == '(' || c == ')' || c == '[' ||
            c == ']' || c == '{' || c == '}')
            continue;
        if (isSpace(c)) {
            space = true;
            continue;
        }
        if (space && !out.empty()) out.push_back(' ');
        space = false;
        out.push_back(c);
    }
    return out;
}

inline int levenshtein(std::string_view a, std::string_view b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<int>(j);
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = static_cast<int>(i);
        for (size_t j = 1; j <= b.size(); ++j) {
            const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        prev.swap(cur);
    }
    return prev[b.size()];
}

inline int typoTolerance(size_t length) {
    if (length <= 3) return 0;
    if (length <= 5) return 1;
    if (length <= 8) return 2;
    return 3;
}

}  // namespace kk

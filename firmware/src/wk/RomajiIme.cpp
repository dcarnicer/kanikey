#include "wk/RomajiIme.h"

#include <cstring>

#include "wk/Utf8.h"

namespace kk {

namespace {

struct Pair {
    const char* romaji;
    const char* kana;
};

// Longest match wins, so order is not significant. The table is deliberately
// Hepburn-first (shi, chi, tsu, fu) with the common IME aliases (si, ti, tu,
// hu, sya, zya, jya, cya, xtsu, …) alongside it.
constexpr Pair kTable[] = {
    {"a", "あ"}, {"i", "い"}, {"u", "う"}, {"e", "え"}, {"o", "お"},
    {"ka", "か"}, {"ki", "き"}, {"ku", "く"}, {"ke", "け"}, {"ko", "こ"},
    {"kya", "きゃ"}, {"kyu", "きゅ"}, {"kyo", "きょ"},
    {"sa", "さ"}, {"shi", "し"}, {"si", "し"}, {"su", "す"}, {"se", "せ"}, {"so", "そ"},
    {"sha", "しゃ"}, {"shu", "しゅ"}, {"sho", "しょ"},
    {"sya", "しゃ"}, {"syu", "しゅ"}, {"syo", "しょ"}, {"she", "しぇ"},
    {"ta", "た"}, {"chi", "ち"}, {"ti", "ち"}, {"tsu", "つ"}, {"tu", "つ"}, {"te", "て"},
    {"to", "と"}, {"cha", "ちゃ"}, {"chu", "ちゅ"}, {"cho", "ちょ"},
    {"tya", "ちゃ"}, {"tyu", "ちゅ"}, {"tyo", "ちょ"},
    {"cya", "ちゃ"}, {"cyu", "ちゅ"}, {"cyo", "ちょ"}, {"che", "ちぇ"},
    {"tcha", "っちゃ"}, {"tchi", "っち"}, {"tchu", "っちゅ"}, {"tcho", "っちょ"},
    {"na", "な"}, {"ni", "に"}, {"nu", "ぬ"}, {"ne", "ね"}, {"no", "の"},
    {"nya", "にゃ"}, {"nyu", "にゅ"}, {"nyo", "にょ"},
    {"ha", "は"}, {"hi", "ひ"}, {"fu", "ふ"}, {"hu", "ふ"}, {"he", "へ"}, {"ho", "ほ"},
    {"hya", "ひゃ"}, {"hyu", "ひゅ"}, {"hyo", "ひょ"},
    {"ma", "ま"}, {"mi", "み"}, {"mu", "む"}, {"me", "め"}, {"mo", "も"},
    {"mya", "みゃ"}, {"myu", "みゅ"}, {"myo", "みょ"},
    {"ya", "や"}, {"yu", "ゆ"}, {"yo", "よ"}, {"ye", "いぇ"},
    {"ra", "ら"}, {"ri", "り"}, {"ru", "る"}, {"re", "れ"}, {"ro", "ろ"},
    {"rya", "りゃ"}, {"ryu", "りゅ"}, {"ryo", "りょ"},
    {"wa", "わ"}, {"wo", "を"}, {"wi", "うぃ"}, {"we", "うぇ"},
    {"ga", "が"}, {"gi", "ぎ"}, {"gu", "ぐ"}, {"ge", "げ"}, {"go", "ご"},
    {"gya", "ぎゃ"}, {"gyu", "ぎゅ"}, {"gyo", "ぎょ"},
    {"za", "ざ"}, {"ji", "じ"}, {"zi", "じ"}, {"zu", "ず"}, {"ze", "ぜ"}, {"zo", "ぞ"},
    {"ja", "じゃ"}, {"ju", "じゅ"}, {"jo", "じょ"}, {"je", "じぇ"},
    {"zya", "じゃ"}, {"zyu", "じゅ"}, {"zyo", "じょ"},
    {"jya", "じゃ"}, {"jyu", "じゅ"}, {"jyo", "じょ"},
    {"da", "だ"}, {"di", "ぢ"}, {"du", "づ"}, {"de", "で"}, {"do", "ど"},
    {"dya", "ぢゃ"}, {"dyu", "ぢゅ"}, {"dyo", "ぢょ"}, {"dzu", "づ"},
    {"ba", "ば"}, {"bi", "び"}, {"bu", "ぶ"}, {"be", "べ"}, {"bo", "ぼ"},
    {"bya", "びゃ"}, {"byu", "びゅ"}, {"byo", "びょ"},
    {"pa", "ぱ"}, {"pi", "ぴ"}, {"pu", "ぷ"}, {"pe", "ぺ"}, {"po", "ぽ"},
    {"pya", "ぴゃ"}, {"pyu", "ぴゅ"}, {"pyo", "ぴょ"},
    {"fa", "ふぁ"}, {"fi", "ふぃ"}, {"fe", "ふぇ"}, {"fo", "ふぉ"},
    {"va", "ゔぁ"}, {"vi", "ゔぃ"}, {"vu", "ゔ"}, {"ve", "ゔぇ"}, {"vo", "ゔぉ"},
    {"xa", "ぁ"}, {"xi", "ぃ"}, {"xu", "ぅ"}, {"xe", "ぇ"}, {"xo", "ぉ"},
    {"la", "ぁ"}, {"li", "ぃ"}, {"lu", "ぅ"}, {"le", "ぇ"}, {"lo", "ぉ"},
    {"xya", "ゃ"}, {"xyu", "ゅ"}, {"xyo", "ょ"},
    {"lya", "ゃ"}, {"lyu", "ゅ"}, {"lyo", "ょ"},
    {"xtu", "っ"}, {"ltu", "っ"}, {"xtsu", "っ"}, {"ltsu", "っ"},
    {"xwa", "ゎ"}, {"lwa", "ゎ"},
    {"xka", "ヵ"}, {"xke", "ヶ"},
};

bool isVowel(char c) { return c == 'a' || c == 'i' || c == 'u' || c == 'e' || c == 'o'; }

bool isConsonant(char c) {
    return c >= 'a' && c <= 'z' && !isVowel(c) && c != 'n';
}

// True when `s` is exactly one UTF-8 code point.
bool isOneCodePoint(std::string_view s) {
    if (s.empty()) return false;
    const char* p = s.data();
    utf8Next(&p);
    return p == s.data() + s.size();
}

}  // namespace

// REV-10: delete one committed kana unit instead of the last raw byte. The
// candidate is the longest raw prefix whose conversion is `text()` with its
// last code point removed; 0 (the empty prefix) makes a one-kana answer clear
// completely. This keeps nn, n', nna, kan, kann and tta consistent without a
// second table. Unconverted ASCII still loses one character.
void RomajiIme::backspace() {
    if (raw_.empty()) return;
    const std::string text = romajiToHiragana(raw_);
    size_t cut = raw_.size();
    for (size_t k = 0; k < raw_.size(); ++k) {
        const std::string prefix = romajiToHiragana(std::string_view(raw_).substr(0, k));
        if (prefix.size() >= text.size()) continue;
        if (text.compare(0, prefix.size(), prefix) != 0) continue;
        if (!isOneCodePoint(std::string_view(text).substr(prefix.size()))) continue;
        cut = k;
    }
    if (cut < raw_.size())
        raw_.erase(cut);
    else
        raw_.pop_back();
}

// REV-10: an answer (here its committed kana) never passes `maxBytes`.
bool RomajiIme::feedCapped(char c, size_t maxBytes) {
    RomajiIme next = *this;
    next.feed(c);
    if (next.text().size() > maxBytes) return false;
    raw_.swap(next.raw_);
    return true;
}

PendingRomaji splitPendingRomaji(std::string_view converted) {
    // Kana is multi-byte UTF-8 (every byte >= 0x80), so the trailing bytes
    // below 0x80 can only be raw romaji that no table entry consumed.
    size_t cut = converted.size();
    while (cut > 0 && static_cast<unsigned char>(converted[cut - 1]) < 0x80) --cut;
    PendingRomaji parts;
    parts.kana.assign(converted.substr(0, cut));
    parts.pending.assign(converted.substr(cut));
    return parts;
}

std::string romajiToHiragana(std::string_view raw) {
    std::string out;
    out.reserve(raw.size() * 3);
    size_t i = 0;
    while (i < raw.size()) {
        const char c = raw[i];
        if (c == 'n') {
            if (i + 1 >= raw.size()) {
                out += "ん";
                ++i;
                continue;
            }
            const char next = raw[i + 1];
            if (next == '\'') {
                out += "ん";
                i += 2;
                continue;
            }
            if (next == 'n') {
                // "nna" is んな: release the first n as ん, let the second
                // start the syllable. "nn" alone and "nnk" release both.
                if (i + 2 < raw.size() && (isVowel(raw[i + 2]) || raw[i + 2] == 'y')) {
                    out += "ん";
                    ++i;
                } else {
                    out += "ん";
                    i += 2;
                }
                continue;
            }
            if (!isVowel(next) && next != 'y') {
                out += "ん";
                ++i;
                continue;
            }
            // Otherwise let the table match na/ni/nya/...
        }
        if (c == '-') {
            out += "ー";
            ++i;
            continue;
        }
        if (isConsonant(c) && i + 1 < raw.size() && raw[i + 1] == c) {
            out += "っ";
            // "tta" keeps the second t for た; a trailing pair ("tt") has no
            // vowel to start the next syllable, so it is the whole っ.
            i += (i + 2 >= raw.size()) ? 2 : 1;
            continue;
        }

        const Pair* best = nullptr;
        size_t bestLen = 0;
        for (const Pair& p : kTable) {
            const size_t len = std::strlen(p.romaji);
            if (len <= bestLen || i + len > raw.size()) continue;
            if (raw.compare(i, len, p.romaji) == 0) {
                best = &p;
                bestLen = len;
            }
        }
        if (best) {
            out += best->kana;
            i += bestLen;
            continue;
        }
        if (static_cast<unsigned char>(c) < 0x80) {
            out.push_back(c);
            ++i;
        } else {
            // Preserve kana or other UTF-8 bytes the user pasted.
            out.push_back(c);
            ++i;
        }
    }
    return out;
}

}  // namespace kk

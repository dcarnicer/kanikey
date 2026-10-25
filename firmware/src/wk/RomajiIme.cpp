#include "wk/RomajiIme.h"

#include <cstring>

namespace kk {

namespace {

struct Pair {
    const char* romaji;
    const char* kana;
};

// Longest match wins, so order is not significant. The table is deliberately
// Hepburn-first (shi, chi, tsu, fu) with the common IME aliases (si, ti, tu, hu)
// alongside it.
constexpr Pair kTable[] = {
    {"a", "あ"}, {"i", "い"}, {"u", "う"}, {"e", "え"}, {"o", "お"},
    {"ka", "か"}, {"ki", "き"}, {"ku", "く"}, {"ke", "け"}, {"ko", "こ"},
    {"kya", "きゃ"}, {"kyu", "きゅ"}, {"kyo", "きょ"},
    {"sa", "さ"}, {"shi", "し"}, {"si", "し"}, {"su", "す"}, {"se", "せ"}, {"so", "そ"},
    {"sha", "しゃ"}, {"shu", "しゅ"}, {"sho", "しょ"},
    {"ta", "た"}, {"chi", "ち"}, {"ti", "ち"}, {"tsu", "つ"}, {"tu", "つ"}, {"te", "て"},
    {"to", "と"}, {"cha", "ちゃ"}, {"chu", "ちゅ"}, {"cho", "ちょ"},
    {"tya", "ちゃ"}, {"tyu", "ちゅ"}, {"tyo", "ちょ"},
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
    {"ja", "じゃ"}, {"ju", "じゅ"}, {"jo", "じょ"},
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
    {"xtu", "っ"}, {"ltu", "っ"}, {"xwa", "ゎ"}, {"lwa", "ゎ"},
    {"xka", "ヵ"}, {"xke", "ヶ"},
};

bool isVowel(char c) { return c == 'a' || c == 'i' || c == 'u' || c == 'e' || c == 'o'; }

bool isConsonant(char c) {
    return c >= 'a' && c <= 'z' && !isVowel(c) && c != 'n';
}

}  // namespace

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
            ++i;
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

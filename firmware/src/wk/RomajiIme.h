#pragma once
// Romaji -> hiragana for typed reading answers. Pure C++, no Arduino.
//
// The user types on a US keyboard; text() returns the kana that would be
// committed. The raw romaji is kept for editing and for the dim "romaji: ..."
// line on screen.
#include <string>
#include <string_view>

namespace kk {

std::string romajiToHiragana(std::string_view raw);

class RomajiIme {
public:
    void clear() { raw_.clear(); }
    bool empty() const { return raw_.empty(); }
    void feed(char c) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c >= 0x20 && c < 0x7F) raw_.push_back(c);
    }
    void backspace() {
        if (!raw_.empty()) raw_.pop_back();
    }
    const std::string& raw() const { return raw_; }
    std::string text() const { return romajiToHiragana(raw_); }

private:
    std::string raw_;
};

}  // namespace kk

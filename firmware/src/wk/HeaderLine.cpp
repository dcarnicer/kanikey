#include "wk/HeaderLine.h"

#include <cstdlib>
#include <cstring>

namespace kk {

namespace {

bool isTrimmable(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

}  // namespace

void HeaderLine::clear() {
    len_ = 0;
    truncated_ = false;
    text_[0] = '\0';
}

void HeaderLine::push(char c) {
    if (len_ >= kCapacity) {
        truncated_ = true;
        return;
    }
    text_[len_++] = c;
    text_[len_] = '\0';
}

bool HeaderLine::blank() const {
    return len_ == 0 || (len_ == 1 && text_[0] == '\r');
}

void HeaderLine::trim() {
    size_t begin = 0;
    while (begin < len_ && isTrimmable(text_[begin])) ++begin;
    size_t end = len_;
    while (end > begin && isTrimmable(text_[end - 1])) --end;
    if (begin > 0) std::memmove(text_, text_ + begin, end - begin);
    len_ = end - begin;
    text_[len_] = '\0';
}

void HeaderLine::toLower() {
    for (size_t i = 0; i < len_; ++i) {
        const char c = text_[i];
        if (c >= 'A' && c <= 'Z') text_[i] = static_cast<char>(c - 'A' + 'a');
    }
}

bool HeaderLine::hasPrefix(const char* prefix) const {
    const size_t n = std::strlen(prefix);
    return n <= len_ && std::memcmp(text_, prefix, n) == 0;
}

bool HeaderLine::contains(const char* token) const {
    return std::strstr(text_, token) != nullptr;
}

long HeaderLine::valueFrom(size_t offset) const {
    if (offset > len_) return 0;
    return std::strtol(text_ + offset, nullptr, 10);
}

}  // namespace kk

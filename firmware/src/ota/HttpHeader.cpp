#include "ota/HttpHeader.h"

namespace kk::ota {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string_view trim(std::string_view text) {
    while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
    return text;
}

// Case-insensitive comparison against an already lowercase field name.
bool nameIs(std::string_view name, std::string_view lower) {
    if (name.size() != lower.size()) return false;
    for (size_t i = 0; i < name.size(); ++i) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != lower[i]) return false;
    }
    return true;
}

bool parseContentLength(std::string_view value, long* out) {
    if (value.empty()) return false;
    long n = 0;
    for (char c : value) {
        if (c < '0' || c > '9') return false;
        if (n > 100000000L) return false;
        n = n * 10 + (c - '0');
    }
    *out = n;
    return true;
}

}  // namespace

bool applyHeaderLine(std::string_view line, ResponseHeaders* out) {
    if (!out) return false;
    const std::string_view text = trim(line);
    if (text.empty()) return false;
    const size_t colon = text.find(':');
    if (colon == std::string_view::npos) return true;  // not a header: drop it
    const std::string_view name = trim(text.substr(0, colon));
    const std::string_view value = trim(text.substr(colon + 1));
    if (nameIs(name, "content-length")) {
        parseContentLength(value, &out->contentLength);
    } else if (nameIs(name, "location")) {
        // Over-long values are dropped, never truncated.
        if (value.size() <= kMaxLocationChars) out->location.assign(value.data(), value.size());
    }
    return true;
}

}  // namespace kk::ota

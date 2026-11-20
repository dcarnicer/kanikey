#pragma once
// One HTTP status or header line in a fixed buffer (MEM-17). The WaniKani client
// used to read each line into an Arduino String, which reallocates about every
// 16 characters on the heap while TLS buffers are live. This keeps the first
// kCapacity bytes of the line on the stack; the rest is consumed by the reader
// and dropped, so the stream stays in step with the next line. Pure C++ (no
// Arduino), so the native tests can check it.
#include <cstddef>

namespace kk {

class HeaderLine {
public:
    // Longest line kept for parsing. The longest header the client reads is about
    // 80 characters, so a longer line is cut and marked truncated() (it never
    // grows the buffer).
    static constexpr size_t kCapacity = 256;

    void clear();
    // Adds one byte. Bytes past kCapacity are dropped and set truncated().
    void push(char c);

    // True when the stored bytes are the blank line that ends a header block:
    // nothing, or only "\r" (the reader keeps the CR of a CRLF line).
    bool blank() const;
    bool truncated() const { return truncated_; }
    size_t size() const { return len_; }
    const char* c_str() const { return text_; }

    // Drops leading and trailing spaces, tabs, CR and LF (String::trim()).
    void trim();
    // ASCII lower-case, in place (String::toLowerCase()).
    void toLower();
    // True when the stored text starts with `prefix` (compared exactly).
    bool hasPrefix(const char* prefix) const;
    // True when `token` occurs anywhere in the stored text.
    bool contains(const char* token) const;
    // strtol(text + offset, 10): the value after a name's prefix, the same as
    // String::substring(offset).toInt().
    long valueFrom(size_t offset) const;

private:
    char text_[kCapacity + 1] = {};  // always NUL-terminated
    size_t len_ = 0;
    bool truncated_ = false;
};

}  // namespace kk

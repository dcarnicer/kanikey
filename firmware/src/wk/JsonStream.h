#pragma once
// Pure SAX-style JSON parser for responses that are too large for a DOM.
// It walks a whole body in place and emits callbacks; it never stores parsed
// values. Strings without escapes are passed as views into the input, escaped
// strings use an internal scratch buffer, and both are only valid for the
// duration of the callback. No Arduino headers.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace kk {

// Return false from any callback to stop the parse. The parser then reports an
// error and leaves the sink responsible for discarding its partial output.
class JsonSink {
public:
    virtual ~JsonSink() = default;
    virtual bool onObjectStart() { return true; }
    virtual bool onObjectEnd() { return true; }
    virtual bool onArrayStart() { return true; }
    virtual bool onArrayEnd() { return true; }
    // A member name. The next value event belongs to this key.
    virtual bool onKey(std::string_view) { return true; }
    // A decoded UTF-8 string value, without quotes.
    virtual bool onString(std::string_view) { return true; }
    // A finite JSON number. Integer fields can cast the double like the DOM.
    virtual bool onNumber(double) { return true; }
    virtual bool onBool(bool) { return true; }
    virtual bool onNull() { return true; }
};

class JsonStreamParser {
public:
    // Maximum number of nested containers, including the root value.
    static constexpr int kDefaultMaxDepth = 32;

    explicit JsonStreamParser(JsonSink* sink, int maxDepth = kDefaultMaxDepth);

    // Parses exactly one complete JSON value. False on malformed input, a
    // truncated body, exceeded depth or a rejected sink callback. The sink may
    // have seen a prefix of the events; callers should clear their output.
    bool parse(std::string_view text);
    const std::string& error() const { return error_; }
    size_t position() const { return pos_; }

private:
    bool value(int depth);
    bool object(int depth);
    bool array(int depth);
    bool string(std::string_view* out);
    bool unicodeEscape(std::string* out);
    bool number();
    bool literal(std::string_view word);
    bool fail(const char* what);
    void ws();
    bool eof() const { return pos_ >= text_.size(); }
    char peek() const { return eof() ? '\0' : text_[pos_]; }
    bool take(char c) {
        if (peek() != c) return false;
        ++pos_;
        return true;
    }

    JsonSink* sink_ = nullptr;
    int maxDepth_ = kDefaultMaxDepth;
    std::string_view text_;
    size_t pos_ = 0;
    std::string scratch_;
    std::string error_;
};

}  // namespace kk

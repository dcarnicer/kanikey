#include "wk/JsonStream.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "wk/Utf8.h"

namespace kk {

namespace {
int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
}  // namespace

JsonStreamParser::JsonStreamParser(JsonSink* sink, int maxDepth)
    : sink_(sink), maxDepth_(maxDepth < 1 ? 1 : maxDepth) {}

void JsonStreamParser::ws() {
    while (!eof()) {
        const char c = peek();
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            ++pos_;
        else
            break;
    }
}

bool JsonStreamParser::fail(const char* what) {
    if (error_.empty())
        error_ = std::string(what) + " at byte " + std::to_string(pos_);
    return false;
}

bool JsonStreamParser::parse(std::string_view text) {
    text_ = text;
    pos_ = 0;
    error_.clear();
    if (!sink_) return fail("null sink");
    ws();
    if (eof()) return fail("unexpected end");
    if (!value(0)) return false;
    ws();
    if (!eof()) return fail("trailing data");
    return true;
}

bool JsonStreamParser::value(int depth) {
    ws();
    if (eof()) return fail("unexpected end");
    switch (peek()) {
        case '{': return object(depth);
        case '[': return array(depth);
        case '"': {
            std::string_view s;
            if (!string(&s)) return false;
            if (!sink_->onString(s)) return fail("sink rejected string");
            return true;
        }
        case 't':
            if (!literal("true")) return false;
            if (!sink_->onBool(true)) return fail("sink rejected bool");
            return true;
        case 'f':
            if (!literal("false")) return false;
            if (!sink_->onBool(false)) return fail("sink rejected bool");
            return true;
        case 'n':
            if (!literal("null")) return false;
            if (!sink_->onNull()) return fail("sink rejected null");
            return true;
        default: return number();
    }
}

bool JsonStreamParser::object(int depth) {
    if (depth >= maxDepth_) return fail("nesting too deep");
    ++pos_;
    if (!sink_->onObjectStart()) return fail("sink rejected object start");
    ws();
    if (take('}')) {
        if (!sink_->onObjectEnd()) return fail("sink rejected object end");
        return true;
    }
    for (;;) {
        ws();
        if (peek() != '"') return fail("expected object key");
        std::string_view key;
        if (!string(&key)) return false;
        if (!sink_->onKey(key)) return fail("sink rejected key");
        ws();
        if (!take(':')) return fail("expected :");
        if (!value(depth + 1)) return false;
        ws();
        if (take('}')) {
            if (!sink_->onObjectEnd()) return fail("sink rejected object end");
            return true;
        }
        if (!take(',')) return fail("expected , or }");
    }
}

bool JsonStreamParser::array(int depth) {
    if (depth >= maxDepth_) return fail("nesting too deep");
    ++pos_;
    if (!sink_->onArrayStart()) return fail("sink rejected array start");
    ws();
    if (take(']')) {
        if (!sink_->onArrayEnd()) return fail("sink rejected array end");
        return true;
    }
    for (;;) {
        if (!value(depth + 1)) return false;
        ws();
        if (take(']')) {
            if (!sink_->onArrayEnd()) return fail("sink rejected array end");
            return true;
        }
        if (!take(',')) return fail("expected , or ]");
    }
}

bool JsonStreamParser::string(std::string_view* out) {
    if (!take('"')) return fail("expected string");
    size_t run = pos_;
    bool escaped = false;
    scratch_.clear();
    for (;;) {
        if (eof()) return fail("unterminated string");
        const char c = text_[pos_];
        if (c == '"') {
            if (escaped) {
                scratch_.append(text_.data() + run, pos_ - run);
                *out = scratch_;
            } else {
                *out = text_.substr(run, pos_ - run);
            }
            ++pos_;
            return true;
        }
        if (static_cast<unsigned char>(c) < 0x20) return fail("control character in string");
        if (c != '\\') {
            ++pos_;
            continue;
        }
        if (!escaped) {
            scratch_.clear();
            escaped = true;
        }
        scratch_.append(text_.data() + run, pos_ - run);
        ++pos_;
        if (eof()) return fail("truncated escape");
        const char e = text_[pos_++];
        switch (e) {
            case '"': scratch_.push_back('"'); break;
            case '\\': scratch_.push_back('\\'); break;
            case '/': scratch_.push_back('/'); break;
            case 'b': scratch_.push_back('\b'); break;
            case 'f': scratch_.push_back('\f'); break;
            case 'n': scratch_.push_back('\n'); break;
            case 'r': scratch_.push_back('\r'); break;
            case 't': scratch_.push_back('\t'); break;
            case 'u':
                if (!unicodeEscape(&scratch_)) return false;
                break;
            default: return fail("bad escape");
        }
        run = pos_;
    }
}

bool JsonStreamParser::unicodeEscape(std::string* out) {
    if (pos_ + 4 > text_.size()) return fail("short \\u escape");
    uint32_t cp = 0;
    for (int i = 0; i < 4; ++i) {
        const int v = hexValue(text_[pos_++]);
        if (v < 0) return fail("bad \\u escape");
        cp = (cp << 4) | static_cast<uint32_t>(v);
    }
    if (cp >= 0xD800 && cp <= 0xDBFF) {
        // A high surrogate must be followed by a low surrogate.
        if (pos_ + 6 > text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u')
            return fail("lone high surrogate");
        pos_ += 2;
        uint32_t low = 0;
        for (int i = 0; i < 4; ++i) {
            const int v = hexValue(text_[pos_++]);
            if (v < 0) return fail("bad low surrogate");
            low = (low << 4) | static_cast<uint32_t>(v);
        }
        if (low < 0xDC00 || low > 0xDFFF) return fail("bad low surrogate");
        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
        return fail("lone low surrogate");
    }
    char buf[5];
    const int n = utf8Encode(cp, buf);
    if (n == 0)
        out->push_back('\0');
    else
        out->append(buf, static_cast<size_t>(n));
    return true;
}

bool JsonStreamParser::number() {
    const size_t start = pos_;
    if (peek() == '-') ++pos_;
    if (eof()) return fail("expected number");
    if (peek() == '0') {
        ++pos_;
        if (peek() >= '0' && peek() <= '9') return fail("leading zero in number");
    } else if (peek() >= '1' && peek() <= '9') {
        while (peek() >= '0' && peek() <= '9') ++pos_;
    } else {
        return fail("expected value");
    }
    if (peek() == '.') {
        ++pos_;
        if (!(peek() >= '0' && peek() <= '9')) return fail("missing fraction digits");
        while (peek() >= '0' && peek() <= '9') ++pos_;
    }
    if (peek() == 'e' || peek() == 'E') {
        ++pos_;
        if (peek() == '+' || peek() == '-') ++pos_;
        if (!(peek() >= '0' && peek() <= '9')) return fail("missing exponent digits");
        while (peek() >= '0' && peek() <= '9') ++pos_;
    }

    const size_t length = pos_ - start;
    char buf[64];
    const char* token = nullptr;
    std::string copy;
    if (length < sizeof buf) {
        std::memcpy(buf, text_.data() + start, length);
        buf[length] = '\0';
        token = buf;
    } else {
        copy.assign(text_.data() + start, length);
        token = copy.c_str();
    }
    char* end = nullptr;
    const double value = std::strtod(token, &end);
    if (!end || *end != '\0' || !std::isfinite(value)) return fail("bad number");
    if (!sink_->onNumber(value)) return fail("sink rejected number");
    return true;
}

bool JsonStreamParser::literal(std::string_view word) {
    if (text_.substr(pos_, word.size()) != word) return fail("bad literal");
    pos_ += word.size();
    return true;
}

}  // namespace kk

#include "wk/Json.h"

#include <cmath>
#include <cstdlib>

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

void JsonParser::ws() {
    while (!eof()) {
        const char c = peek();
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            ++pos_;
        else
            break;
    }
}

bool JsonParser::fail(const char* what) {
    error_ = std::string(what) + " at byte " + std::to_string(pos_);
    return false;
}

bool JsonParser::parse(std::string_view text, JsonValue* out) {
    text_ = text;
    pos_ = 0;
    error_.clear();
    if (!out) return fail("null output");
    *out = JsonValue{};
    if (!value(out, 0)) return false;
    ws();
    if (!eof()) return fail("trailing data");
    return true;
}

bool JsonParser::value(JsonValue* out, int depth) {
    if (depth > 32) return fail("nesting too deep");
    ws();
    if (eof()) return fail("unexpected end");
    const char c = peek();
    if (c == '{') return object(out, depth);
    if (c == '[') return array(out, depth);
    if (c == '"') {
        out->type = JsonType::String;
        return string(&out->string);
    }
    if (c == 't') {
        if (!literal("true")) return false;
        out->type = JsonType::Bool;
        out->boolean = true;
        return true;
    }
    if (c == 'f') {
        if (!literal("false")) return false;
        out->type = JsonType::Bool;
        out->boolean = false;
        return true;
    }
    if (c == 'n') {
        if (!literal("null")) return false;
        out->type = JsonType::Null;
        return true;
    }
    return number(out);
}

bool JsonParser::literal(std::string_view word) {
    if (text_.substr(pos_, word.size()) != word) return fail("bad literal");
    pos_ += word.size();
    return true;
}

bool JsonParser::object(JsonValue* out, int depth) {
    if (!take('{')) return fail("expected {");
    out->type = JsonType::Object;
    ws();
    if (take('}')) return true;
    for (;;) {
        ws();
        std::string key;
        if (peek() != '"') return fail("expected object key");
        if (!string(&key)) return false;
        ws();
        if (!take(':')) return fail("expected :");
        JsonValue v;
        if (!value(&v, depth + 1)) return false;
        out->object.emplace_back(std::move(key), std::move(v));
        ws();
        if (take('}')) return true;
        if (!take(',')) return fail("expected , or }");
    }
}

bool JsonParser::array(JsonValue* out, int depth) {
    if (!take('[')) return fail("expected [");
    out->type = JsonType::Array;
    ws();
    if (take(']')) return true;
    for (;;) {
        JsonValue v;
        if (!value(&v, depth + 1)) return false;
        out->array.push_back(std::move(v));
        ws();
        if (take(']')) return true;
        if (!take(',')) return fail("expected , or ]");
    }
}

bool JsonParser::string(std::string* out) {
    if (!take('"')) return fail("expected string");
    out->clear();
    while (!eof()) {
        const char c = text_[pos_++];
        if (c == '"') return true;
        if (static_cast<unsigned char>(c) < 0x20) return fail("control character in string");
        if (c != '\\') {
            out->push_back(c);
            continue;
        }
        if (eof()) return fail("truncated escape");
        const char e = text_[pos_++];
        switch (e) {
            case '"': out->push_back('"'); break;
            case '\\': out->push_back('\\'); break;
            case '/': out->push_back('/'); break;
            case 'b': out->push_back('\b'); break;
            case 'f': out->push_back('\f'); break;
            case 'n': out->push_back('\n'); break;
            case 'r': out->push_back('\r'); break;
            case 't': out->push_back('\t'); break;
            case 'u': {
                if (pos_ + 4 > text_.size()) return fail("short \\u escape");
                int h = 0;
                for (int i = 0; i < 4; ++i) {
                    const int v = hexValue(text_[pos_++]);
                    if (v < 0) return fail("bad \\u escape");
                    h = (h << 4) | v;
                }
                uint32_t cp = static_cast<uint32_t>(h);
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    // A high surrogate must be followed by a low surrogate.
                    if (pos_ + 6 > text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u')
                        return fail("lone high surrogate");
                    pos_ += 2;
                    int l = 0;
                    for (int i = 0; i < 4; ++i) {
                        const int v = hexValue(text_[pos_++]);
                        if (v < 0) return fail("bad low surrogate");
                        l = (l << 4) | v;
                    }
                    if (l < 0xDC00 || l > 0xDFFF) return fail("bad low surrogate");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<uint32_t>(l) - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return fail("lone low surrogate");
                }
                char buf[5];
                utf8Encode(cp, buf);
                out->append(buf);
                break;
            }
            default: return fail("bad escape");
        }
    }
    return fail("unterminated string");
}

bool JsonParser::number(JsonValue* out) {
    const size_t start = pos_;
    if (peek() == '-') ++pos_;
    while (!eof() && peek() >= '0' && peek() <= '9') ++pos_;
    if (!eof() && peek() == '.') {
        ++pos_;
        while (!eof() && peek() >= '0' && peek() <= '9') ++pos_;
    }
    if (!eof() && (peek() == 'e' || peek() == 'E')) {
        ++pos_;
        if (peek() == '+' || peek() == '-') ++pos_;
        while (!eof() && peek() >= '0' && peek() <= '9') ++pos_;
    }
    if (pos_ == start) return fail("expected value");
    const std::string token(text_.substr(start, pos_ - start));
    char* end = nullptr;
    const double d = std::strtod(token.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(d)) return fail("bad number");
    out->type = JsonType::Number;
    out->number = d;
    return true;
}

}  // namespace kk

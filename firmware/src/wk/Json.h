#pragma once
// Small JSON DOM for the API responses that fit comfortably in RAM (`/user`,
// `/summary`). Subject pages will use a streaming parser instead. Pure C++.
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kk {

enum class JsonType : uint8_t { Null, Bool, Number, String, Array, Object };

struct JsonValue {
    JsonType type = JsonType::Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    bool isNull() const { return type == JsonType::Null; }
    bool isObject() const { return type == JsonType::Object; }
    bool isArray() const { return type == JsonType::Array; }
    bool isString() const { return type == JsonType::String; }
    bool isNumber() const { return type == JsonType::Number; }
    size_t size() const { return type == JsonType::Array ? array.size() : object.size(); }

    const JsonValue* find(std::string_view key) const {
        if (type != JsonType::Object) return nullptr;
        for (const auto& [k, v] : object)
            if (k == key) return &v;
        return nullptr;
    }
    const JsonValue* at(size_t i) const {
        return type == JsonType::Array && i < array.size() ? &array[i] : nullptr;
    }
};

class JsonParser {
public:
    // Parses one complete JSON value. False on error; error() describes it.
    bool parse(std::string_view text, JsonValue* out);
    const std::string& error() const { return error_; }

private:
    bool value(JsonValue* out, int depth);
    bool object(JsonValue* out, int depth);
    bool array(JsonValue* out, int depth);
    bool string(std::string* out);
    bool number(JsonValue* out);
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

    std::string_view text_;
    size_t pos_ = 0;
    std::string error_;
};

}  // namespace kk

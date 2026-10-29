#include "wk/SummaryParser.h"

#include <cstdlib>
#include <cstring>
#include <utility>

#include "wk/Text.h"
#include "wk/Time.h"
#include "wk/Utf8.h"

namespace kk {

namespace {
class Reader {
public:
    Reader(std::string_view text, size_t maxIds) : text_(text), maxIds_(maxIds) {}

    bool parse(Summary* out) {
        ws();
        if (!object(out)) return false;
        ws();
        return pos_ == text_.size();
    }
    bool complete() const { return hasData_ && hasLessons_ && hasReviews_; }
    const std::string& error() const { return error_; }

private:
    bool fail(const char* what) {
        if (error_.empty()) {
            error_ = std::string(what) + " at " + std::to_string(pos_);
        }
        return false;
    }
    void ws() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++pos_;
            else
                break;
        }
    }
    bool take(char c) {
        ws();
        if (pos_ >= text_.size() || text_[pos_] != c) return false;
        ++pos_;
        return true;
    }
    bool peek(char c) const { return pos_ < text_.size() && text_[pos_] == c; }

    bool string(std::string* out) {
        ws();
        if (!take('"')) return fail("expected string");
        out->clear();
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') return true;
            if (c != '\\') {
                out->push_back(c);
                continue;
            }
            if (pos_ >= text_.size()) return fail("truncated escape");
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
                    if (pos_ + 4 > text_.size()) return fail("short \\u");
                    uint32_t cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = text_[pos_++];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= static_cast<uint32_t>(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= static_cast<uint32_t>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= static_cast<uint32_t>(h - 'A' + 10);
                        else return fail("bad \\u");
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

    bool number(double* out) {
        ws();
        const size_t start = pos_;
        if (peek('-')) ++pos_;
        while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        if (peek('.')) {
            ++pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        if (peek('e') || peek('E')) {
            ++pos_;
            if (peek('+') || peek('-')) ++pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        if (pos_ == start) return fail("expected number");
        const std::string token(text_.substr(start, pos_ - start));
        char* end = nullptr;
        *out = std::strtod(token.c_str(), &end);
        if (!end || *end) return fail("bad number");
        return true;
    }

    bool literal(const char* word) {
        const size_t n = std::strlen(word);
        if (text_.substr(pos_, n) != word) return fail("bad literal");
        pos_ += n;
        return true;
    }

    bool skipValue(int depth = 0) {
        if (depth > 32) return fail("nesting");
        ws();
        if (pos_ >= text_.size()) return fail("eof");
        const char c = text_[pos_];
        if (c == '"') {
            std::string tmp;
            return string(&tmp);
        }
        if (c == '{') {
            ++pos_;
            ws();
            if (take('}')) return true;
            for (;;) {
                std::string key;
                if (!string(&key)) return false;
                if (!take(':')) return fail("expected :");
                if (!skipValue(depth + 1)) return false;
                if (take('}')) return true;
                if (!take(',')) return fail("expected , or }");
            }
        }
        if (c == '[') {
            ++pos_;
            ws();
            if (take(']')) return true;
            for (;;) {
                if (!skipValue(depth + 1)) return false;
                if (take(']')) return true;
                if (!take(',')) return fail("expected , or ]");
            }
        }
        if (c == 't') return literal("true");
        if (c == 'f') return literal("false");
        if (c == 'n') return literal("null");
        double ignored = 0;
        return number(&ignored);
    }

    bool buckets(std::vector<SummaryBucket>* out) {
        if (!take('[')) return fail("expected [");
        ws();
        if (take(']')) return true;
        for (;;) {
            if (!take('{')) return fail("expected bucket");
            SummaryBucket b;
            ws();
            if (!take('}')) {
                for (;;) {
                    std::string key;
                    if (!string(&key)) return false;
                    if (!take(':')) return fail("expected :");
                    if (key == "available_at") {
                        std::string s;
                        if (!string(&s)) return false;
                        parseIso8601(s, &b.availableAt);
                    } else if (key == "subject_ids") {
                        if (!take('[')) return fail("expected subject_ids");
                        ws();
                        if (!take(']')) {
                            for (;;) {
                                double n = 0;
                                if (!number(&n)) return false;
                                if (b.count < 0xFFFF) ++b.count;
                                if (b.subjectIds.size() < maxIds_)
                                    b.subjectIds.push_back(static_cast<uint32_t>(n));
                                if (take(']')) break;
                                if (!take(',')) return fail("expected , or ]");
                            }
                        }
                    } else {
                        if (!skipValue(1)) return false;
                    }
                    if (take('}')) break;
                    if (!take(',')) return fail("expected , or }");
                }
            }
            out->push_back(std::move(b));
            if (take(']')) return true;
            if (!take(',')) return fail("expected , or ]");
        }
    }

    bool data(Summary* out) {
        if (!take('{')) return fail("expected data");
        ws();
        if (take('}')) return true;
        for (;;) {
            std::string key;
            if (!string(&key)) return false;
            if (!take(':')) return fail("expected :");
            if (key == "lessons") {
                if (!buckets(&out->lessons)) return false;
                hasLessons_ = true;
            } else if (key == "reviews") {
                if (!buckets(&out->reviews)) return false;
                hasReviews_ = true;
            } else if (key == "next_reviews_at") {
                ws();
                if (peek('n')) {
                    if (!literal("null")) return false;
                    out->nextReviewsAt.clear();
                } else if (!string(&out->nextReviewsAt)) {
                    return false;
                }
            } else {
                if (!skipValue(1)) return false;
            }
            if (take('}')) return true;
            if (!take(',')) return fail("expected , or }");
        }
    }

    bool object(Summary* out) {
        if (!take('{')) return fail("expected {");
        ws();
        if (take('}')) return true;
        for (;;) {
            std::string key;
            if (!string(&key)) return false;
            if (!take(':')) return fail("expected :");
            if (key == "data") {
                if (!data(out)) return false;
                hasData_ = true;
            } else if (key == "data_updated_at") {
                std::string stamp;
                if (!string(&stamp)) return false;
                parseIso8601(stamp, &out->dataUpdatedAt);
            } else {
                if (!skipValue(1)) return false;
            }
            if (take('}')) return true;
            if (!take(',')) return fail("expected , or }");
        }
    }

    std::string_view text_;
    size_t pos_ = 0;
    size_t maxIds_;
    std::string error_;
    bool hasData_ = false;
    bool hasLessons_ = false;
    bool hasReviews_ = false;
};
}  // namespace

bool parseSummaryText(std::string_view text, Summary* out, size_t maxIdsPerBucket) {
    if (!out) return false;
    out->lessons.clear();
    out->reviews.clear();
    out->nextReviewsAt.clear();
    out->dataUpdatedAt = 0;
    Reader r(text, maxIdsPerBucket);
    if (!r.parse(out) || !r.complete()) {
        out->lessons.clear();
        out->reviews.clear();
        out->nextReviewsAt.clear();
        out->dataUpdatedAt = 0;
        return false;
    }
    // A valid report with no buckets is an empty account, not a parse failure.
    return true;
}

}  // namespace kk

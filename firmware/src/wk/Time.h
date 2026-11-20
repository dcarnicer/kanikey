#pragma once
// ISO 8601 UTC helpers for WaniKani timestamps. The API uses
// `YYYY-MM-DDTHH:MM:SS.ffffffZ`; KaniKey only needs second resolution.
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <string_view>

namespace kk {

inline int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

// The six fields of `%4d-%2d-%2dT%2d:%2d:%2d`, read without scanf. newlib's
// scanf engine costs ~9 KB of flash (FLASH-01), and this one call was its only
// user. Each `%Nd` skips white space (not counted), takes an optional sign
// (counted in N) and then digits; a literal must match exactly. A NUL ends the
// input, as it does for sscanf. test_iso8601_scan_matches_sscanf (in
// test/test_wk/test_wk.cpp) checks it against sscanf.
namespace detail {

inline bool isScanSpace(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

inline bool scanIntField(std::string_view t, size_t* pos, size_t width, int* out) {
    size_t i = *pos;
    while (i < t.size() && isScanSpace(t[i])) ++i;
    const size_t limit = i + width < t.size() ? i + width : t.size();
    bool negative = false;
    if (i < limit && (t[i] == '+' || t[i] == '-')) {
        negative = t[i] == '-';
        ++i;
    }
    const size_t digitsAt = i;
    long value = 0;
    while (i < limit && t[i] >= '0' && t[i] <= '9') {
        value = value * 10 + (t[i] - '0');
        ++i;
    }
    if (i == digitsAt) return false;
    *out = static_cast<int>(negative ? -value : value);
    *pos = i;
    return true;
}

inline bool scanLiteral(std::string_view t, size_t* pos, char c) {
    if (*pos >= t.size() || t[*pos] != c) return false;
    ++*pos;
    return true;
}

}  // namespace detail

inline bool scanDateTimeFields(std::string_view t, int f[6]) {
    size_t p = 0;
    return detail::scanIntField(t, &p, 4, &f[0]) && detail::scanLiteral(t, &p, '-') &&
           detail::scanIntField(t, &p, 2, &f[1]) && detail::scanLiteral(t, &p, '-') &&
           detail::scanIntField(t, &p, 2, &f[2]) && detail::scanLiteral(t, &p, 'T') &&
           detail::scanIntField(t, &p, 2, &f[3]) && detail::scanLiteral(t, &p, ':') &&
           detail::scanIntField(t, &p, 2, &f[4]) && detail::scanLiteral(t, &p, ':') &&
           detail::scanIntField(t, &p, 2, &f[5]);
}

inline bool parseIso8601(std::string_view text, uint32_t* epoch) {
    if (!epoch) return false;
    const std::string t(text);
    int f[6] = {0, 0, 0, 0, 0, 0};
    if (!scanDateTimeFields(t, f)) return false;
    const int y = f[0], mo = f[1], d = f[2], h = f[3], mi = f[4], s = f[5];
    // The tail is checked from the fixed 19-character prefix, as before the
    // scanf replacement, so malformed input is accepted or refused the same way.
    size_t pos = 19;
    if (pos < t.size() && t[pos] == '.') {
        ++pos;
        while (pos < t.size() && t[pos] >= '0' && t[pos] <= '9') ++pos;
    }
    if (pos < t.size() && t[pos] == 'Z') {
        ++pos;
    } else if (pos + 5 < t.size() && t[pos] == '+' && t[pos + 1] == '0' && t[pos + 2] == '0' &&
               t[pos + 3] == ':' && t[pos + 4] == '0' && t[pos + 5] == '0') {
        pos += 6;
    } else {
        return false;
    }
    if (pos != t.size()) return false;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60) return false;
    const int64_t days = daysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d));
    const int64_t seconds = days * 86400 + h * 3600 + mi * 60 + s;
    if (seconds < 0 || seconds > 0xFFFFFFFFll) return false;
    *epoch = static_cast<uint32_t>(seconds);
    return true;
}

inline std::string formatIso8601(uint32_t epoch) {
    const time_t t = static_cast<time_t>(epoch);
    struct tm tm {};
    if (!gmtime_r(&t, &tm)) return std::string();
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

}  // namespace kk

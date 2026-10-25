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

inline bool parseIso8601(std::string_view text, uint32_t* epoch) {
    if (!epoch) return false;
    const std::string t(text);
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (std::sscanf(t.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &s) != 6)
        return false;
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

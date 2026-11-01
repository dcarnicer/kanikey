#pragma once
// HTTP response header lines for the OTA fetcher. Pure C++ (no Arduino), so the
// redirect handling is native-tested.
//
// GitHub answers a release download with two redirects and puts a ~4 KB
// Content-Security-Policy line before the Location. The reader therefore never
// aborts on a long line: unknown or over-long headers are consumed and dropped.
#include <cstddef>
#include <string>
#include <string_view>

namespace kk::ota {

// Longest Location that is kept. A longer value is dropped without being
// stored, so a redirect becomes an HTTP error instead of a truncated URL.
constexpr size_t kMaxLocationChars = 4096;

struct ResponseHeaders {
    std::string location;
    long contentLength = -1;
};

// Applies one raw header line to `out`: trims CR/LF, spaces and tabs, matches
// the field name case-insensitively, keeps a Location up to
// kMaxLocationChars and parses Content-Length. Returns false only for a blank
// line, which ends the header block; a long or unknown header is consumed and
// ignored.
bool applyHeaderLine(std::string_view line, ResponseHeaders* out);

}  // namespace kk::ota

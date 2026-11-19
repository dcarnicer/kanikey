#include "wk/HttpChunked.h"

#include <cstring>

namespace kk {
namespace {

// Finds the CRLF that ends the line starting at `from`. Returns the index of
// the CR, or false when no complete line is in the buffer yet.
bool findCrlf(const char* data, size_t len, size_t from, size_t* cr) {
    if (from >= len) return false;
    for (size_t i = from; i + 1 < len; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n') {
            *cr = i;
            return true;
        }
    }
    return false;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Parses `1a;name=value` (the chunk-size line). The extension after ';' is
// ignored. Rejects an empty size, a non-hex digit and an overflow of size_t.
bool parseSizeLine(const char* data, size_t begin, size_t end, size_t* size) {
    size_t value = 0;
    bool any = false;
    for (size_t i = begin; i < end; ++i) {
        const char c = data[i];
        if (c == ';') break;
        const int digit = hexDigit(c);
        if (digit < 0) return false;
        if (value > (static_cast<size_t>(-1) - 15u) / 16u) return false;
        value = value * 16u + static_cast<size_t>(digit);
        any = true;
    }
    if (!any) return false;
    *size = value;
    return true;
}

// Read-only pass. It validates the framing and measures the decoded size, but
// never writes, so an Incomplete result can be retried after more bytes arrive.
ChunkedStatus scan(const char* data, size_t len, size_t cap, size_t* decoded) {
    size_t pos = 0;
    size_t out = 0;
    for (;;) {
        size_t cr = 0;
        if (!findCrlf(data, len, pos, &cr)) return ChunkedStatus::Incomplete;
        size_t size = 0;
        if (!parseSizeLine(data, pos, cr, &size)) return ChunkedStatus::Malformed;
        pos = cr + 2;

        if (size == 0) {
            // Zero chunk: optional trailer lines, then an empty line.
            for (;;) {
                if (!findCrlf(data, len, pos, &cr)) return ChunkedStatus::Incomplete;
                const size_t lineLen = cr - pos;
                pos = cr + 2;
                if (lineLen == 0) {
                    *decoded = out;
                    return ChunkedStatus::Complete;
                }
            }
        }

        if (out > cap || size > cap - out) return ChunkedStatus::TooLarge;
        if (pos > len || size > len - pos) return ChunkedStatus::Incomplete;
        pos += size;
        if (len - pos < 2) return ChunkedStatus::Incomplete;
        if (data[pos] != '\r' || data[pos + 1] != '\n') return ChunkedStatus::Malformed;
        pos += 2;
        out += size;
    }
}

}  // namespace

bool chunkedDecodeInPlace(char* data, size_t len, size_t cap, size_t* outLen,
                          ChunkedStatus* status) {
    if (outLen) *outLen = 0;
    if (!data || !outLen) {
        if (status) *status = ChunkedStatus::Malformed;
        return false;
    }

    size_t decoded = 0;
    const ChunkedStatus result = scan(data, len, cap, &decoded);
    if (status) *status = result;
    if (result != ChunkedStatus::Complete) return false;

    // Second pass: move each chunk's payload down over the framing that was
    // consumed since the previous chunk. Destination is always before source,
    // so memmove is safe even when the two ranges overlap.
    size_t pos = 0;
    size_t out = 0;
    while (true) {
        size_t cr = 0;
        findCrlf(data, len, pos, &cr);
        size_t size = 0;
        parseSizeLine(data, pos, cr, &size);
        pos = cr + 2;
        if (size == 0) break;
        if (size && out != pos) std::memmove(data + out, data + pos, size);
        out += size;
        pos += size + 2;
    }
    *outLen = out;
    return true;
}

}  // namespace kk

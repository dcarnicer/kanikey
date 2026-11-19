#pragma once
// HTTP/1.1 chunked transfer decoding. Pure: no Arduino headers, no heap.
//
// WaniKani answers an HTTP/1.1 keep-alive request with `Transfer-Encoding:
// chunked`. The wire body is read into the shared response buffer and decoded
// in place, so one 32 KB buffer serves both the encoded and the decoded body.
#include <cstddef>
#include <cstdint>

namespace kk {

enum class ChunkedStatus : uint8_t {
    Complete,    // the zero chunk and its trailers were found
    Incomplete,  // valid so far, more wire bytes are needed
    Malformed,   // a size line or a chunk delimiter is not valid
    TooLarge,    // the decoded body does not fit in `cap`
};

// Decodes one complete chunked message in `data[0..len)` in place and returns
// true on Complete. `cap` is the capacity of `data` and the largest decoded
// body that is accepted. `*outLen` receives the decoded size.
//
// On Incomplete, Malformed and TooLarge the buffer is left untouched and
// `*outLen` is 0, so a reader can append more bytes to the same buffer and
// call again. `status` (optional) distinguishes the failure cases.
bool chunkedDecodeInPlace(char* data, size_t len, size_t cap, size_t* outLen,
                          ChunkedStatus* status = nullptr);

}  // namespace kk

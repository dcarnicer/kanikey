#pragma once
// Pure helpers for the KANIKEY_DEV request overrides (`cap`, `devhost`). They
// live in wk/ so the native tests can cover them; the device code that calls
// them is compiled only into dev builds.
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kk {

// The body cap for one request. `devCap` 0 means no override. The result is
// never larger than the caller's cap or the buffer the body is read into.
size_t effectiveBodyCap(size_t requestedCap, size_t bufferSize, uint32_t devCap);

// Whether `devhost <host> <port>` may be used. The host must be a DNS name or
// an IPv4 literal of 1-63 characters (letters, digits, '.', '-'), and the port
// 1-65535. A name containing "wanikani" (any case) is refused: the override
// turns certificate checks off, so it must never point the token at the real
// API.
bool devHostAllowed(std::string_view host, uint32_t port);

}  // namespace kk

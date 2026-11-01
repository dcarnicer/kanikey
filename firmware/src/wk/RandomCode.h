#pragma once
// Random codes for the setup portal: the WPA2 key of the temporary network and
// the 6-digit unlock code of the station page. Pure C++ so the native tests can
// drive the generator with a fixed random source.
#include <cstddef>
#include <cstdint>

namespace kk {

// Printable ASCII without 0/O/1/I/l, so a key read off the device screen is
// unambiguous. Exactly 32 characters, so `rnd() % 32` has no modulo bias.
constexpr char kCodeAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
constexpr char kDigitAlphabet[] = "0123456789";

// Writes `length` characters picked from `alphabet` into `out` and
// NUL-terminates it. `rnd()` is the random source (esp_random() on the device,
// a fixed sequence in tests). Leaves `out` empty when the arguments cannot
// produce a full code, e.g. when `n` is too small for `length` plus the NUL.
void makeRandomCode(uint32_t (*rnd)(), const char* alphabet, size_t length, char* out,
                    size_t n);

}  // namespace kk

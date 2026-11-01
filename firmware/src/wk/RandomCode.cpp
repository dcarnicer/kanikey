// Random code generator; see RandomCode.h.
#include "wk/RandomCode.h"

#include <cstring>

namespace kk {

void makeRandomCode(uint32_t (*rnd)(), const char* alphabet, size_t length, char* out,
                    size_t n) {
    if (!out || n == 0) return;
    out[0] = '\0';
    if (!rnd || !alphabet) return;
    const size_t count = strlen(alphabet);
    if (count == 0 || length == 0 || length >= n) return;
    for (size_t i = 0; i < length; ++i) out[i] = alphabet[rnd() % count];
    out[length] = '\0';
}

}  // namespace kk

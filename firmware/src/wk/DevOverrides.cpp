#include "wk/DevOverrides.h"

#include <algorithm>

namespace kk {

size_t effectiveBodyCap(size_t requestedCap, size_t bufferSize, uint32_t devCap) {
    size_t cap = std::min(requestedCap, bufferSize);
    if (devCap != 0) cap = std::min(cap, static_cast<size_t>(devCap));
    return cap;
}

bool devHostAllowed(std::string_view host, uint32_t port) {
    if (port == 0 || port > 65535) return false;
    if (host.empty() || host.size() > 63) return false;
    if (host.front() == '.' || host.front() == '-' || host.back() == '.' || host.back() == '-')
        return false;
    for (char c : host) {
        const bool digit = c >= '0' && c <= '9';
        const bool lower = c >= 'a' && c <= 'z';
        const bool upper = c >= 'A' && c <= 'Z';
        if (!(digit || lower || upper || c == '.' || c == '-')) return false;
    }
    // Case-insensitive search for "wanikani".
    constexpr std::string_view kForbidden = "wanikani";
    for (size_t i = 0; i + kForbidden.size() <= host.size(); ++i) {
        size_t k = 0;
        while (k < kForbidden.size()) {
            char c = host[i + k];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c != kForbidden[k]) break;
            ++k;
        }
        if (k == kForbidden.size()) return false;
    }
    return true;
}

}  // namespace kk

#include "platform/DevToken.h"

#include <Preferences.h>

namespace kk {

namespace {
constexpr const char* kNs = "kanikey";
constexpr const char* kKey = "token";
}

String DevToken::load() {
    Preferences p;
    if (!p.begin(kNs, false)) return String();
    const String token = p.isKey(kKey) ? p.getString(kKey, "") : String();
    p.end();
    return token;
}

bool DevToken::save(const String& token) {
    if (token.isEmpty() || token.length() > 128) return false;
    Preferences p;
    if (!p.begin(kNs, false)) return false;
    const bool ok = p.putString(kKey, token) == token.length();
    p.end();
    return ok;
}

void DevToken::clear() {
    Preferences p;
    if (!p.begin(kNs, false)) return;
    p.remove(kKey);
    p.end();
}

bool DevToken::has() { return !load().isEmpty(); }

}  // namespace kk

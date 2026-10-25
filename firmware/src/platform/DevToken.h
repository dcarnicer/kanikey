#pragma once
// Development token storage for the first API milestone.
//
// The production setup flow will use the WPA2 portal; until then the token can
// be entered over the serial monitor with `k <token>`. It is stored in NVS
// namespace "kanikey" and never printed back.
#include <Arduino.h>

namespace kk {

class DevToken {
public:
    static String load();
    static bool save(const String& token);
    static void clear();
    static bool has();
};

}  // namespace kk

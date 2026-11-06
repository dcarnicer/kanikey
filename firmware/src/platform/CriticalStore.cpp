#include "platform/CriticalStore.h"

#include <LittleFS.h>

#include <ctime>

namespace kk {

namespace {
constexpr const char* kPath = "/state/critical.bin";
constexpr const char* kTmp = "/state/critical.tmp";
constexpr size_t kMaxBytes = 20 + 2000 * 8;

uint32_t nowSeconds() {
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    return now >= 1700000000u ? now : 0;
}
}  // namespace

bool criticalLoad(uint32_t accountKey, std::vector<CriticalItem>* out) {
    if (!out || accountKey == 0) return false;
    File file = LittleFS.open(kPath, "r");
    if (!file) return false;
    const size_t size = static_cast<size_t>(file.size());
    if (size < 20 || size > kMaxBytes) {
        file.close();
        return false;
    }
    std::vector<uint8_t> bytes(size);
    size_t got = 0;
    while (got < size) {
        const int n = file.read(bytes.data() + got, size - got);
        if (n <= 0) break;
        got += static_cast<size_t>(n);
    }
    file.close();
    if (got != size) return false;
    if (!decodeCriticalList(bytes.data(), bytes.size(), accountKey, out)) return false;
    Serial.printf("[critical] loaded %u items\n", static_cast<unsigned>(out->size()));
    return true;
}

bool criticalSave(uint32_t accountKey, const std::vector<CriticalItem>& items) {
    std::vector<uint8_t> bytes;
    if (!encodeCriticalList(items, accountKey, nowSeconds(), &bytes)) return false;
    LittleFS.mkdir("/state");
    File file = LittleFS.open(kTmp, "w");
    if (!file) return false;
    const bool ok = file.write(bytes.data(), bytes.size()) == bytes.size();
    file.close();
    if (!ok) {
        LittleFS.remove(kTmp);
        return false;
    }
    if (LittleFS.exists(kPath)) LittleFS.remove(kPath);
    if (!LittleFS.rename(kTmp, kPath)) {
        LittleFS.remove(kTmp);
        return false;
    }
    Serial.printf("[critical] saved %u items\n", static_cast<unsigned>(items.size()));
    return true;
}

void criticalClear() {
    if (LittleFS.exists(kPath)) LittleFS.remove(kPath);
    if (LittleFS.exists(kTmp)) LittleFS.remove(kTmp);
}

}  // namespace kk

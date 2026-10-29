#pragma once
// Firmware updates over the air: the platform side. Arduino/ESP32 only; the
// manifest parser and the offer rule live in ota/Manifest (native-tested).
//
// The install streams the app image into Arduino's Update library, checking
// the exact size and SHA-256 from the manifest while it writes. Update writes
// the image header last, so a cut download cannot boot. Only after both checks
// pass does activate() let Update.end() verify the image and switch slots.
//
// The bootloader is built with rollback enabled. verifyRollbackLater() tells
// the framework not to mark a pending image valid immediately; the app calls
// markValid() once it has run for a few seconds, and a crash before that makes
// the bootloader return to the previous slot.
#include <Arduino.h>

#include <cstdint>
#include <string>

#include "ota/Manifest.h"

namespace kk::ota {

constexpr const char* kDefaultManifestUrl =
    "https://github.com/dcarnicer/kanikey/releases/latest/download/kanikey-firmware.txt";

enum class FetchStatus : uint8_t {
    Ok,
    Connect,
    Tls,
    Http,
    TooLarge,
    SizeMismatch,
    HashMismatch,
    BadImage,
    WriteFailed,
    NoSlot,
    BadUrl,
    Cancelled,
};
const char* fetchStatusString(FetchStatus status);

struct FetchResult {
    FetchStatus status = FetchStatus::Connect;
    int httpCode = 0;
    int tlsError = 0;
    size_t bytes = 0;
};

using ProgressFn = bool (*)(size_t done, size_t total, void* ctx);

// The PlatformIO environment this image was built from
// (`-DKANIKEY_ENV="cardputer-adv"`); "unknown" without the flag.
const char* buildEnv();

// Only a device running app0/app1 of the two-slot partition table updates
// itself. A single-slot or launcher layout returns false.
bool layoutIsOurs();
uint32_t slotBytes();
const char* runningSlot();
const char* nextSlot();

// The running image is a new one on its first boot, pending verification.
bool pendingVerify();
bool markValid();
const char* lastInvalidSlot();
void rollBackNow();

// Reads a small text resource over HTTPS, following up to four redirects.
// `http://` is accepted only in a KANIKEY_DEV build (for a local test server).
FetchResult fetchText(std::string_view url, size_t maxBytes, std::string* out,
                      uint32_t timeoutMs = 15000);

// Blocking install into the next slot (network must be up). Ok means the whole
// image was written and matched the manifest; call activate() or abandon().
FetchResult install(const AppImage& app, ProgressFn progress = nullptr,
                    void* progressCtx = nullptr, uint32_t idleTimeoutMs = 15000,
                    uint32_t totalTimeoutMs = 10u * 60u * 1000u);
bool activate();
void abandon();

// The manifest URL: the GitHub latest release by default. A dev build may
// override it with `U <url>` (NVS `fwurl`); release builds always use the
// default URL.
String manifestUrl();
bool setManifestUrl(const String& url);  // dev only; empty clears it

// Small NVS record shown by Settings/About and used to detect a rollback.
struct SavedState {
    std::string badgeVersion;  // a newer version the last check found
    uint32_t badgeBytes = 0;
    std::string failedVersion;  // a version this device rolled back from
    std::string installing;     // version written before the last restart
    std::string installSlot;    // the slot it was written to
    uint32_t lastCheckAt = 0;   // Unix time of the last manifest read
};
SavedState loadSavedState();
void saveBadge(std::string_view version, uint32_t bytes);
void saveFailedVersion(std::string_view version);
void saveInstallRecord(std::string_view version, std::string_view slot);
void clearInstallRecord();
void saveLastCheck(uint32_t epoch);

}  // namespace kk::ota

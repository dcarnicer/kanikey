#pragma once
// Release manifest and version rules for over-the-air updates. Pure C++ (no
// Arduino), so the parser and the offer decision are native-tested.
//
// Manifest format, version 1 (UTF-8, LF; a CR before LF is ignored):
//
//   #kanikey-firmware<TAB>1
//   version<TAB>v0.1.0
//   app<TAB>cardputer-adv<TAB>kanikey-cardputer-adv.bin<TAB>1423021<TAB><sha256>
//
// Blank lines and lines starting with '#' (after the header) are ignored.
// `version` appears once, before the first `app`. A relative file name is
// resolved against the manifest URL's directory. Any other record type is
// ignored by this version, so later manifests can add records without a
// format bump. A malformed known record rejects the whole manifest.
//
// The manifest is published as a GitHub release asset:
//   https://github.com/dcarnicer/kanikey/releases/latest/download/kanikey-firmware.txt
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kk::ota {

constexpr size_t kManifestMaxBytes = 8192;

struct AppImage {
    std::string env;
    std::string file;  // the manifest field as written
    std::string url;   // absolute, resolved against the manifest URL
    uint32_t bytes = 0;
    std::string sha256;  // 64 hex characters
};

struct Manifest {
    uint32_t format = 0;
    std::string version;  // vX.Y.Z as written
    std::vector<AppImage> apps;

    const AppImage* find(std::string_view envName) const;
};

enum class ManifestError : uint8_t {
    Ok,
    Empty,
    TooLarge,
    BadHeader,
    UnsupportedVersion,
    BadVersion,
    BadApp,
    DuplicateApp,
};
const char* manifestErrorString(ManifestError error);

struct ManifestParseResult {
    ManifestError error = ManifestError::Empty;
    unsigned line = 0;  // 1-based line of the error, 0 when not line-specific
    Manifest manifest;
};

ManifestParseResult parseManifest(std::string_view text, std::string_view manifestUrl);

struct FirmwareVersion {
    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t patch = 0;
};

// Accepts "X.Y.Z" or "vX.Y.Z" and nothing else. A build version with a suffix
// (for example a git describe string) is not a release version.
bool parseFirmwareVersion(std::string_view text, FirmwareVersion* out);
int compareFirmwareVersions(const FirmwareVersion& a, const FirmwareVersion& b);

enum class OfferVerdict : uint8_t {
    Offer,
    UpToDate,
    NoImage,
    NoSlot,
    TooLarge,
    RolledBack,
    DevBuild,
    BadManifest,
};

struct OfferInput {
    std::string_view running;          // the running firmware version
    std::string_view env;              // cardputer-adv
    uint32_t slotBytes = 0;            // the next OTA slot, 0 when no layout
    std::string_view rolledBack;       // a version this device rolled back from
    bool allowAnyDevVersion = false;   // dev builds may install any different version
};

struct Offer {
    OfferVerdict verdict = OfferVerdict::BadManifest;
    const AppImage* app = nullptr;
};

Offer offerFor(const Manifest& manifest, const OfferInput& input);
const char* offerVerdictString(OfferVerdict verdict);

// Resolves a relative manifest file against the manifest's own URL. Absolute
// https:// and http:// URLs are returned unchanged; ".." is rejected.
std::string resolveUrl(std::string_view manifestUrl, std::string_view file);

}  // namespace kk::ota

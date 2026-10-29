#include "ota/Manifest.h"

#include <cctype>
#include <cstdlib>
#include <utility>

namespace kk::ota {
namespace {

bool isHex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool validEnv(std::string_view env) {
    if (env.empty() || env.size() > 31) return false;
    for (char c : env) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') continue;
        return false;
    }
    return true;
}

bool parseU32(std::string_view text, uint32_t* out) {
    if (text.empty() || text.size() > 10) return false;
    uint64_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<uint64_t>(c - '0');
        if (value > 0xFFFFFFFFull) return false;
    }
    *out = static_cast<uint32_t>(value);
    return true;
}

std::string_view trimCr(std::string_view line) {
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    return line;
}

std::vector<std::string_view> splitTabs(std::string_view line) {
    std::vector<std::string_view> fields;
    size_t start = 0;
    for (;;) {
        const size_t tab = line.find('\t', start);
        if (tab == std::string_view::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    return fields;
}

bool validFile(std::string_view file) {
    if (file.empty() || file.size() > 255) return false;
    if (file.find("..") != std::string_view::npos) return false;
    for (char c : file) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') return false;
    }
    return true;
}

}  // namespace

const AppImage* Manifest::find(std::string_view envName) const {
    for (const AppImage& app : apps)
        if (app.env == envName) return &app;
    return nullptr;
}

const char* manifestErrorString(ManifestError error) {
    switch (error) {
        case ManifestError::Ok: return "ok";
        case ManifestError::Empty: return "empty";
        case ManifestError::TooLarge: return "too large";
        case ManifestError::BadHeader: return "not a firmware manifest";
        case ManifestError::UnsupportedVersion: return "unsupported manifest version";
        case ManifestError::BadVersion: return "bad version";
        case ManifestError::BadApp: return "bad app record";
        case ManifestError::DuplicateApp: return "duplicate app";
    }
    return "?";
}

bool parseFirmwareVersion(std::string_view text, FirmwareVersion* out) {
    if (!out) return false;
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V'))
        text.remove_prefix(1);
    uint32_t parts[3] = {0, 0, 0};
    int count = 0;
    size_t start = 0;
    for (;;) {
        const size_t dot = text.find('.', start);
        const std::string_view piece =
            dot == std::string_view::npos ? text.substr(start) : text.substr(start, dot - start);
        if (count >= 3 || !parseU32(piece, &parts[count])) return false;
        ++count;
        if (dot == std::string_view::npos) break;
        start = dot + 1;
    }
    if (count != 3) return false;
    *out = FirmwareVersion{parts[0], parts[1], parts[2]};
    return true;
}

int compareFirmwareVersions(const FirmwareVersion& a, const FirmwareVersion& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    return 0;
}

std::string resolveUrl(std::string_view manifestUrl, std::string_view file) {
    if (file.rfind("https://", 0) == 0 || file.rfind("http://", 0) == 0)
        return std::string(file);
    if (file.empty()) return {};
    if (file.front() == '/') {
        const size_t scheme = manifestUrl.find("://");
        if (scheme == std::string_view::npos) return {};
        const size_t hostEnd = manifestUrl.find('/', scheme + 3);
        const std::string_view origin = hostEnd == std::string_view::npos
                                            ? manifestUrl
                                            : manifestUrl.substr(0, hostEnd);
        return std::string(origin) + std::string(file);
    }
    const size_t slash = manifestUrl.rfind('/');
    if (slash == std::string_view::npos) return {};
    return std::string(manifestUrl.substr(0, slash + 1)) + std::string(file);
}

ManifestParseResult parseManifest(std::string_view text, std::string_view manifestUrl) {
    ManifestParseResult result;
    if (text.empty()) {
        result.error = ManifestError::Empty;
        return result;
    }
    if (text.size() > kManifestMaxBytes) {
        result.error = ManifestError::TooLarge;
        return result;
    }

    size_t pos = 0;
    unsigned lineNo = 0;
    bool haveHeader = false;
    bool haveVersion = false;
    bool sawApp = false;
    Manifest manifest;

    while (pos < text.size()) {
        const size_t nl = text.find('\n', pos);
        const size_t end = nl == std::string_view::npos ? text.size() : nl;
        std::string_view line = trimCr(text.substr(pos, end - pos));
        pos = nl == std::string_view::npos ? text.size() : nl + 1;
        ++lineNo;

        if (line.empty()) continue;
        if (!haveHeader) {
            const std::vector<std::string_view> fields = splitTabs(line);
            if (fields.size() != 2 || fields[0] != "#kanikey-firmware") {
                result.error = ManifestError::BadHeader;
                result.line = lineNo;
                return result;
            }
            uint32_t format = 0;
            if (!parseU32(fields[1], &format) || format != 1) {
                result.error = ManifestError::UnsupportedVersion;
                result.line = lineNo;
                return result;
            }
            manifest.format = format;
            haveHeader = true;
            continue;
        }
        if (line.front() == '#') continue;

        const std::vector<std::string_view> fields = splitTabs(line);
        if (fields.empty()) continue;
        if (fields[0] == "version") {
            if (fields.size() != 2 || haveVersion || sawApp) {
                result.error = ManifestError::BadVersion;
                result.line = lineNo;
                return result;
            }
            FirmwareVersion version;
            if (!parseFirmwareVersion(fields[1], &version)) {
                result.error = ManifestError::BadVersion;
                result.line = lineNo;
                return result;
            }
            manifest.version = std::string(fields[1]);
            haveVersion = true;
        } else if (fields[0] == "app") {
            if (!haveVersion || fields.size() != 5) {
                result.error = ManifestError::BadApp;
                result.line = lineNo;
                return result;
            }
            AppImage app;
            app.env = std::string(fields[1]);
            app.file = std::string(fields[2]);
            if (!validEnv(app.env) || !validFile(app.file) ||
                !parseU32(fields[3], &app.bytes) || app.bytes == 0) {
                result.error = ManifestError::BadApp;
                result.line = lineNo;
                return result;
            }
            if (fields[4].size() != 64) {
                result.error = ManifestError::BadApp;
                result.line = lineNo;
                return result;
            }
            for (char c : fields[4]) {
                if (!isHex(c)) {
                    result.error = ManifestError::BadApp;
                    result.line = lineNo;
                    return result;
                }
            }
            app.sha256 = std::string(fields[4]);
            app.url = resolveUrl(manifestUrl, app.file);
            if (app.url.empty()) {
                result.error = ManifestError::BadApp;
                result.line = lineNo;
                return result;
            }
            if (manifest.find(app.env)) {
                result.error = ManifestError::DuplicateApp;
                result.line = lineNo;
                return result;
            }
            manifest.apps.push_back(std::move(app));
            sawApp = true;
        }
        // Unknown record types are ignored on purpose.
    }

    if (!haveHeader) {
        result.error = ManifestError::BadHeader;
        return result;
    }
    if (!haveVersion) {
        result.error = ManifestError::BadVersion;
        return result;
    }
    result.error = ManifestError::Ok;
    result.manifest = std::move(manifest);
    return result;
}

Offer offerFor(const Manifest& manifest, const OfferInput& input) {
    Offer offer;
    FirmwareVersion running;
    const bool runningIsRelease = parseFirmwareVersion(input.running, &running);
    if (!runningIsRelease && !input.allowAnyDevVersion) {
        offer.verdict = OfferVerdict::DevBuild;
        return offer;
    }
    const AppImage* app = manifest.find(input.env);
    if (!app) {
        offer.verdict = OfferVerdict::NoImage;
        return offer;
    }
    FirmwareVersion candidate;
    if (!parseFirmwareVersion(manifest.version, &candidate)) {
        offer.verdict = OfferVerdict::BadManifest;
        return offer;
    }
    if (!input.rolledBack.empty()) {
        FirmwareVersion failed;
        bool same = manifest.version == input.rolledBack;
        if (!same && parseFirmwareVersion(input.rolledBack, &failed))
            same = compareFirmwareVersions(candidate, failed) == 0;
        if (same) {
            offer.verdict = OfferVerdict::RolledBack;
            return offer;
        }
    }
    if (runningIsRelease && compareFirmwareVersions(candidate, running) <= 0) {
        offer.verdict = OfferVerdict::UpToDate;
        return offer;
    }
    if (!runningIsRelease && manifest.version == input.running) {
        offer.verdict = OfferVerdict::UpToDate;
        return offer;
    }
    if (input.slotBytes == 0) {
        offer.verdict = OfferVerdict::NoSlot;
        return offer;
    }
    if (app->bytes > input.slotBytes) {
        offer.verdict = OfferVerdict::TooLarge;
        return offer;
    }
    offer.verdict = OfferVerdict::Offer;
    offer.app = app;
    return offer;
}

const char* offerVerdictString(OfferVerdict verdict) {
    switch (verdict) {
        case OfferVerdict::Offer: return "offer";
        case OfferVerdict::UpToDate: return "up-to-date";
        case OfferVerdict::NoImage: return "no image for this build";
        case OfferVerdict::NoSlot: return "no update slot";
        case OfferVerdict::TooLarge: return "image too large";
        case OfferVerdict::RolledBack: return "rolled back";
        case OfferVerdict::DevBuild: return "development build";
        case OfferVerdict::BadManifest: return "bad manifest";
    }
    return "?";
}

}  // namespace kk::ota

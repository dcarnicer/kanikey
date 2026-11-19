#include <unity.h>

#include <string>

#include "ota/HttpHeader.h"
#include "ota/Manifest.h"

using namespace kk::ota;

namespace {

constexpr const char* kSha =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

std::string manifestWithPack(const std::string& packLine) {
    return std::string("#kanikey-firmware\t1\nversion\tv0.2.0\n") +
           "app\tcardputer-adv\tkanikey-cardputer-adv.bin\t1423021\t" + kSha + "\n" +
           packLine;
}

}  // namespace

// The helper must keep the Location in the middle of GitHub's header block,
// where a ~4 KB Content-Security-Policy line comes first or last.
static void test_header_location_after_long_csp() {
    ResponseHeaders headers;
    const std::string csp = "Content-Security-Policy: " + std::string(4000, 'a');
    TEST_ASSERT_TRUE(applyHeaderLine(csp, &headers));
    TEST_ASSERT_TRUE(headers.location.empty());
    TEST_ASSERT_TRUE(applyHeaderLine("Location: https://release.example/x.bin", &headers));
    TEST_ASSERT_EQUAL_STRING("https://release.example/x.bin", headers.location.c_str());
    TEST_ASSERT_EQUAL_INT(-1, static_cast<int>(headers.contentLength));
}

static void test_header_location_before_long_csp() {
    ResponseHeaders headers;
    TEST_ASSERT_TRUE(applyHeaderLine("Location: https://release.example/x.bin", &headers));
    const std::string csp = "Content-Security-Policy: " + std::string(4000, 'b');
    TEST_ASSERT_TRUE(applyHeaderLine(csp, &headers));
    TEST_ASSERT_EQUAL_STRING("https://release.example/x.bin", headers.location.c_str());
}

static void test_header_long_csp_after_location() {
    ResponseHeaders headers;
    TEST_ASSERT_TRUE(applyHeaderLine("Content-Length: 7", &headers));
    TEST_ASSERT_TRUE(applyHeaderLine("Location: https://a.test/x", &headers));
    const std::string csp = "Set-Cookie: " + std::string(4096, 'c');
    TEST_ASSERT_TRUE(applyHeaderLine(csp, &headers));
    TEST_ASSERT_FALSE(applyHeaderLine("\r\n", &headers));  // end of block
    TEST_ASSERT_EQUAL_STRING("https://a.test/x", headers.location.c_str());
    TEST_ASSERT_EQUAL_INT(7, static_cast<int>(headers.contentLength));
}

static void test_header_crlf_trimmed() {
    ResponseHeaders headers;
    TEST_ASSERT_TRUE(applyHeaderLine("  Location:   https://a.test/x \r\n", &headers));
    TEST_ASSERT_EQUAL_STRING("https://a.test/x", headers.location.c_str());
    TEST_ASSERT_TRUE(applyHeaderLine(" Content-Length:\t42 \r\n", &headers));
    TEST_ASSERT_EQUAL_INT(42, static_cast<int>(headers.contentLength));
}

static void test_header_name_case_insensitive() {
    ResponseHeaders headers;
    TEST_ASSERT_TRUE(applyHeaderLine("lOcAtIoN: https://a.test/x", &headers));
    TEST_ASSERT_EQUAL_STRING("https://a.test/x", headers.location.c_str());
    TEST_ASSERT_TRUE(applyHeaderLine("CONTENT-LENGTH: 9", &headers));
    TEST_ASSERT_EQUAL_INT(9, static_cast<int>(headers.contentLength));
    TEST_ASSERT_TRUE(applyHeaderLine("location: https://b.test/y", &headers));
    TEST_ASSERT_EQUAL_STRING("https://b.test/y", headers.location.c_str());
}

static void test_header_location_4096_accepted() {
    ResponseHeaders headers;
    const std::string value(4096, 'd');
    TEST_ASSERT_TRUE(applyHeaderLine("Location: " + value, &headers));
    TEST_ASSERT_EQUAL_UINT32(kMaxLocationChars, headers.location.size());
    TEST_ASSERT_EQUAL_STRING(value.c_str(), headers.location.c_str());
}

static void test_header_location_over_4096_rejected() {
    ResponseHeaders headers;
    const std::string value(4097, 'e');
    // Consumed and dropped, not a "stop" signal: the caller then sees an empty
    // Location and reports an HTTP error instead of following a cut URL.
    TEST_ASSERT_TRUE(applyHeaderLine("Location: " + value, &headers));
    TEST_ASSERT_TRUE(headers.location.empty());
}

static void test_header_content_length_parsed() {
    ResponseHeaders headers;
    TEST_ASSERT_TRUE(applyHeaderLine("Content-Length: 0", &headers));
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(headers.contentLength));
    TEST_ASSERT_TRUE(applyHeaderLine("Content-Length: 1461584", &headers));
    TEST_ASSERT_EQUAL_INT(1461584, static_cast<int>(headers.contentLength));
    // Invalid values leave the last good one in place.
    TEST_ASSERT_TRUE(applyHeaderLine("Content-Length: 12x", &headers));
    TEST_ASSERT_EQUAL_INT(1461584, static_cast<int>(headers.contentLength));
    TEST_ASSERT_TRUE(applyHeaderLine("Content-Length:", &headers));
    TEST_ASSERT_EQUAL_INT(1461584, static_cast<int>(headers.contentLength));
}

static void test_manifest_pack_record_parse() {
    const std::string text = manifestWithPack(
        std::string("pack\tfont\tglyphs.kgp\t9360216\t") + kSha + "\n");
    const ManifestParseResult r = parseManifest(text, "https://x/y/manifest.txt");
    TEST_ASSERT_EQUAL(ManifestError::Ok, r.error);
    TEST_ASSERT_EQUAL(1, r.manifest.packs.size());
    const PackImage* pack = r.manifest.findPack("font");
    TEST_ASSERT_NOT_NULL(pack);
    TEST_ASSERT_EQUAL_STRING("font", pack->type.c_str());
    TEST_ASSERT_EQUAL_STRING("glyphs.kgp", pack->file.c_str());
    TEST_ASSERT_EQUAL_UINT32(9360216u, pack->bytes);
    TEST_ASSERT_EQUAL_STRING(kSha, pack->sha256.c_str());
    TEST_ASSERT_EQUAL_STRING("https://x/y/glyphs.kgp", pack->url.c_str());
    TEST_ASSERT_NULL(r.manifest.findPack("radical"));
    // The firmware offer path still sees the app record.
    TEST_ASSERT_NOT_NULL(r.manifest.find("cardputer-adv"));
}

static void test_manifest_pack_record_absolute_url() {
    const std::string text =
        manifestWithPack(std::string("pack\tfont\thttps://cdn.test/glyphs.kgp\t12\t") + kSha +
                         "\n");
    const ManifestParseResult r = parseManifest(text, "https://x/y/manifest.txt");
    TEST_ASSERT_EQUAL(ManifestError::Ok, r.error);
    TEST_ASSERT_EQUAL_STRING("https://cdn.test/glyphs.kgp",
                             r.manifest.packs[0].url.c_str());
}

static void test_manifest_pack_missing() {
    // A manifest without a pack record is still valid: the firmware update path
    // does not require one.
    const ManifestParseResult none =
        parseManifest(manifestWithPack(""), "https://x/y/manifest.txt");
    TEST_ASSERT_EQUAL(ManifestError::Ok, none.error);
    TEST_ASSERT_EQUAL(0, none.manifest.packs.size());
    TEST_ASSERT_NULL(none.manifest.findPack("font"));

    // A pack record with missing fields is malformed and rejects the manifest.
    const ManifestParseResult shortRecord =
        parseManifest(manifestWithPack("pack\tfont\tglyphs.kgp\t12\n"), "https://x/y");
    TEST_ASSERT_EQUAL(ManifestError::BadPack, shortRecord.error);
    TEST_ASSERT_EQUAL_UINT(4, shortRecord.line);
}

static void test_manifest_pack_bad_hash() {
    const std::string shortHash(63, 'a');
    ManifestParseResult r = parseManifest(
        manifestWithPack("pack\tfont\tglyphs.kgp\t12\t" + shortHash + "\n"), "https://x/y");
    TEST_ASSERT_EQUAL(ManifestError::BadPack, r.error);

    std::string nonHex(64, 'a');
    nonHex[10] = 'z';
    r = parseManifest(manifestWithPack("pack\tfont\tglyphs.kgp\t12\t" + nonHex + "\n"),
                      "https://x/y");
    TEST_ASSERT_EQUAL(ManifestError::BadPack, r.error);
}

static void test_manifest_pack_wrong_size() {
    ManifestParseResult r = parseManifest(
        manifestWithPack(std::string("pack\tfont\tglyphs.kgp\t0\t") + kSha + "\n"),
        "https://x/y");
    TEST_ASSERT_EQUAL(ManifestError::BadPack, r.error);
    r = parseManifest(
        manifestWithPack(std::string("pack\tfont\tglyphs.kgp\tabc\t") + kSha + "\n"),
        "https://x/y");
    TEST_ASSERT_EQUAL(ManifestError::BadPack, r.error);
    r = parseManifest(
        manifestWithPack(std::string("pack\tfont\tglyphs.kgp\t99999999999\t") + kSha + "\n"),
        "https://x/y");
    TEST_ASSERT_EQUAL(ManifestError::BadPack, r.error);
}

static void test_manifest_pack_duplicate() {
    const std::string line =
        std::string("pack\tfont\tglyphs.kgp\t12\t") + kSha + "\n";
    const ManifestParseResult r =
        parseManifest(manifestWithPack(line + line), "https://x/y");
    TEST_ASSERT_EQUAL(ManifestError::DuplicatePack, r.error);
}

void run_ota_tests() {
    RUN_TEST(test_header_location_after_long_csp);
    RUN_TEST(test_header_location_before_long_csp);
    RUN_TEST(test_header_long_csp_after_location);
    RUN_TEST(test_header_crlf_trimmed);
    RUN_TEST(test_header_name_case_insensitive);
    RUN_TEST(test_header_location_4096_accepted);
    RUN_TEST(test_header_location_over_4096_rejected);
    RUN_TEST(test_header_content_length_parsed);
    RUN_TEST(test_manifest_pack_record_parse);
    RUN_TEST(test_manifest_pack_record_absolute_url);
    RUN_TEST(test_manifest_pack_missing);
    RUN_TEST(test_manifest_pack_bad_hash);
    RUN_TEST(test_manifest_pack_wrong_size);
    RUN_TEST(test_manifest_pack_duplicate);
}

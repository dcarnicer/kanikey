// FIX-27: the subject cache is streamed through 1 KB blocks in both directions
// instead of building the whole payload (save) or the whole file (load) in one
// contiguous vector. These tests pin the on-disk bytes to the old vector
// encoder and cover short sources, CRC and size errors, sink failures and the
// string cap.
#include <unity.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "wk/Cache.h"
#include "wk/Crc32.h"

using namespace kk;

namespace {

// Must match kMaxString in wk/Cache.cpp.
constexpr size_t kMaxCacheString = 16384;

void setLe32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

class MemorySink : public ByteSink {
public:
    bool write(const uint8_t* data, size_t size) override {
        bytes.insert(bytes.end(), data, data + size);
        return true;
    }

    std::vector<uint8_t> bytes;
};

class FailingSink : public ByteSink {
public:
    explicit FailingSink(size_t limit) : limit_(limit) {}
    bool write(const uint8_t*, size_t size) override {
        if (written_ + size > limit_) return false;
        written_ += size;
        return true;
    }
    size_t written() const { return written_; }

private:
    size_t limit_;
    size_t written_ = 0;
};

class MemorySource : public ByteSource {
public:
    explicit MemorySource(const std::vector<uint8_t>& bytes) : bytes_(bytes) {}
    size_t read(uint8_t* data, size_t size) override {
        const size_t left = bytes_.size() - pos_;
        const size_t n = size < left ? size : left;
        if (n) std::memcpy(data, bytes_.data() + pos_, n);
        pos_ += n;
        return n;
    }

private:
    const std::vector<uint8_t>& bytes_;
    size_t pos_ = 0;
};

// A source that hands out 1-7 bytes per call, so integers and strings cross
// refills.
class ChunkedSource : public ByteSource {
public:
    explicit ChunkedSource(const std::vector<uint8_t>& bytes) : bytes_(bytes) {}
    size_t read(uint8_t* data, size_t size) override {
        const size_t left = bytes_.size() - pos_;
        chunk_ = chunk_ % 7 + 1;
        const size_t n = std::min({size, left, chunk_});
        if (n) std::memcpy(data, bytes_.data() + pos_, n);
        pos_ += n;
        return n;
    }

private:
    const std::vector<uint8_t>& bytes_;
    size_t pos_ = 0;
    size_t chunk_ = 0;
};

Subject kanjiSubject() {
    Subject s;
    s.id = 101;
    s.type = SubjectType::Kanji;
    s.level = 5;
    s.characters = "食";
    s.meanings = {{"eat", true, true}, {"food", false, true}};
    s.readings = {{"しょく", true, true, ReadingType::Onyomi},
                  {"た.べる", false, true, ReadingType::Kunyomi}};
    s.meaningSynonyms = {"consume"};
    s.whitelist = {"devour"};
    s.blacklist = {"drink"};
    s.componentSubjectIds = {1, 2};
    s.visuallySimilarSubjectIds = {7, 8};
    s.amalgamationSubjectIds = {9};
    s.meaningMnemonic = "A synthetic mnemonic for the cache test.";
    s.readingMnemonic = "Another synthetic mnemonic.";
    s.meaningHint = "A hint.";
    s.readingHint = "A reading hint.";
    s.meaningNote = "A note.";
    s.readingNote = "Another note.";
    s.meaningCorrect = 8;
    s.meaningIncorrect = 2;
    s.readingCorrect = 7;
    s.readingIncorrect = 1;
    s.meaningMaxStreak = 5;
    s.meaningCurrentStreak = 3;
    s.readingMaxStreak = 4;
    s.readingCurrentStreak = 2;
    s.percentageCorrect = 83;
    s.hasReviewStats = true;
    return s;
}

Subject vocabularySubject() {
    Subject s;
    s.id = 202;
    s.type = SubjectType::Vocabulary;
    s.level = 7;
    s.characters = "大人";
    s.meanings = {{"adult", true, true}};
    s.readings = {{"おとな", true, true, ReadingType::Kunyomi}};
    s.partsOfSpeech = {"noun", "proper noun"};
    s.contextSentences = {{"これは大人です。", "This is an adult."},
                          {"大人になりました。", "Became an adult."}};
    // Long mnemonics near the 16 KB string cap.
    s.meaningMnemonic = std::string(16000, 'm');
    s.readingMnemonic = std::string(12000, 'r');
    s.meaningHint = std::string(9000, 'h');
    s.readingHint = std::string(4000, 'i');
    return s;
}

Subject radicalSubject() {
    Subject s;
    s.id = 303;
    s.type = SubjectType::Radical;
    s.level = 1;
    s.meanings = {{"ground", true, true}};
    s.blacklist = {"floor"};
    return s;
}

std::vector<Subject> realisticSubjects() {
    return {kanjiSubject(), vocabularySubject(), radicalSubject()};
}

CacheMeta testMeta() {
    CacheMeta meta;
    meta.savedAt = 1760000000u;
    meta.accountKey = cacheAccountKey("test-token");
    return meta;
}

// Byte-for-byte comparison against the vector encoder.
void assertSubjectsEqual(const std::vector<Subject>& want, const std::vector<Subject>& got) {
    std::vector<uint8_t> wantBytes;
    std::vector<uint8_t> gotBytes;
    TEST_ASSERT_TRUE(encodeSubjectCache(testMeta(), want, &wantBytes));
    TEST_ASSERT_TRUE(encodeSubjectCache(testMeta(), got, &gotBytes));
    TEST_ASSERT_EQUAL(static_cast<int>(wantBytes.size()), static_cast<int>(gotBytes.size()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(wantBytes.data(), gotBytes.data(), wantBytes.size());
}

void assertMetaEqual(const CacheMeta& want, const CacheMeta& got) {
    TEST_ASSERT_EQUAL_UINT32(want.savedAt, got.savedAt);
    TEST_ASSERT_EQUAL_UINT32(want.accountKey, got.accountKey);
}

}  // namespace

static void stream_write_matches_vector_encoder() {
    const std::vector<Subject> subjects = realisticSubjects();

    std::vector<uint8_t> vectorBytes;
    TEST_ASSERT_TRUE(encodeSubjectCache(testMeta(), subjects, &vectorBytes));

    MemorySink sink;
    TEST_ASSERT_TRUE(writeSubjectCache(testMeta(), subjects, &sink));
    TEST_ASSERT_EQUAL(static_cast<int>(vectorBytes.size()), static_cast<int>(sink.bytes.size()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(vectorBytes.data(), sink.bytes.data(), vectorBytes.size());
    // Header: KKCS, the subject version (13), savedAt, account key.
    TEST_ASSERT_EQUAL_UINT8('K', sink.bytes[0]);
    TEST_ASSERT_EQUAL_UINT8('S', sink.bytes[3]);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(kSubjectsVersion), sink.bytes[4]);
}

static void stream_read_decodes_vector_encoded_file() {
    const std::vector<Subject> subjects = realisticSubjects();
    std::vector<uint8_t> bytes;
    TEST_ASSERT_TRUE(encodeSubjectCache(testMeta(), subjects, &bytes));

    MemorySource source(bytes);
    SubjectCache out;
    TEST_ASSERT_TRUE(readSubjectCache(&source, bytes.size(), &out));
    assertMetaEqual(testMeta(), out.meta);
    TEST_ASSERT_EQUAL(static_cast<int>(subjects.size()), static_cast<int>(out.subjects.size()));
    assertSubjectsEqual(subjects, out.subjects);
    TEST_ASSERT_EQUAL_STRING("食", out.subjects[0].characters.c_str());
    TEST_ASSERT_EQUAL(2, out.subjects[1].contextSentences.size());
    TEST_ASSERT_EQUAL(static_cast<int>(16000), static_cast<int>(out.subjects[1].meaningMnemonic.size()));
}

static void stream_roundtrip_small_source_chunks() {
    const std::vector<Subject> subjects = realisticSubjects();
    std::vector<uint8_t> bytes;
    TEST_ASSERT_TRUE(encodeSubjectCache(testMeta(), subjects, &bytes));

    ChunkedSource source(bytes);
    SubjectCache out;
    TEST_ASSERT_TRUE(readSubjectCache(&source, bytes.size(), &out));
    assertMetaEqual(testMeta(), out.meta);
    assertSubjectsEqual(subjects, out.subjects);

    // A file written by the streaming encoder also reads back through the
    // small-chunk source.
    MemorySink sink;
    TEST_ASSERT_TRUE(writeSubjectCache(testMeta(), subjects, &sink));
    ChunkedSource again(sink.bytes);
    SubjectCache second;
    TEST_ASSERT_TRUE(readSubjectCache(&again, sink.bytes.size(), &second));
    assertSubjectsEqual(subjects, second.subjects);
}

static void stream_read_rejects_bad_crc() {
    std::vector<uint8_t> bytes;
    TEST_ASSERT_TRUE(encodeSubjectCache(testMeta(), realisticSubjects(), &bytes));
    bytes[bytes.size() - 1] ^= 0x01;

    MemorySource source(bytes);
    SubjectCache out;
    TEST_ASSERT_FALSE(readSubjectCache(&source, bytes.size(), &out));
    TEST_ASSERT_TRUE(out.subjects.empty());
}

static void stream_read_rejects_truncated_file() {
    std::vector<uint8_t> bytes;
    TEST_ASSERT_TRUE(encodeSubjectCache(testMeta(), realisticSubjects(), &bytes));
    bytes.pop_back();

    MemorySource source(bytes);
    SubjectCache out;
    TEST_ASSERT_FALSE(readSubjectCache(&source, bytes.size(), &out));
    TEST_ASSERT_TRUE(out.subjects.empty());

    // Header alone is too small, and a file size below the header is refused
    // before any read.
    MemorySource tiny(bytes);
    TEST_ASSERT_FALSE(readSubjectCache(&tiny, kCacheHeaderBytes - 1, &out));

    // The header describes the full file but the source ends early: the read
    // fails mid-payload.
    std::vector<uint8_t> complete;
    TEST_ASSERT_TRUE(encodeSubjectCache(testMeta(), realisticSubjects(), &complete));
    std::vector<uint8_t> shortBytes(complete.begin(), complete.end() - 1);
    MemorySource shortSource(shortBytes);
    TEST_ASSERT_FALSE(readSubjectCache(&shortSource, complete.size(), &out));
    TEST_ASSERT_TRUE(out.subjects.empty());
}

static void stream_read_rejects_trailing_bytes() {
    std::vector<uint8_t> bytes;
    TEST_ASSERT_TRUE(encodeSubjectCache(testMeta(), realisticSubjects(), &bytes));
    const size_t payload = bytes.size() - kCacheHeaderBytes;
    bytes.push_back(0x7F);
    // Make the header describe the extra byte, with a valid CRC over it, so
    // only the "payload fully consumed" check can reject the file.
    setLe32(bytes.data() + 16, static_cast<uint32_t>(payload + 1));
    setLe32(bytes.data() + 20, crc32(bytes.data() + kCacheHeaderBytes, payload + 1));

    MemorySource source(bytes);
    SubjectCache out;
    TEST_ASSERT_FALSE(readSubjectCache(&source, bytes.size(), &out));
    TEST_ASSERT_TRUE(out.subjects.empty());
}

static void stream_read_leaves_output_untouched_on_error() {
    std::vector<uint8_t> bytes;
    TEST_ASSERT_TRUE(encodeSubjectCache(testMeta(), realisticSubjects(), &bytes));
    bytes[bytes.size() - 1] ^= 0x40;

    SubjectCache out;
    out.meta.savedAt = 1234;
    out.meta.accountKey = 5678;
    Subject existing;
    existing.id = 999;
    out.subjects.push_back(existing);

    MemorySource source(bytes);
    TEST_ASSERT_FALSE(readSubjectCache(&source, bytes.size(), &out));
    TEST_ASSERT_EQUAL_UINT32(1234, out.meta.savedAt);
    TEST_ASSERT_EQUAL_UINT32(5678, out.meta.accountKey);
    TEST_ASSERT_EQUAL(1, out.subjects.size());
    TEST_ASSERT_EQUAL_UINT32(999, out.subjects[0].id);

    TEST_ASSERT_FALSE(readSubjectCache(nullptr, bytes.size(), &out));
    MemorySource good(bytes);
    TEST_ASSERT_FALSE(readSubjectCache(&good, bytes.size(), nullptr));
}

static void stream_write_reports_sink_failure() {
    const std::vector<Subject> subjects = realisticSubjects();

    // 64 bytes allowed: the 24-byte header lands, the first 1 KB payload block
    // does not.
    FailingSink early(64);
    TEST_ASSERT_FALSE(writeSubjectCache(testMeta(), subjects, &early));
    TEST_ASSERT_EQUAL(kCacheHeaderBytes, early.written());

    // Failing on the header write itself.
    FailingSink none(0);
    TEST_ASSERT_FALSE(writeSubjectCache(testMeta(), subjects, &none));
    TEST_ASSERT_EQUAL(0, none.written());

    TEST_ASSERT_FALSE(writeSubjectCache(testMeta(), subjects, nullptr));
}

static void stream_write_rejects_too_long_string() {
    std::vector<Subject> subjects = {kanjiSubject()};
    subjects[0].meaningMnemonic = std::string(kMaxCacheString + 1, 'x');

    MemorySink sink;
    TEST_ASSERT_FALSE(writeSubjectCache(testMeta(), subjects, &sink));
    TEST_ASSERT_TRUE(sink.bytes.empty());

    // Exactly at the cap still encodes, and the vector encoder agrees.
    subjects[0].meaningMnemonic = std::string(kMaxCacheString, 'x');
    MemorySink okSink;
    std::vector<uint8_t> vectorBytes;
    TEST_ASSERT_TRUE(writeSubjectCache(testMeta(), subjects, &okSink));
    TEST_ASSERT_TRUE(encodeSubjectCache(testMeta(), subjects, &vectorBytes));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(vectorBytes.data(), okSink.bytes.data(), vectorBytes.size());
}

// AUD-02: the chosen clip URL of each voice is persisted (KKCS v13). A URL
// round-trips through both encoders; an empty URL stays empty.
static void subject_audio_urls_round_trip() {
    Subject vocab = vocabularySubject();
    vocab.audioUrl[0] = "https://audio.example.test/clips/0123456789/female-ichiban.mp3";
    vocab.audioUrl[1] = "";  // no male clip for this word
    const std::vector<Subject> subjects = {vocab, kanjiSubject()};

    std::vector<uint8_t> bytes;
    TEST_ASSERT_TRUE(encodeSubjectCache(testMeta(), subjects, &bytes));
    SubjectCache out;
    TEST_ASSERT_TRUE(decodeSubjectCache(bytes.data(), bytes.size(), &out));
    TEST_ASSERT_EQUAL(2, out.subjects.size());
    TEST_ASSERT_EQUAL_STRING(vocab.audioUrl[0].c_str(), out.subjects[0].audioUrl[0].c_str());
    TEST_ASSERT_TRUE(out.subjects[0].audioUrl[1].empty());
    TEST_ASSERT_TRUE(out.subjects[1].audioUrl[0].empty());
    TEST_ASSERT_TRUE(out.subjects[1].audioUrl[1].empty());

    // The streaming reader gives the same URLs.
    MemorySink sink;
    TEST_ASSERT_TRUE(writeSubjectCache(testMeta(), subjects, &sink));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data(), sink.bytes.data(), bytes.size());
    MemorySource source(sink.bytes);
    SubjectCache streamed;
    TEST_ASSERT_TRUE(readSubjectCache(&source, sink.bytes.size(), &streamed));
    TEST_ASSERT_EQUAL_STRING(vocab.audioUrl[0].c_str(), streamed.subjects[0].audioUrl[0].c_str());
    TEST_ASSERT_TRUE(streamed.subjects[0].audioUrl[1].empty());
}

// The URL field is the last one of a subject: a file cut inside it, or with the
// URL bytes changed into a longer string, is refused, never half-read.
static void subject_audio_urls_reject_truncation() {
    Subject vocab = vocabularySubject();
    vocab.audioUrl[1] = "https://audio.example.test/clips/male.mp3";
    std::vector<uint8_t> bytes;
    TEST_ASSERT_TRUE(encodeSubjectCache(testMeta(), {vocab}, &bytes));
    std::vector<uint8_t> cut(bytes.begin(), bytes.end() - 3);
    SubjectCache out;
    TEST_ASSERT_FALSE(decodeSubjectCache(cut.data(), cut.size(), &out));
    MemorySource source(cut);
    TEST_ASSERT_FALSE(readSubjectCache(&source, cut.size(), &out));
}

// The subject file is read only at kSubjectsVersion. A v12 file (no URLs, the
// old KKCS) and a future version are refused, so the batch is fetched again.
static void subject_cache_other_version_rejected() {
    std::vector<uint8_t> bytes;
    TEST_ASSERT_TRUE(encodeSubjectCache(testMeta(), realisticSubjects(), &bytes));
    TEST_ASSERT_EQUAL_UINT16(kSubjectsVersion, static_cast<uint16_t>(bytes[4] | (bytes[5] << 8)));

    for (const uint16_t version : {static_cast<uint16_t>(kSubjectsVersion - 1),
                                   static_cast<uint16_t>(kCacheVersion),
                                   static_cast<uint16_t>(kSubjectsVersion + 1)}) {
        std::vector<uint8_t> bad = bytes;
        bad[4] = static_cast<uint8_t>(version);
        bad[5] = static_cast<uint8_t>(version >> 8);
        SubjectCache out;
        TEST_ASSERT_FALSE(decodeSubjectCache(bad.data(), bad.size(), &out));
        MemorySource source(bad);
        TEST_ASSERT_FALSE(readSubjectCache(&source, bad.size(), &out));
    }
    SubjectCache out;
    TEST_ASSERT_TRUE(decodeSubjectCache(bytes.data(), bytes.size(), &out));
}

void run_cache_tests() {
    RUN_TEST(subject_audio_urls_round_trip);
    RUN_TEST(subject_audio_urls_reject_truncation);
    RUN_TEST(subject_cache_other_version_rejected);
    RUN_TEST(stream_write_matches_vector_encoder);
    RUN_TEST(stream_read_decodes_vector_encoded_file);
    RUN_TEST(stream_roundtrip_small_source_chunks);
    RUN_TEST(stream_read_rejects_bad_crc);
    RUN_TEST(stream_read_rejects_truncated_file);
    RUN_TEST(stream_read_rejects_trailing_bytes);
    RUN_TEST(stream_read_leaves_output_untouched_on_error);
    RUN_TEST(stream_write_reports_sink_failure);
    RUN_TEST(stream_write_rejects_too_long_string);
}

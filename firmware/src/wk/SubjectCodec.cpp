#include "wk/SubjectCodec.h"

#include <cstring>
#include <string_view>
#include <utility>

namespace kk {

namespace {
constexpr uint16_t kMaxMeanings = 32;
constexpr uint16_t kMaxReadings = 32;
constexpr uint16_t kMaxList = 64;
constexpr uint16_t kMaxContextSentences = 8;
constexpr uint16_t kMaxString = 16384;

void put16(std::vector<uint8_t>* out, uint16_t v) {
    out->push_back(static_cast<uint8_t>(v));
    out->push_back(static_cast<uint8_t>(v >> 8));
}
void put32(std::vector<uint8_t>* out, uint32_t v) {
    out->push_back(static_cast<uint8_t>(v));
    out->push_back(static_cast<uint8_t>(v >> 8));
    out->push_back(static_cast<uint8_t>(v >> 16));
    out->push_back(static_cast<uint8_t>(v >> 24));
}
uint16_t get16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t get32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

class Writer {
public:
    void u8(uint8_t v) {
        if (ok_) out_.push_back(v);
    }
    void u16(uint16_t v) {
        if (ok_) put16(&out_, v);
    }
    void u32(uint32_t v) {
        if (ok_) put32(&out_, v);
    }
    void str(std::string_view s) {
        if (!ok_) return;
        if (s.size() > kMaxString) {
            ok_ = false;
            return;
        }
        u16(static_cast<uint16_t>(s.size()));
        if (!ok_) return;
        out_.insert(out_.end(), s.begin(), s.end());
    }
    void fail() { ok_ = false; }
    bool ok() const { return ok_; }
    std::vector<uint8_t>& bytes() { return out_; }

private:
    std::vector<uint8_t> out_;
    bool ok_ = true;
};

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    bool u8(uint8_t* v) {
        if (!ok_ || pos_ + 1 > size_) return fail();
        *v = data_[pos_++];
        return true;
    }
    bool u16(uint16_t* v) {
        uint8_t b[2];
        if (!bytes(b, sizeof b)) return false;
        *v = get16(b);
        return true;
    }
    bool u32(uint32_t* v) {
        uint8_t b[4];
        if (!bytes(b, sizeof b)) return false;
        *v = get32(b);
        return true;
    }
    bool str(std::string* out) {
        uint16_t n = 0;
        if (!u16(&n) || n > kMaxString) return fail();
        if (pos_ + n > size_) return fail();
        out->assign(reinterpret_cast<const char*>(data_ + pos_), n);
        pos_ += n;
        return true;
    }
    bool done() const { return ok_ && pos_ == size_; }
    bool ok() const { return ok_; }

private:
    bool fail() {
        ok_ = false;
        return false;
    }
    bool bytes(uint8_t* out, size_t n) {
        if (!ok_ || pos_ + n > size_) return fail();
        std::memcpy(out, data_ + pos_, n);
        pos_ += n;
        return true;
    }

    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    size_t pos_ = 0;
    bool ok_ = true;
};

void encodeMeaning(Writer& w, const Meaning& m) {
    w.str(m.text);
    w.u8(static_cast<uint8_t>((m.primary ? 1 : 0) | (m.accepted ? 2 : 0)));
}

bool decodeMeaning(Reader& r, Meaning* m) {
    uint8_t flags = 0;
    if (!r.str(&m->text) || !r.u8(&flags)) return false;
    m->primary = (flags & 1) != 0;
    m->accepted = (flags & 2) != 0;
    return true;
}

void encodeReading(Writer& w, const Reading& reading) {
    w.str(reading.text);
    w.u8(static_cast<uint8_t>((reading.primary ? 1 : 0) | (reading.accepted ? 2 : 0)));
    w.u8(static_cast<uint8_t>(reading.type));
}

bool decodeReading(Reader& r, Reading* reading) {
    uint8_t flags = 0;
    uint8_t type = 0;
    if (!r.str(&reading->text) || !r.u8(&flags) || !r.u8(&type)) return false;
    if (type > static_cast<uint8_t>(ReadingType::Nanori)) return false;
    reading->primary = (flags & 1) != 0;
    reading->accepted = (flags & 2) != 0;
    reading->type = static_cast<ReadingType>(type);
    return true;
}

void encodeStrings(Writer& w, const std::vector<std::string>& list) {
    if (list.size() > kMaxList) {
        w.fail();
        return;
    }
    w.u16(static_cast<uint16_t>(list.size()));
    for (const std::string& s : list) w.str(s);
}

bool decodeStrings(Reader& r, std::vector<std::string>* out) {
    uint16_t count = 0;
    if (!r.u16(&count) || count > kMaxList) return false;
    out->clear();
    out->reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        std::string s;
        if (!r.str(&s)) return false;
        out->push_back(std::move(s));
    }
    return true;
}

}  // namespace

bool subjectEncodePayload(const Subject& s, std::vector<uint8_t>* out) {
    if (!out) return false;
    Writer w;
    w.u32(s.id);
    w.u8(static_cast<uint8_t>(s.type));
    w.u8(s.level);
    w.str(s.characters);

    if (s.meanings.size() > kMaxMeanings) return false;
    w.u16(static_cast<uint16_t>(s.meanings.size()));
    for (const Meaning& m : s.meanings) encodeMeaning(w, m);

    if (s.readings.size() > kMaxReadings) return false;
    w.u16(static_cast<uint16_t>(s.readings.size()));
    for (const Reading& r : s.readings) encodeReading(w, r);

    encodeStrings(w, s.meaningSynonyms);
    encodeStrings(w, s.whitelist);
    encodeStrings(w, s.blacklist);

    if (s.componentSubjectIds.size() > kMaxList) return false;
    w.u16(static_cast<uint16_t>(s.componentSubjectIds.size()));
    for (uint32_t id : s.componentSubjectIds) w.u32(id);

    if (s.visuallySimilarSubjectIds.size() > kMaxList ||
        s.amalgamationSubjectIds.size() > kMaxList)
        return false;
    w.u16(static_cast<uint16_t>(s.visuallySimilarSubjectIds.size()));
    for (uint32_t id : s.visuallySimilarSubjectIds) w.u32(id);
    w.u16(static_cast<uint16_t>(s.amalgamationSubjectIds.size()));
    for (uint32_t id : s.amalgamationSubjectIds) w.u32(id);

    w.str(s.meaningMnemonic);
    w.str(s.readingMnemonic);
    w.str(s.meaningHint);
    w.str(s.readingHint);
    w.str(s.meaningNote);
    w.str(s.readingNote);
    encodeStrings(w, s.partsOfSpeech);
    if (s.contextSentences.size() > kMaxContextSentences) return false;
    w.u16(static_cast<uint16_t>(s.contextSentences.size()));
    for (const ContextSentence& sentence : s.contextSentences) {
        w.str(sentence.ja);
        w.str(sentence.en);
    }
    w.u32(s.meaningCorrect);
    w.u32(s.meaningIncorrect);
    w.u32(s.readingCorrect);
    w.u32(s.readingIncorrect);
    w.u16(s.meaningMaxStreak);
    w.u16(s.meaningCurrentStreak);
    w.u16(s.readingMaxStreak);
    w.u16(s.readingCurrentStreak);
    w.u8(s.percentageCorrect);
    w.u8(s.hasReviewStats ? 1 : 0);
    if (!w.ok()) return false;
    *out = std::move(w.bytes());
    return true;
}

bool subjectDecodePayload(const uint8_t* data, size_t size, Subject* out) {
    if (!data || !out) return false;
    Reader r(data, size);
    Subject s;
    uint8_t type = 0;
    if (!r.u32(&s.id) || !r.u8(&type) || !r.u8(&s.level)) return false;
    if (type > static_cast<uint8_t>(SubjectType::KanaVocabulary)) return false;
    s.type = static_cast<SubjectType>(type);
    if (!r.str(&s.characters)) return false;

    uint16_t meanings = 0;
    if (!r.u16(&meanings) || meanings > kMaxMeanings) return false;
    s.meanings.reserve(meanings);
    for (uint16_t i = 0; i < meanings; ++i) {
        Meaning m;
        if (!decodeMeaning(r, &m)) return false;
        s.meanings.push_back(std::move(m));
    }

    uint16_t readings = 0;
    if (!r.u16(&readings) || readings > kMaxReadings) return false;
    s.readings.reserve(readings);
    for (uint16_t i = 0; i < readings; ++i) {
        Reading reading;
        if (!decodeReading(r, &reading)) return false;
        s.readings.push_back(std::move(reading));
    }

    if (!decodeStrings(r, &s.meaningSynonyms) || !decodeStrings(r, &s.whitelist) ||
        !decodeStrings(r, &s.blacklist))
        return false;

    uint16_t components = 0;
    if (!r.u16(&components) || components > kMaxList) return false;
    s.componentSubjectIds.reserve(components);
    for (uint16_t i = 0; i < components; ++i) {
        uint32_t id = 0;
        if (!r.u32(&id)) return false;
        s.componentSubjectIds.push_back(id);
    }

    uint16_t similar = 0;
    if (!r.u16(&similar) || similar > kMaxList) return false;
    s.visuallySimilarSubjectIds.reserve(similar);
    for (uint16_t i = 0; i < similar; ++i) {
        uint32_t id = 0;
        if (!r.u32(&id)) return false;
        s.visuallySimilarSubjectIds.push_back(id);
    }
    uint16_t amalgamations = 0;
    if (!r.u16(&amalgamations) || amalgamations > kMaxList) return false;
    s.amalgamationSubjectIds.reserve(amalgamations);
    for (uint16_t i = 0; i < amalgamations; ++i) {
        uint32_t id = 0;
        if (!r.u32(&id)) return false;
        s.amalgamationSubjectIds.push_back(id);
    }

    if (!r.str(&s.meaningMnemonic) || !r.str(&s.readingMnemonic) ||
        !r.str(&s.meaningHint) || !r.str(&s.readingHint) || !r.str(&s.meaningNote) ||
        !r.str(&s.readingNote))
        return false;
    if (!decodeStrings(r, &s.partsOfSpeech)) return false;
    uint16_t contextCount = 0;
    if (!r.u16(&contextCount) || contextCount > kMaxContextSentences) return false;
    s.contextSentences.reserve(contextCount);
    for (uint16_t i = 0; i < contextCount; ++i) {
        ContextSentence sentence;
        if (!r.str(&sentence.ja) || !r.str(&sentence.en)) return false;
        s.contextSentences.push_back(std::move(sentence));
    }
    uint8_t hasStats = 0;
    if (!r.u32(&s.meaningCorrect) || !r.u32(&s.meaningIncorrect) ||
        !r.u32(&s.readingCorrect) || !r.u32(&s.readingIncorrect) ||
        !r.u16(&s.meaningMaxStreak) || !r.u16(&s.meaningCurrentStreak) ||
        !r.u16(&s.readingMaxStreak) || !r.u16(&s.readingCurrentStreak) ||
        !r.u8(&s.percentageCorrect) || !r.u8(&hasStats))
        return false;
    s.hasReviewStats = hasStats != 0;
    if (!r.done()) return false;
    *out = std::move(s);
    return true;
}

}  // namespace kk

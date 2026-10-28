#include "wk/Cache.h"

#include <cstring>
#include <utility>

#include "wk/Crc32.h"

namespace kk {

namespace {

constexpr const char kSnapshotMagic[4] = {'K', 'K', 'C', 'A'};
constexpr const char kSubjectsMagic[4] = {'K', 'K', 'C', 'S'};

constexpr uint16_t kMaxBuckets = 64;
constexpr uint16_t kMaxBucketIds = 1000;
constexpr uint16_t kMaxAssignments = 128;
constexpr uint16_t kMaxSubjects = 128;
constexpr uint16_t kMaxMeanings = 32;
constexpr uint16_t kMaxReadings = 32;
constexpr uint16_t kMaxList = 64;
constexpr uint16_t kMaxString = 16384;

void put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}
void put32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
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
    void u8(uint8_t v) { bytes_.push_back(v); }
    void u16(uint16_t v) {
        if (!ok_) return;
        bytes_.push_back(static_cast<uint8_t>(v));
        bytes_.push_back(static_cast<uint8_t>(v >> 8));
    }
    void u32(uint32_t v) {
        if (!ok_) return;
        const size_t at = bytes_.size();
        bytes_.resize(at + 4);
        put32(bytes_.data() + at, v);
    }
    void str(std::string_view s) {
        if (!ok_) return;
        if (s.size() > kMaxString) {
            ok_ = false;
            return;
        }
        u16(static_cast<uint16_t>(s.size()));
        bytes_.insert(bytes_.end(), s.begin(), s.end());
    }
    bool ok() const { return ok_; }
    void fail() { ok_ = false; }
    std::vector<uint8_t>& bytes() { return bytes_; }

private:
    std::vector<uint8_t> bytes_;
    bool ok_ = true;
};

class Reader {
public:
    Reader() = default;
    Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool u8(uint8_t* v) {
        if (!ok_ || pos_ + 1 > size_) return fail();
        *v = data_[pos_++];
        return true;
    }
    bool u16(uint16_t* v) {
        if (!ok_ || pos_ + 2 > size_) return fail();
        *v = get16(data_ + pos_);
        pos_ += 2;
        return true;
    }
    bool u32(uint32_t* v) {
        if (!ok_ || pos_ + 4 > size_) return fail();
        *v = get32(data_ + pos_);
        pos_ += 4;
        return true;
    }
    bool str(std::string* out) {
        uint16_t n = 0;
        if (!u16(&n) || n > kMaxString || pos_ + n > size_) return fail();
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
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    size_t pos_ = 0;
    bool ok_ = true;
};

bool encodeFile(const char magic[4], const CacheMeta& meta, const std::vector<uint8_t>& payload,
                std::vector<uint8_t>* out) {
    std::vector<uint8_t> bytes(kCacheHeaderBytes + payload.size(), 0);
    std::memcpy(bytes.data(), magic, 4);
    put16(bytes.data() + 4, kCacheVersion);
    put16(bytes.data() + 6, 0);
    put32(bytes.data() + 8, meta.savedAt);
    put32(bytes.data() + 12, meta.accountKey);
    put32(bytes.data() + 16, static_cast<uint32_t>(payload.size()));
    put32(bytes.data() + 20, crc32(payload.data(), payload.size()));
    if (!payload.empty())
        std::memcpy(bytes.data() + kCacheHeaderBytes, payload.data(), payload.size());
    *out = std::move(bytes);
    return true;
}

bool decodeFile(const char magic[4], const uint8_t* data, size_t size, CacheMeta* meta,
                Reader* payload) {
    if (!data || size < kCacheHeaderBytes) return false;
    if (std::memcmp(data, magic, 4) != 0) return false;
    if (get16(data + 4) != kCacheVersion) return false;
    if (get16(data + 6) != 0) return false;
    const uint32_t payloadSize = get32(data + 16);
    if (payloadSize != size - kCacheHeaderBytes) return false;
    if (get32(data + 20) != crc32(data + kCacheHeaderBytes, payloadSize)) return false;
    meta->savedAt = get32(data + 8);
    meta->accountKey = get32(data + 12);
    *payload = Reader(data + kCacheHeaderBytes, payloadSize);
    return true;
}

void encodeBuckets(Writer& w, const std::vector<SummaryBucket>& buckets) {
    if (buckets.size() > kMaxBuckets) {
        w.fail();
        return;
    }
    w.u16(static_cast<uint16_t>(buckets.size()));
    for (const SummaryBucket& b : buckets) {
        w.u32(b.availableAt);
        w.u16(b.count);
        if (b.subjectIds.size() > kMaxBucketIds) {
            w.fail();
            return;
        }
        w.u16(static_cast<uint16_t>(b.subjectIds.size()));
        for (uint32_t id : b.subjectIds) w.u32(id);
    }
}

bool decodeBuckets(Reader& r, std::vector<SummaryBucket>* out) {
    uint16_t count = 0;
    if (!r.u16(&count) || count > kMaxBuckets) return false;
    out->clear();
    out->reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        SummaryBucket b;
        uint16_t ids = 0;
        if (!r.u32(&b.availableAt) || !r.u16(&b.count) || !r.u16(&ids) ||
            ids > kMaxBucketIds)
            return false;
        b.subjectIds.reserve(ids);
        for (uint16_t j = 0; j < ids; ++j) {
            uint32_t id = 0;
            if (!r.u32(&id)) return false;
            b.subjectIds.push_back(id);
        }
        out->push_back(std::move(b));
    }
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

void encodeUser(Writer& w, const User& u) {
    w.str(u.username);
    w.u16(static_cast<uint16_t>(u.level < 0 ? 0 : u.level));
    w.u8(u.subscriptionActive ? 1 : 0);
    w.u16(static_cast<uint16_t>(u.maxLevelGranted < 0 ? 0 : u.maxLevelGranted));
    w.u16(static_cast<uint16_t>(u.lessonsBatchSize < 0 ? 0 : u.lessonsBatchSize));
    w.u8(static_cast<uint8_t>(u.reviewsOrder));
    w.u32(u.vacationStartedAt);
}

bool decodeUser(Reader& r, User* u) {
    uint16_t level = 0;
    uint8_t subscription = 0;
    uint16_t maxLevel = 0;
    uint16_t batch = 0;
    uint8_t order = 0;
    uint32_t vacation = 0;
    if (!r.str(&u->username) || !r.u16(&level) || !r.u8(&subscription) ||
        !r.u16(&maxLevel) || !r.u16(&batch) || !r.u8(&order) || !r.u32(&vacation))
        return false;
    if (order > static_cast<uint8_t>(ReviewOrder::LowerLevelsFirst)) return false;
    u->level = level;
    u->subscriptionActive = subscription != 0;
    u->maxLevelGranted = maxLevel;
    u->lessonsBatchSize = batch;
    u->reviewsOrder = static_cast<ReviewOrder>(order);
    u->vacationStartedAt = vacation;
    return true;
}

void encodeLevelProgress(Writer& w, const LevelProgress& p) {
    w.u8(p.level);
    w.u32(p.unlockedAt);
    w.u32(p.startedAt);
    w.u32(p.passedAt);
    w.u32(p.completedAt);
    w.u8(p.valid ? 1 : 0);
}

bool decodeLevelProgress(Reader& r, LevelProgress* p) {
    uint8_t valid = 0;
    if (!r.u8(&p->level) || !r.u32(&p->unlockedAt) || !r.u32(&p->startedAt) ||
        !r.u32(&p->passedAt) || !r.u32(&p->completedAt) || !r.u8(&valid))
        return false;
    p->valid = valid != 0;
    return true;
}

void encodeAssignment(Writer& w, const Assignment& a) {
    w.u32(a.id);
    w.u32(a.subjectId);
    w.u8(static_cast<uint8_t>(a.type));
    w.u8(a.srsStage);
    w.u32(a.availableAt);
    w.u32(a.dataUpdatedAt);
    w.u8(static_cast<uint8_t>((a.hidden ? 1 : 0) | (a.pendingReview ? 2 : 0)));
}

bool decodeAssignment(Reader& r, Assignment* a) {
    uint8_t type = 0;
    uint8_t flags = 0;
    if (!r.u32(&a->id) || !r.u32(&a->subjectId) || !r.u8(&type) || !r.u8(&a->srsStage) ||
        !r.u32(&a->availableAt) || !r.u32(&a->dataUpdatedAt) || !r.u8(&flags))
        return false;
    if (type > static_cast<uint8_t>(SubjectType::KanaVocabulary)) return false;
    a->type = static_cast<SubjectType>(type);
    a->hidden = (flags & 1) != 0;
    a->pendingReview = (flags & 2) != 0;
    return true;
}

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

void encodeReading(Writer& w, const Reading& r) {
    w.str(r.text);
    w.u8(static_cast<uint8_t>((r.primary ? 1 : 0) | (r.accepted ? 2 : 0)));
    w.u8(static_cast<uint8_t>(r.type));
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

void encodeSubject(Writer& w, const Subject& s) {
    w.u32(s.id);
    w.u8(static_cast<uint8_t>(s.type));
    w.u8(s.level);
    w.str(s.characters);

    const uint16_t meanings = static_cast<uint16_t>(s.meanings.size());
    if (s.meanings.size() > kMaxMeanings) {
        w.fail();
        return;
    }
    w.u16(meanings);
    for (uint16_t i = 0; i < meanings; ++i) encodeMeaning(w, s.meanings[i]);

    if (s.readings.size() > kMaxReadings) {
        w.fail();
        return;
    }
    const uint16_t readings = static_cast<uint16_t>(s.readings.size());
    w.u16(readings);
    for (uint16_t i = 0; i < readings; ++i) encodeReading(w, s.readings[i]);

    encodeStrings(w, s.meaningSynonyms);
    encodeStrings(w, s.whitelist);
    encodeStrings(w, s.blacklist);

    if (s.componentSubjectIds.size() > kMaxList) {
        w.fail();
        return;
    }
    const uint16_t components = static_cast<uint16_t>(s.componentSubjectIds.size());
    w.u16(components);
    for (uint16_t i = 0; i < components; ++i) w.u32(s.componentSubjectIds[i]);

    w.str(s.meaningMnemonic);
    w.str(s.readingMnemonic);
    w.str(s.meaningHint);
    w.str(s.readingHint);
    w.str(s.meaningNote);
    w.str(s.readingNote);
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
}

bool decodeSubject(Reader& r, Subject* s) {
    uint8_t type = 0;
    if (!r.u32(&s->id) || !r.u8(&type) || !r.u8(&s->level)) return false;
    if (type > static_cast<uint8_t>(SubjectType::KanaVocabulary)) return false;
    s->type = static_cast<SubjectType>(type);
    if (!r.str(&s->characters)) return false;

    uint16_t meanings = 0;
    if (!r.u16(&meanings) || meanings > kMaxMeanings) return false;
    s->meanings.reserve(meanings);
    for (uint16_t i = 0; i < meanings; ++i) {
        Meaning m;
        if (!decodeMeaning(r, &m)) return false;
        s->meanings.push_back(std::move(m));
    }

    uint16_t readings = 0;
    if (!r.u16(&readings) || readings > kMaxReadings) return false;
    s->readings.reserve(readings);
    for (uint16_t i = 0; i < readings; ++i) {
        Reading reading;
        if (!decodeReading(r, &reading)) return false;
        s->readings.push_back(std::move(reading));
    }

    if (!decodeStrings(r, &s->meaningSynonyms) || !decodeStrings(r, &s->whitelist) ||
        !decodeStrings(r, &s->blacklist))
        return false;

    uint16_t components = 0;
    if (!r.u16(&components) || components > kMaxList) return false;
    s->componentSubjectIds.reserve(components);
    for (uint16_t i = 0; i < components; ++i) {
        uint32_t id = 0;
        if (!r.u32(&id)) return false;
        s->componentSubjectIds.push_back(id);
    }

    if (!r.str(&s->meaningMnemonic) || !r.str(&s->readingMnemonic) || !r.str(&s->meaningHint) ||
        !r.str(&s->readingHint) || !r.str(&s->meaningNote) || !r.str(&s->readingNote))
        return false;
    uint8_t hasStats = 0;
    if (!r.u32(&s->meaningCorrect) || !r.u32(&s->meaningIncorrect) || !r.u32(&s->readingCorrect) ||
        !r.u32(&s->readingIncorrect) || !r.u16(&s->meaningMaxStreak) ||
        !r.u16(&s->meaningCurrentStreak) || !r.u16(&s->readingMaxStreak) ||
        !r.u16(&s->readingCurrentStreak) || !r.u8(&s->percentageCorrect) || !r.u8(&hasStats))
        return false;
    s->hasReviewStats = hasStats != 0;
    return true;
}

}  // namespace

uint32_t cacheAccountKey(std::string_view token) {
    uint32_t h = 2166136261u;
    for (unsigned char c : token) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

bool encodeCacheSnapshot(const CacheSnapshot& snapshot, std::vector<uint8_t>* out) {
    if (!out) return false;
    Writer w;
    w.str(snapshot.summary.nextReviewsAt);
    w.u32(snapshot.summary.dataUpdatedAt);
    encodeBuckets(w, snapshot.summary.lessons);
    encodeBuckets(w, snapshot.summary.reviews);
    encodeUser(w, snapshot.user);
    encodeLevelProgress(w, snapshot.levelProgress);
    if (snapshot.assignments.size() > kMaxAssignments) return false;
    w.u16(static_cast<uint16_t>(snapshot.assignments.size()));
    for (const Assignment& a : snapshot.assignments) encodeAssignment(w, a);
    if (!w.ok()) return false;
    return encodeFile(kSnapshotMagic, snapshot.meta, w.bytes(), out);
}

bool decodeCacheSnapshot(const uint8_t* data, size_t size, CacheSnapshot* out) {
    if (!out) return false;
    CacheMeta meta;
    Reader r;
    if (!decodeFile(kSnapshotMagic, data, size, &meta, &r)) return false;
    CacheSnapshot snapshot;
    snapshot.meta = meta;
    if (!r.str(&snapshot.summary.nextReviewsAt) ||
        !r.u32(&snapshot.summary.dataUpdatedAt) ||
        !decodeBuckets(r, &snapshot.summary.lessons) ||
        !decodeBuckets(r, &snapshot.summary.reviews) || !decodeUser(r, &snapshot.user) ||
        !decodeLevelProgress(r, &snapshot.levelProgress))
        return false;
    uint16_t count = 0;
    if (!r.u16(&count) || count > kMaxAssignments) return false;
    snapshot.assignments.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        Assignment a;
        if (!decodeAssignment(r, &a)) return false;
        snapshot.assignments.push_back(a);
    }
    if (!r.done()) return false;
    *out = std::move(snapshot);
    return true;
}

bool encodeSubjectCache(const CacheMeta& meta, const std::vector<Subject>& subjects,
                        std::vector<uint8_t>* out) {
    if (!out) return false;
    if (subjects.size() > kMaxSubjects) return false;
    Writer w;
    w.u16(static_cast<uint16_t>(subjects.size()));
    for (const Subject& s : subjects) encodeSubject(w, s);
    if (!w.ok()) return false;
    return encodeFile(kSubjectsMagic, meta, w.bytes(), out);
}

bool encodeSubjectCache(const SubjectCache& cache, std::vector<uint8_t>* out) {
    return encodeSubjectCache(cache.meta, cache.subjects, out);
}

bool decodeSubjectCache(const uint8_t* data, size_t size, SubjectCache* out) {
    if (!out) return false;
    CacheMeta meta;
    Reader r;
    if (!decodeFile(kSubjectsMagic, data, size, &meta, &r)) return false;
    SubjectCache cache;
    cache.meta = meta;
    uint16_t count = 0;
    if (!r.u16(&count) || count > kMaxSubjects) return false;
    cache.subjects.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        Subject s;
        if (!decodeSubject(r, &s)) return false;
        cache.subjects.push_back(std::move(s));
    }
    if (!r.done()) return false;
    *out = std::move(cache);
    return true;
}

}  // namespace kk

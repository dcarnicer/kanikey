// MEM-20: the review batch's component records. The record builder cuts the
// characters and the meaning into their fixed fields on UTF-8 boundaries, takes
// the same meaning the card showed, and the batch helpers pick each component
// once without keeping its full Subject.
#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "wk/SubjectRef.h"

using namespace kk;

namespace {

// UTF-8 bytes, written as escapes so the source encoding does not matter.
const char kIchi[] = "\xE4\xB8\x80";                 // 一, 3 bytes
const char kNi[] = "\xE4\xBA\x8C";                    // 二, 3 bytes
const char kSan[] = "\xE4\xB8\x89";                   // 三, 3 bytes
const char kShi[] = "\xE5\x9B\x9B";                   // 四, 3 bytes
const char kSakaki[] = "\xF0\xA0\xAE\xB7";            // 𠮷 (U+20BB7), 4 bytes

Subject makeSubject(uint32_t id, SubjectType type, const std::string& characters,
                    const std::vector<Meaning>& meanings) {
    Subject s;
    s.id = id;
    s.type = type;
    s.characters = characters;
    s.meanings = meanings;
    return s;
}

}  // namespace

// The fixed field sizes: the record stays at 48 bytes (45 of fields, padded by
// the 4-byte id). A bigger record would double the component cost.
static void subject_ref_record_is_compact() {
    TEST_ASSERT_TRUE(sizeof(SubjectRef) <= 48);
    TEST_ASSERT_EQUAL(12, static_cast<int>(kSubjectRefCharsBytes));
    TEST_ASSERT_EQUAL(28, static_cast<int>(kSubjectRefMeaningBytes));
}

static void copy_fits_without_cut() {
    char field[kSubjectRefCharsBytes];
    TEST_ASSERT_EQUAL(3, static_cast<int>(copyUtf8Truncated(std::string(kShi), field, sizeof field)));
    TEST_ASSERT_EQUAL_STRING(kShi, field);
    // Exactly 11 bytes is the longest value that leaves room for the NUL.
    const std::string eleven = std::string("abcdefghij") + "k";
    TEST_ASSERT_EQUAL(11, static_cast<int>(copyUtf8Truncated(eleven, field, sizeof field)));
    TEST_ASSERT_EQUAL_STRING(eleven.c_str(), field);
}

// ASCII is cut at the field size, minus the NUL.
static void copy_cuts_ascii_at_capacity() {
    char field[kSubjectRefCharsBytes];
    TEST_ASSERT_EQUAL(11, static_cast<int>(copyUtf8Truncated(std::string("abcdefghijklmnop"),
                                                             field, sizeof field)));
    TEST_ASSERT_EQUAL_STRING("abcdefghijk", field);
}

// The cut must not land inside a 3-byte character: 一二三四 is 12 bytes, so only
// three fit in 11 bytes.
static void copy_cuts_on_utf8_boundary_three_byte() {
    char field[kSubjectRefCharsBytes];
    const std::string four = std::string(kIchi) + kNi + kSan + kShi;
    TEST_ASSERT_EQUAL(9, static_cast<int>(copyUtf8Truncated(four, field, sizeof field)));
    TEST_ASSERT_EQUAL_STRING((std::string(kIchi) + kNi + kSan).c_str(), field);
}

// A 4-byte character after 10 bytes does not fit in 11: the cut backs off to
// the lead byte and keeps the 10 whole bytes.
static void copy_cuts_on_utf8_boundary_four_byte() {
    char field[kSubjectRefCharsBytes];
    const std::string text = std::string("abcdefghij") + kSakaki;  // 14 bytes
    TEST_ASSERT_EQUAL(10, static_cast<int>(copyUtf8Truncated(text, field, sizeof field)));
    TEST_ASSERT_EQUAL_STRING("abcdefghij", field);
}

// 'a' + 一 + 二 + 三 = 10 bytes; the cut at 11 falls inside 四, so 四 goes.
static void copy_cuts_mixed_text_on_boundary() {
    char field[kSubjectRefCharsBytes];
    const std::string text = std::string("a") + kIchi + kNi + kSan + kShi;  // 13 bytes
    TEST_ASSERT_EQUAL(10, static_cast<int>(copyUtf8Truncated(text, field, sizeof field)));
    TEST_ASSERT_EQUAL_STRING((std::string("a") + kIchi + kNi + kSan).c_str(), field);
}

static void copy_into_capacity_one_is_empty() {
    char field[1] = {'x'};
    TEST_ASSERT_EQUAL(0, static_cast<int>(copyUtf8Truncated("abc", field, sizeof field)));
    TEST_ASSERT_EQUAL(0, field[0]);
}

// A kanji component: its character and its first accepted meaning.
static void record_for_kanji_takes_character_and_meaning() {
    const Subject kanji = makeSubject(2, SubjectType::Kanji, kShi,
                                      {{"four", true, true}, {"yon", false, true}});
    const SubjectRef ref = subjectRefFromSubject(kanji);
    TEST_ASSERT_EQUAL_UINT32(2, ref.id);
    TEST_ASSERT_EQUAL(static_cast<int>(SubjectType::Kanji), static_cast<int>(ref.type));
    TEST_ASSERT_EQUAL_STRING(kShi, ref.characters);
    TEST_ASSERT_EQUAL_STRING("four", ref.meaning);
}

// A radical with no Unicode character: the characters stay empty, which the card
// draws as RAD, and the meaning is still set.
static void record_for_radical_without_characters() {
    const Subject radical = makeSubject(4, SubjectType::Radical, "",
                                        {{"Ground", true, true}});
    const SubjectRef ref = subjectRefFromSubject(radical);
    TEST_ASSERT_EQUAL(static_cast<int>(SubjectType::Radical), static_cast<int>(ref.type));
    TEST_ASSERT_EQUAL(0, ref.characters[0]);
    TEST_ASSERT_EQUAL_STRING("Ground", ref.meaning);
}

// The card's meaning is the first accepted one; if none is accepted, the first.
static void record_meaning_matches_subject_main_meaning() {
    const Subject rejectedFirst = makeSubject(
        7, SubjectType::Kanji, kIchi, {{"old", false, false}, {"one", false, true}});
    TEST_ASSERT_EQUAL_STRING("one", subjectRefFromSubject(rejectedFirst).meaning);

    const Subject noneAccepted = makeSubject(
        8, SubjectType::Kanji, kNi, {{"first", false, false}, {"second", false, false}});
    TEST_ASSERT_EQUAL_STRING("first", subjectRefFromSubject(noneAccepted).meaning);

    const Subject noMeaning = makeSubject(9, SubjectType::Kanji, kSan, {});
    TEST_ASSERT_EQUAL(0, subjectRefFromSubject(noMeaning).meaning[0]);
}

// A long meaning is cut to the 27 visible bytes, on a character boundary.
static void record_meaning_is_cut_to_field() {
    const std::string longMeaning(40, 'm');
    const Subject s = makeSubject(10, SubjectType::Radical, "", {{longMeaning, true, true}});
    const SubjectRef ref = subjectRefFromSubject(s);
    TEST_ASSERT_EQUAL(27, static_cast<int>(std::strlen(ref.meaning)));
}

// Components referenced by the reviewed subjects, once each, and never an id
// that is already a reviewed subject.
static void collect_component_ids_dedupes_and_skips_reviewed() {
    Subject vocab = makeSubject(100, SubjectType::Vocabulary, "x", {{"v", true, true}});
    vocab.componentSubjectIds = {2, 3, 5};
    Subject kanji = makeSubject(5, SubjectType::Kanji, "y", {{"k", true, true}});
    kanji.componentSubjectIds = {1, 3, 4};
    const std::vector<Subject> reviewed = {vocab, kanji};

    std::vector<uint32_t> ids;
    collectComponentIds(reviewed, &ids);
    // 2, 3 and 1, 4 once each; 5 is reviewed, so it is not fetched again.
    const std::vector<uint32_t> want = {2, 3, 1, 4};
    TEST_ASSERT_EQUAL(want.size(), ids.size());
    for (size_t i = 0; i < want.size(); ++i) TEST_ASSERT_EQUAL_UINT32(want[i], ids[i]);
}

// Pages add one record per component; a repeat across pages or an id that is a
// reviewed subject adds nothing. The reviewed vector is only read.
static void component_refs_leave_reviewed_subjects_alone() {
    const Subject reviewed = makeSubject(9, SubjectType::Kanji, kIchi, {{"one", true, true}});
    const std::vector<Subject> reviewedList = {reviewed};

    std::vector<SubjectRef> refs;
    refs.reserve(3);
    const std::vector<Subject> first = {
        makeSubject(1, SubjectType::Radical, "", {{"Ground", true, true}}),
        makeSubject(2, SubjectType::Kanji, kNi, {{"two", true, true}}),
    };
    appendComponentRefs(first, reviewedList, &refs);
    const std::vector<Subject> second = {
        makeSubject(1, SubjectType::Radical, "", {{"Ground", true, true}}),  // repeat
        makeSubject(9, SubjectType::Kanji, kIchi, {{"one", true, true}}),    // reviewed
        makeSubject(3, SubjectType::Kanji, kSan, {{"three", true, true}}),
    };
    appendComponentRefs(second, reviewedList, &refs);

    TEST_ASSERT_EQUAL(3, refs.size());
    TEST_ASSERT_EQUAL_UINT32(1, refs[0].id);
    TEST_ASSERT_EQUAL_UINT32(2, refs[1].id);
    TEST_ASSERT_EQUAL_UINT32(3, refs[2].id);
    TEST_ASSERT_EQUAL_STRING("three", refs[2].meaning);
    // The reviewed subject is still the only Subject kept; the call stores none.
    TEST_ASSERT_EQUAL(1, reviewedList.size());
}

static void find_subject_ref_by_id() {
    std::vector<SubjectRef> refs(2);
    refs[0].id = 5;
    refs[1].id = 6;
    TEST_ASSERT_TRUE(findSubjectRef(refs, 6) == &refs[1]);
    TEST_ASSERT_TRUE(findSubjectRef(refs, 7) == nullptr);
    TEST_ASSERT_TRUE(findSubjectRef({}, 5) == nullptr);
}

void run_subject_ref_tests() {
    RUN_TEST(subject_ref_record_is_compact);
    RUN_TEST(copy_fits_without_cut);
    RUN_TEST(copy_cuts_ascii_at_capacity);
    RUN_TEST(copy_cuts_on_utf8_boundary_three_byte);
    RUN_TEST(copy_cuts_on_utf8_boundary_four_byte);
    RUN_TEST(copy_cuts_mixed_text_on_boundary);
    RUN_TEST(copy_into_capacity_one_is_empty);
    RUN_TEST(record_for_kanji_takes_character_and_meaning);
    RUN_TEST(record_for_radical_without_characters);
    RUN_TEST(record_meaning_matches_subject_main_meaning);
    RUN_TEST(record_meaning_is_cut_to_field);
    RUN_TEST(collect_component_ids_dedupes_and_skips_reviewed);
    RUN_TEST(component_refs_leave_reviewed_subjects_alone);
    RUN_TEST(find_subject_ref_by_id);
}

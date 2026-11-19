#pragma once
// The lesson card split into topic pages. Pure text building: no display
// width logic and no Arduino headers; the controller wraps each body.
#include <string>
#include <vector>

#include "wk/Types.h"

namespace kk {

struct LessonPageText {
    const char* title = "";
    std::string body;
};

// Builds the topic pages for one lesson:
//   Meaning              accepted meanings, meaning mnemonic, meaning hint
//   Reading              readings, reading mnemonic, reading hint (skipped when
//                        the subject needs no reading, e.g. radicals)
//   Components and notes component meanings, meaning/reading notes (skipped
//                        when both are empty)
//   Context              parts of speech and context sentences (skipped when
//                        both are empty)
// The pages are always in this order and the Meaning page is always present.
std::vector<LessonPageText> buildLessonPages(
    const Subject& subject, const std::vector<const Subject*>& components);

}  // namespace kk

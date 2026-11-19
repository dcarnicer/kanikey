#pragma once
// Display tokens for the accepted answers on review and lesson feedback.
// Pure domain helpers: no Arduino headers.
#include <string>
#include <vector>

#include "wk/Types.h"

namespace kk {

// Every accepted meaning in WaniKani order. Synonyms stay out of the list so
// the view can draw them separately in the faint colour.
std::vector<std::string> acceptedMeaningTokens(const Subject& s);

// Every accepted reading in display order. For a kanji whose readings carry a
// parsed ReadingType the on'yomi group comes first and the kun'yomi group
// second (nanori after them, unknown readings last). An empty token starts a
// new group; the view draws a wider gap there. Items without parsed types stay
// flat in WaniKani order.
std::vector<std::string> acceptedReadingTokens(const Subject& s);

}  // namespace kk

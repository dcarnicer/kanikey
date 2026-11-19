#pragma once
// Small subject-text helpers shared by the review, lesson, study and outbox
// controllers. Pure and inline; no Arduino headers.
#include <string>

#include "wk/ReviewQueue.h"
#include "wk/Types.h"

namespace kk {

inline std::string subjectMainMeaning(const Subject& s) {
    for (const Meaning& m : s.meanings)
        if (m.accepted) return m.text;
    if (!s.meanings.empty()) return s.meanings[0].text;
    return std::string();
}

inline std::string correctAnswer(const Subject& s, ReviewPart part) {
    if (part == ReviewPart::Reading) {
        for (const Reading& r : s.readings)
            if (r.accepted) return r.text;
        if (!s.readings.empty()) return s.readings[0].text;
        return "-";
    }
    for (const Meaning& m : s.meanings)
        if (m.accepted) return m.text;
    if (!s.meanings.empty()) return s.meanings[0].text;
    return "-";
}

}  // namespace kk

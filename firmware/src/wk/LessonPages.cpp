#include "wk/LessonPages.h"

#include <string>
#include <utility>
#include <vector>

namespace kk {

namespace {

void appendSentence(std::string* body, const std::string& text) {
    if (text.empty()) return;
    if (!body->empty()) *body += ' ';
    *body += text;
    if (body->back() != '.' && body->back() != '!' && body->back() != '?') *body += '.';
}

std::string join(const std::vector<std::string>& values, const char* separator) {
    std::string out;
    for (const std::string& value : values) {
        if (value.empty()) continue;
        if (!out.empty()) out += separator;
        out += value;
    }
    return out;
}

std::vector<std::string> meaningTexts(const Subject& subject) {
    std::vector<std::string> out;
    for (const Meaning& meaning : subject.meanings)
        if (meaning.accepted) out.push_back(meaning.text);
    if (out.empty())
        for (const Meaning& meaning : subject.meanings) out.push_back(meaning.text);
    return out;
}

std::vector<std::string> readingTexts(const Subject& subject) {
    std::vector<std::string> out;
    for (const Reading& reading : subject.readings)
        if (reading.accepted) out.push_back(reading.text);
    if (out.empty())
        for (const Reading& reading : subject.readings) out.push_back(reading.text);
    return out;
}

std::string mainMeaning(const Subject& subject) {
    for (const Meaning& meaning : subject.meanings)
        if (meaning.accepted) return meaning.text;
    if (!subject.meanings.empty()) return subject.meanings[0].text;
    return std::string();
}

}  // namespace

std::vector<LessonPageText> buildLessonPages(
    const Subject& subject, const std::vector<const Subject*>& components) {
    std::vector<LessonPageText> pages;

    LessonPageText meaning;
    meaning.title = "Meaning";
    const std::string meanings = join(meaningTexts(subject), ", ");
    if (!meanings.empty()) appendSentence(&meaning.body, "Meanings: " + meanings);
    appendSentence(&meaning.body, subject.meaningMnemonic);
    if (!subject.meaningHint.empty())
        appendSentence(&meaning.body, "Hint: " + subject.meaningHint);
    if (meaning.body.empty()) meaning.body = "No mnemonic cached.";
    pages.push_back(std::move(meaning));

    if (subject.needsReading()) {
        LessonPageText reading;
        reading.title = "Reading";
        const std::string readings = join(readingTexts(subject), ", ");
        if (!readings.empty()) appendSentence(&reading.body, "Readings: " + readings);
        appendSentence(&reading.body, subject.readingMnemonic);
        if (!subject.readingHint.empty())
            appendSentence(&reading.body, "Reading hint: " + subject.readingHint);
        if (reading.body.empty()) reading.body = "No mnemonic cached.";
        pages.push_back(std::move(reading));
    }

    LessonPageText notes;
    notes.title = "Components and notes";
    std::vector<std::string> componentMeanings;
    for (const Subject* component : components) {
        if (!component) continue;
        const std::string text = mainMeaning(*component);
        if (!text.empty()) componentMeanings.push_back(text);
    }
    if (!componentMeanings.empty())
        appendSentence(&notes.body, "Components: " + join(componentMeanings, " + "));
    if (!subject.meaningNote.empty())
        appendSentence(&notes.body, "Meaning note: " + subject.meaningNote);
    if (!subject.readingNote.empty())
        appendSentence(&notes.body, "Reading note: " + subject.readingNote);
    if (!notes.body.empty()) pages.push_back(std::move(notes));

    LessonPageText context;
    context.title = "Context";
    const std::string parts = join(subject.partsOfSpeech, ", ");
    if (!parts.empty()) appendSentence(&context.body, "Parts of speech: " + parts);
    for (const ContextSentence& sentence : subject.contextSentences) {
        // The card body uses the ASCII 6x8 font; like the item detail page,
        // show the translation and fall back to the Japanese line.
        const std::string& text = sentence.en.empty() ? sentence.ja : sentence.en;
        appendSentence(&context.body, text);
    }
    if (!context.body.empty()) pages.push_back(std::move(context));

    return pages;
}

}  // namespace kk

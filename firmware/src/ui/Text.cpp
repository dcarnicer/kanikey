// Shared wrapped-text drawing. The app and the screens call this with the
// caller-owned transfer window already open.
#include "ui/Text.h"

#include <vector>

#include "ui/TextLayout.h"
#include "ui/UiFont.h"

namespace kk::ui {
namespace {

int measureFont0(void* ctx, std::string_view text) {
    auto* d = static_cast<lgfx::LovyanGFX*>(ctx);
    return d->textWidth(std::string(text).c_str());
}

}  // namespace

void drawWrapped(lgfx::LovyanGFX& d, const std::string& text, int x, int y, int w, int maxLines,
                 uint16_t fg, uint16_t bg, bool ellipsis) {
    ui::applyFont(d, ui::UiFont::Small);
    d.setTextColor(fg, bg);
    if (maxLines < 1) return;

    const TextMeasure measure{&measureFont0, &d};
    std::vector<TextLine> lines(static_cast<size_t>(maxLines));
    const WrapResult wrapped = wrapLines(text, w, measure, lines.data(), maxLines);
    for (int line = 0; line < wrapped.lineCount; ++line) {
        const TextLine& span = lines[static_cast<size_t>(line)];
        d.drawString(text.substr(span.begin, span.length()).c_str(), x, y + line * 8);
    }
    if (ellipsis && wrapped.truncated && wrapped.lineCount > 0) {
        // Replace the last drawn line with a shortened one ending in "...".
        const int lastY = y + (wrapped.lineCount - 1) * 8;
        d.fillRect(x, lastY, w, 8, bg);
        const TextLine& span = lines[static_cast<size_t>(wrapped.lineCount - 1)];
        const std::string shown = ellipsize(text.substr(span.begin, span.length()), w,
                                            measure, "...", true);
        d.setTextColor(fg, bg);
        d.drawString(shown.c_str(), x, lastY);
    }
}

}  // namespace kk::ui

// Shared wrapped-text drawing. The app and the screens call this with the
// caller-owned transfer window already open.
#include "ui/Text.h"

namespace kk::ui {

void drawWrapped(lgfx::LovyanGFX& d, const std::string& text, int x, int y, int w, int maxLines,
                 uint16_t fg, uint16_t bg) {
    d.setTextFont(0);
    d.setTextSize(1);
    d.setTextColor(fg, bg);
    size_t start = 0;
    for (int line = 0; line < maxLines && start < text.size(); ++line) {
        size_t end = text.size();
        while (end > start && d.textWidth(text.substr(start, end - start).c_str()) > w) --end;
        if (end < text.size()) {
            const size_t sp = text.rfind(' ', end);
            if (sp != std::string::npos && sp > start) end = sp;
        }
        d.drawString(text.substr(start, end - start).c_str(), x, y + line * 8);
        start = end;
        while (start < text.size() && text[start] == ' ') ++start;
    }
}

}  // namespace kk::ui

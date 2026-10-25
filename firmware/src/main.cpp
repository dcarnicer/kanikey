// KaniKey — first hardware milestone.
//
// What this build proves on a Cardputer ADV:
//   - the board layer starts (display, TCA8418 keyboard, battery);
//   - the keyboard matrix delivers down/up events and grid positions;
//   - the microSD mounts on SPI3 and reports its size;
//   - the internal heap is measured before any WaniKani/TLS work.
//
// It does not talk to WaniKani yet. The next milestone adds the Wi-Fi setup and
// a TLS GET /user with heap before/after, as described in docs/design.md.

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include <cstring>

#include "board/Board.h"
#include "platform/DevToken.h"
#include "platform/DevWifi.h"
#include "platform/WkProbe.h"

namespace {

using tgc::Key;
using tgc::KeyEvent;

constexpr uint16_t kBg = 0x0000;
constexpr uint16_t kSurface = 0x18E4;
constexpr uint16_t kLine = 0x3A0A;
constexpr uint16_t kText = 0xFFFF;
constexpr uint16_t kDim = 0xA577;
constexpr uint16_t kFaint = 0x636F;
constexpr uint16_t kBrand = 0xFB0B;  // #FF7A59, RGB565
constexpr uint16_t kOk = 0x5EF1;
constexpr uint16_t kWarn = 0xFE26;

lgfx::LGFX_Device* gDisp = nullptr;
bool gDirty = true;
uint32_t gLastStatusMs = 0;
uint32_t gLastDrawMs = 0;

bool gSdOk = false;
char gSdLine[48] = "SD: not mounted";
char gNetLine[48] = "NET: not tested";
String gSerialLine;

bool gShift = false;
bool gFn = false;
bool gCtrl = false;
bool gOpt = false;
bool gAlt = false;

struct Line {
    char text[44];
};
Line gLog[3];

const char* keyName(Key k) {
    switch (k) {
        case Key::Up: return "Up";
        case Key::Down: return "Down";
        case Key::Left: return "Left";
        case Key::Right: return "Right";
        case Key::Enter: return "Enter";
        case Key::Esc: return "Esc";
        case Key::Backspace: return "Backspace";
        case Key::Tab: return "Tab";
        case Key::Other: return "Other";
        case Key::None: return "None";
    }
    return "?";
}

void pushLog(const char* text) {
    for (int i = 0; i < 2; ++i) gLog[i] = gLog[i + 1];
    snprintf(gLog[2].text, sizeof gLog[2].text, "%s", text);
}

void updateModifier(uint8_t x, uint8_t y, bool down) {
    switch (tgc::cardputerModifierAt(x, y)) {
        case tgc::Modifier::Shift: gShift = down; break;
        case tgc::Modifier::Fn: gFn = down; break;
        case tgc::Modifier::Ctrl: gCtrl = down; break;
        case tgc::Modifier::Opt: gOpt = down; break;
        case tgc::Modifier::Alt: gAlt = down; break;
        case tgc::Modifier::None: break;
    }
}

void runProbe() {
    const String token = kk::DevToken::load();
    snprintf(gNetLine, sizeof gNetLine, "API: probing...");
    gDirty = true;
    const kk::WkProbeResult r = kk::probeWaniKaniUser(token);
    if (r.connected) {
        if (r.httpCode == 200 && !r.username.isEmpty())
            snprintf(gNetLine, sizeof gNetLine, "API: 200 %s L%d", r.username.c_str(), r.level);
        else
            snprintf(gNetLine, sizeof gNetLine, "API: %d  TLS %ums", r.httpCode,
                     static_cast<unsigned>(r.ms));
    } else {
        snprintf(gNetLine, sizeof gNetLine, "API: TLS failed (%d)", r.tlsError);
    }
    gDirty = true;
}

void onKey(const KeyEvent& e) {
    updateModifier(e.x, e.y, e.down);
    const char c = tgc::cardputerCharAt(e.x, e.y, gShift);

    char line[44];
    if (c) {
        snprintf(line, sizeof line, "%s '%c'  code %u  %u,%u", e.down ? "v" : "^", c,
                 static_cast<unsigned>(e.code), static_cast<unsigned>(e.x),
                 static_cast<unsigned>(e.y));
    } else {
        snprintf(line, sizeof line, "%s %-9s code %u  %u,%u", e.down ? "v" : "^",
                 keyName(e.key), static_cast<unsigned>(e.code),
                 static_cast<unsigned>(e.x), static_cast<unsigned>(e.y));
    }
    pushLog(line);
    gDirty = true;

    char cs[2] = {c, 0};
    Serial.printf("[key] %s code=%u grid=%u,%u key=%s char=%s shift=%d fn=%d ctrl=%d opt=%d "
                  "alt=%d\n",
                  e.down ? "down" : "up", static_cast<unsigned>(e.code),
                  static_cast<unsigned>(e.x), static_cast<unsigned>(e.y), keyName(e.key),
                  c ? cs : "-", gShift, gFn, gCtrl, gOpt, gAlt);
}

bool mountSd() {
    tgc::board::SdPins pins;
    if (!tgc::board::sdPins(&pins)) {
        snprintf(gSdLine, sizeof gSdLine, "SD: no slot");
        return false;
    }
    if (pins.otherCs >= 0) {
        pinMode(pins.otherCs, OUTPUT);
        digitalWrite(pins.otherCs, HIGH);
    }
    static SPIClass spi(HSPI);
    spi.begin(pins.sck, pins.miso, pins.mosi, pins.cs);
    const size_t heapBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const uint32_t t0 = millis();
    if (!SD.begin(pins.cs, spi, 20000000, "/sd", 2)) {
        snprintf(gSdLine, sizeof gSdLine, "SD: no card / not FAT32");
        Serial.printf("[sd] mount failed in %u ms, heap %u -> %u\n",
                      static_cast<unsigned>(millis() - t0),
                      static_cast<unsigned>(heapBefore),
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
        return false;
    }
    const uint64_t mb = SD.cardSize() / (1024ull * 1024ull);
    snprintf(gSdLine, sizeof gSdLine, "SD: %llu MB", static_cast<unsigned long long>(mb));
    Serial.printf("[sd] mounted in %u ms, %llu MB, heap %u -> %u, largest %u\n",
                  static_cast<unsigned>(millis() - t0), static_cast<unsigned long long>(mb),
                  static_cast<unsigned>(heapBefore),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                         MALLOC_CAP_8BIT)));
    return true;
}

void draw() {
    if (!gDisp) return;
    auto& d = *gDisp;
    d.startWrite();
    d.fillScreen(kBg);

    d.setTextDatum(top_left);
    d.setTextWrap(false);
    d.setTextFont(2);
    d.setTextSize(2);
    d.setTextColor(kBrand, kBg);
    d.drawString("KaniKey", 6, 2);

    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(kDim, kBg);
    d.drawString("unofficial WaniKani companion", 7, 38);

    const int bat = tgc::board::batteryPercent();
    char right[24];
    if (bat >= 0)
        snprintf(right, sizeof right, "%d%%", bat);
    else
        snprintf(right, sizeof right, "USB");
    d.setTextColor(bat >= 0 && bat < 20 ? kWarn : kDim, kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString(right, 233, 7);
    d.setTextDatum(lgfx::top_left);

    d.setTextColor(kText, kBg);
    d.setTextFont(1);
    d.drawString("M0 hardware check", 7, 54);
    d.setTextColor(kFaint, kBg);
    d.drawString(gNetLine, 7, 66);

    d.fillRect(4, 78, 232, 34, kSurface);
    d.drawRect(4, 78, 232, 34, kLine);
    d.setTextFont(1);
    for (int i = 0; i < 3; ++i) {
        d.setTextColor(i == 2 ? kText : kFaint, kSurface);
        d.drawString(gLog[i].text, 9, 81 + i * 10);
    }

    d.setTextFont(0);
    d.setTextColor(gSdOk ? kOk : kWarn, kBg);
    d.drawString(gSdLine, 5, 117);
    char heap[40];
    snprintf(heap, sizeof heap, "heap %u  largest %u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                     MALLOC_CAP_8BIT)));
    d.setTextColor(kFaint, kBg);
    d.drawString(heap, 5, 126);

    char mods[24] = "";
    if (gShift) strncat(mods, "S", sizeof mods - strlen(mods) - 1);
    if (gFn) strncat(mods, "F", sizeof mods - strlen(mods) - 1);
    if (gCtrl) strncat(mods, "C", sizeof mods - strlen(mods) - 1);
    if (gOpt) strncat(mods, "O", sizeof mods - strlen(mods) - 1);
    if (gAlt) strncat(mods, "A", sizeof mods - strlen(mods) - 1);
    if (mods[0]) {
        d.setTextColor(kDim, kBg);
        d.setTextDatum(lgfx::top_right);
        d.drawString(mods, 233, 126);
        d.setTextDatum(lgfx::top_left);
    }

    d.endWrite();
}

void drawStatus() {
    if (!gDisp) return;
    // The status line changes on its own (battery/heap), so redraw just it.
    auto& d = *gDisp;
    d.startWrite();
    d.setTextDatum(top_left);
    d.setTextFont(0);
    d.setTextColor(gSdOk ? kOk : kWarn, kBg);
    d.fillRect(5, 117, 230, 8, kBg);
    d.drawString(gSdLine, 5, 117);
    char heap[40];
    snprintf(heap, sizeof heap, "heap %u  largest %u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                     MALLOC_CAP_8BIT)));
    d.fillRect(5, 126, 230, 8, kBg);
    d.setTextColor(kFaint, kBg);
    d.drawString(heap, 5, 126);
    d.fillRect(5, 66, 230, 12, kBg);
    d.setTextFont(1);
    d.setTextColor(kFaint, kBg);
    d.drawString(gNetLine, 7, 66);
    const int bat = tgc::board::batteryPercent();
    char right[24];
    if (bat >= 0)
        snprintf(right, sizeof right, "%d%%", bat);
    else
        snprintf(right, sizeof right, "USB");
    d.fillRect(206, 7, 28, 12, kBg);
    d.setTextFont(1);
    d.setTextColor(bat >= 0 && bat < 20 ? kWarn : kDim, kBg);
    d.setTextDatum(lgfx::top_right);
    d.drawString(right, 233, 7);
    d.setTextDatum(lgfx::top_left);
    d.endWrite();
}

void handleSerial() {
    while (Serial.available()) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\r') continue;
        if (c != '\n') {
            if (gSerialLine.length() < 80) gSerialLine += c;
            continue;
        }
        gSerialLine.trim();
        if (gSerialLine == "w") {
            snprintf(gNetLine, sizeof gNetLine, "NET: joining...");
            gDirty = true;
            if (kk::DevWifi::connect(30000))
                snprintf(gNetLine, sizeof gNetLine, "NET: %s", kk::DevWifi::ip().c_str());
            else
                snprintf(gNetLine, sizeof gNetLine, "NET: join failed");
        } else if (gSerialLine.startsWith("w ")) {
            String rest = gSerialLine.substring(2);
            const int sep = rest.indexOf('|');
            if (sep < 0) {
                Serial.println("[wifi] use: w <ssid>|<password>");
            } else {
                const String ssid = rest.substring(0, sep);
                const String pass = rest.substring(sep + 1);
                if (!kk::DevWifi::saveCredentials(ssid, pass)) {
                    Serial.println("[wifi] not saved (bad SSID or password length)");
                } else if (kk::DevWifi::connect(30000)) {
                    snprintf(gNetLine, sizeof gNetLine, "NET: %s", kk::DevWifi::ip().c_str());
                } else {
                    snprintf(gNetLine, sizeof gNetLine, "NET: join failed");
                }
            }
        } else if (gSerialLine == "t") {
            if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                snprintf(gNetLine, sizeof gNetLine, "API: no Wi-Fi");
            } else {
                runProbe();
            }
        } else if (gSerialLine == "u") {
            if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                snprintf(gNetLine, sizeof gNetLine, "API: no Wi-Fi");
            } else {
                runProbe();
            }
        } else if (gSerialLine.startsWith("k ")) {
            String token = gSerialLine.substring(2);
            token.trim();
            if (kk::DevToken::save(token)) {
                Serial.printf("[auth] token saved (%u chars)\n", static_cast<unsigned>(token.length()));
                if (!kk::DevWifi::connected() && !kk::DevWifi::connect(30000)) {
                    snprintf(gNetLine, sizeof gNetLine, "API: no Wi-Fi");
                } else {
                    runProbe();
                }
            } else {
                Serial.println("[auth] token not saved (empty or too long)");
            }
        } else if (gSerialLine == "c") {
            kk::DevToken::clear();
            Serial.println("[auth] token cleared");
            snprintf(gNetLine, sizeof gNetLine, "API: token cleared");
        } else if (gSerialLine == "h") {
            Serial.printf("[heap] free %u, largest %u, min %u\n",
                          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                          static_cast<unsigned>(heap_caps_get_largest_free_block(
                              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                          static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
        } else if (gSerialLine == "s") {
            if (kk::DevWifi::connected()) {
                Serial.println("[wifi] disconnect first");
            } else {
                WiFi.mode(WIFI_STA);
                const int n = WiFi.scanNetworks();
                Serial.printf("[wifi] %d networks\n", n);
                for (int i = 0; i < n; ++i)
                    Serial.printf("[wifi] %2d  %4d dBm  %s\n", i, WiFi.RSSI(i),
                                  WiFi.SSID(i).c_str());
                WiFi.scanDelete();
                WiFi.mode(WIFI_OFF);
            }
        } else if (gSerialLine == "r") {
            Serial.println("[kk] restart");
            delay(100);
            ESP.restart();
        } else if (!gSerialLine.isEmpty()) {
            Serial.printf("[kk] commands: w=wifi w <ssid>|<pass> t=tls u=user k <token> c=clear h=heap s=scan r=restart\n");
        }
        gSerialLine = "";
        gDirty = true;
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(250);

    tgc::board::begin();
    gDisp = &tgc::board::display();
    gDisp->setRotation(1);
    gDisp->setBrightness(128);

    Serial.printf("\n[kk] KaniKey M0\n");
    Serial.printf("[kk] board: %s\n", tgc::board::name());
    Serial.printf("[kk] flash: %u MB, PSRAM: %u MB\n",
                  static_cast<unsigned>(ESP.getFlashChipSize() / (1024 * 1024)),
                  static_cast<unsigned>(ESP.getPsramSize() / (1024 * 1024)));
    Serial.printf("[kk] internal heap: %u free, %u largest\n",
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                         MALLOC_CAP_8BIT)));
    Serial.printf("[kk] keyboard: %s\n", tgc::board::hasKeyboard() ? "yes" : "no");

    gSdOk = mountSd();
    gDirty = true;
    Serial.println("[kk] first draw");
    draw();
    Serial.println("[kk] first draw done");
    gDirty = false;
    gLastDrawMs = millis();

    Serial.printf("[wifi] saved credentials: %s\n", kk::DevWifi::hasSaved() ? "yes" : "no");
    if (kk::DevWifi::hasSaved()) {
        snprintf(gNetLine, sizeof gNetLine, "NET: joining %s",
                 kk::DevWifi::savedSsid().c_str());
        drawStatus();
        if (kk::DevWifi::connect(30000)) {
            snprintf(gNetLine, sizeof gNetLine, "NET: %s", kk::DevWifi::ip().c_str());
            drawStatus();
            runProbe();
        } else {
            snprintf(gNetLine, sizeof gNetLine, "NET: join failed");
            gDirty = true;
        }
    } else {
        snprintf(gNetLine, sizeof gNetLine, "NET: no saved Wi-Fi");
    }
    drawStatus();
    gLastStatusMs = millis();
}

void loop() {
    tgc::board::update();
    handleSerial();

    KeyEvent e;
    while (tgc::board::nextKeyEvent(&e)) {
        if (e.key == Key::None) {
            // The TCA8418 lost events: treat every key as released.
            gShift = gFn = gCtrl = gOpt = gAlt = false;
            pushLog("^ (all keys released)");
            gDirty = true;
            continue;
        }
        onKey(e);
    }

    const uint32_t now = millis();
    if (gDirty && now - gLastDrawMs >= 16) {
        draw();
        gDirty = false;
        gLastDrawMs = now;
    }
    if (now - gLastStatusMs >= 1000) {
        drawStatus();
        gLastStatusMs = now;
    }

    // G0: a short press wakes the screen; a long press is the future power
    // action. For M0 it just logs, so the button can be checked too.
    if (tgc::board::wasPressed(tgc::board::Button::Select)) {
        Serial.println("[key] G0 pressed");
    }
    if (tgc::board::wasHeld(tgc::board::Button::Select)) {
        Serial.println("[key] G0 held");
    }

    static uint32_t lastBeat = 0;
    if (now - lastBeat > 5000) {
        lastBeat = now;
        Serial.printf("[kk] alive heap=%u\n",
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    }
    delay(4);
}

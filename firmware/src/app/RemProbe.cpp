// REM-01 dev probe. See RemProbe.h.
#include "app/RemProbe.h"

#ifdef KANIKEY_DEV
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>

#include "board/Board.h"

namespace kk::app {
namespace {

constexpr uint32_t kRemMagic = 0x52454D31;  // "REM1"
constexpr uint32_t kRemMaxWakes = 32;       // the count limit and the history size

struct RemWake {
    uint32_t seq;
    uint32_t sleptMs;  // time asleep before this wake
    uint32_t bootMs;   // millis() when this wake went back to sleep; 0 = not yet
};

// RTC_NOINIT memory keeps this through deep sleep and through a software or
// USB-triggered system reset (opening the USB CDC port resets the board, and
// RTC_DATA_ATTR would be re-initialised then, losing the run before the host
// can read it). A power cut leaves garbage, which the magic rejects.
struct RemState {
    uint32_t magic;
    uint32_t seq;        // probe sleeps entered since the power-on (never reset by a run)
    uint32_t wakesLeft;  // timer wakes still to run in this run, including the next one
    uint32_t requestedS;
    uint32_t active;      // a repeat run is in progress
    uint32_t dumpPending; // the run ended; print its history at the first draw
    uint32_t histCount;   // wakes recorded in this run
    uint64_t sleepAtUs;   // rtcMicros() when the last probe sleep began
    RemWake hist[kRemMaxWakes];
};

RTC_NOINIT_ATTR RemState gRem;

bool recordValid() { return gRem.magic == kRemMagic; }

void printHistory() {
    for (uint32_t i = 0; i < gRem.histCount; ++i) {
        const RemWake& w = gRem.hist[i];
        Serial.printf("[rem] hist %u seq %u slept_ms %u boot_ms %u\n",
                      static_cast<unsigned>(i + 1), static_cast<unsigned>(w.seq),
                      static_cast<unsigned>(w.sleptMs), static_cast<unsigned>(w.bootMs));
    }
}

// Enters deep sleep with a timer. The Select/keyboard wake stays armed as in
// the normal path. Never returns.
[[noreturn]] void sleepFor(uint32_t seconds) {
    // A wake of this run ends here: store its boot-to-sleep time.
    if (gRem.histCount > 0) gRem.hist[gRem.histCount - 1].bootMs = millis();
    gRem.seq += 1;
    gRem.sleepAtUs = tgc::board::rtcMicros();
    Serial.printf("[rem] boot_ms %u\n", static_cast<unsigned>(millis()));
    Serial.printf("[rem] sleep seq %u requested_s %u left %u\n", static_cast<unsigned>(gRem.seq),
                  static_cast<unsigned>(seconds), static_cast<unsigned>(gRem.wakesLeft));
    tgc::board::deepSleep(static_cast<uint64_t>(seconds) * 1000000ULL);
}

// `rem <seconds> [count]`: a fresh run. The run keeps the cumulative seq.
void arm(uint32_t seconds, uint32_t count) {
    if (!recordValid()) {
        gRem = RemState{};
        gRem.magic = kRemMagic;
    }
    gRem.active = 1;
    gRem.wakesLeft = count;
    gRem.requestedS = seconds;
    gRem.histCount = 0;
    gRem.dumpPending = 0;
    sleepFor(seconds);
}

void reportState() {
    if (!recordValid()) {
        Serial.println("[rem] no probe record");
        return;
    }
    Serial.printf("[rem] record seq %u active %u wakes_left %u requested_s %u history %u\n",
                  static_cast<unsigned>(gRem.seq), static_cast<unsigned>(gRem.active),
                  static_cast<unsigned>(gRem.wakesLeft), static_cast<unsigned>(gRem.requestedS),
                  static_cast<unsigned>(gRem.histCount));
    printHistory();
}

// `rem tone`: one 880 Hz tone through the existing playTone(), with the heap
// before it and after it has finished. The min is the lowest free heap since
// the boot, so it shows any transient the tone caused.
void toneHeap() {
    const uint32_t freeBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const uint32_t largestBefore =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t minBefore = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    const bool played = tgc::board::playTone(880, 150);
    delay(200);  // the tone runs in the background; the 150 ms tone has ended
    Serial.printf(
        "[rem] tone heap before free %u largest %u min %u; after free %u largest %u min %u (played %d)\n",
        static_cast<unsigned>(freeBefore), static_cast<unsigned>(largestBefore),
        static_cast<unsigned>(minBefore),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
        played ? 1 : 0);
}

}  // namespace

void remBootProbe() {
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    const bool timer = cause == ESP_SLEEP_WAKEUP_TIMER;
    const bool key = cause == ESP_SLEEP_WAKEUP_EXT1;
    const bool valid = recordValid();
    uint32_t sleptMs = 0;
    if (valid && (timer || key)) {
        const uint64_t nowUs = tgc::board::rtcMicros();
        if (nowUs >= gRem.sleepAtUs)
            sleptMs = static_cast<uint32_t>((nowUs - gRem.sleepAtUs) / 1000);
    }
    const uint32_t seq = valid ? gRem.seq : 0;
    const uint32_t requestedS = valid ? gRem.requestedS : 0;
    if (timer)
        Serial.printf("[rem] wake cause timer seq %u slept_ms %u requested_s %u\n",
                      static_cast<unsigned>(seq), static_cast<unsigned>(sleptMs),
                      static_cast<unsigned>(requestedS));
    else if (key)
        Serial.printf("[rem] wake cause ext1 seq %u slept_ms %u requested_s %u\n",
                      static_cast<unsigned>(seq), static_cast<unsigned>(sleptMs),
                      static_cast<unsigned>(requestedS));
    else
        Serial.printf("[rem] wake cause other %d seq %u slept_ms 0 requested_s %u\n",
                      static_cast<int>(cause), static_cast<unsigned>(seq),
                      static_cast<unsigned>(requestedS));

    if (!valid || !gRem.active) return;
    if (!timer) {
        // A key, a reset or a power-on ends the run; this boot is a normal one.
        gRem.active = 0;
        gRem.dumpPending = 1;
        Serial.printf("[rem] repeat aborted by cause %d at seq %u: normal boot\n",
                      static_cast<int>(cause), static_cast<unsigned>(seq));
        return;
    }
    if (gRem.histCount < kRemMaxWakes) {
        RemWake& w = gRem.hist[gRem.histCount];
        gRem.histCount += 1;
        w.seq = seq;
        w.sleptMs = sleptMs;
        w.bootMs = 0;
    }
    if (gRem.wakesLeft > 0) gRem.wakesLeft -= 1;
    // Not the last timer wake: sleep again from here, before Wi-Fi or storage.
    if (gRem.wakesLeft > 0) sleepFor(gRem.requestedS);
    gRem.active = 0;
    gRem.dumpPending = 1;
    Serial.printf("[rem] repeat done after %u timer wakes: normal boot\n",
                  static_cast<unsigned>(gRem.histCount));
}

void remNoteBootReady() {
    Serial.printf("[rem] boot_ms %u\n", static_cast<unsigned>(millis()));
    if (!recordValid() || !gRem.dumpPending) return;
    gRem.dumpPending = 0;
    // The last wake of a run does not sleep again, so its boot time is taken here.
    if (gRem.histCount > 0 && gRem.hist[gRem.histCount - 1].bootMs == 0)
        gRem.hist[gRem.histCount - 1].bootMs = millis();
    printHistory();
}

void remSerialCommand(const String& line) {
    String rest = line.substring(3);
    rest.trim();
    if (rest.isEmpty()) {
        reportState();
        return;
    }
    if (rest == "tone") {
        toneHeap();
        return;
    }
    const int sep = rest.indexOf(' ');
    const long seconds = (sep < 0 ? rest : rest.substring(0, sep)).toInt();
    String countText = sep < 0 ? String("1") : rest.substring(sep + 1);
    countText.trim();
    const long count = countText.toInt();
    if (seconds < 1 || seconds > 86400 || count < 1 || count > static_cast<long>(kRemMaxWakes)) {
        Serial.printf("[rem] use: rem <seconds 1-86400> [count 1-%u], rem (log), rem tone\n",
                      static_cast<unsigned>(kRemMaxWakes));
        return;
    }
    Serial.printf("[rem] arm %ld s x %ld timer wakes, Wi-Fi off on probe wakes; sleeping now\n",
                  seconds, count);
    arm(static_cast<uint32_t>(seconds), static_cast<uint32_t>(count));
}

}  // namespace kk::app
#endif

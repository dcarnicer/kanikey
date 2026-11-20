# Bluetooth keyboard and PaperDIY

Status: **spike brief, 2026-11-19; BT-01 probe evidence and BT-02 seam
status added 2026-11-20**. This prepares the post-v1 spikes BT-01 (BLE HID host
on the ESP32-S3) and PD-01/PD-02 (M5Stack PaperDIY board and e-paper
rendering). It is a specification, not an implementation: no firmware, test,
partition or library is changed here. §1.7 records compiled size probes, a
conditional BT-02 decision and the seam's review status; those are desktop
evidence, not a hardware run. No new number in this file is a KaniKey hardware
measurement; build figures are from `pio run`, heap figures from
`docs/hardware.md`, and vendor/third-party figures are labelled. Every
measurement the spikes must take is defined here; results go to
`docs/hardware.md` as a new dated section. Claims marked `verify` are
unconfirmed and need a probe or a build.

The Cardputer ADV stays the primary device. BT-01 is a go/no-go gate for an
optional keyboard; PD-01/PD-02 is a separate target whose first release is a
read-only companion. Full PaperDIY reviews depend on BT-01…BT-04.

---

## 1. BT-01 — BLE HID host on the ESP32-S3

- **Priority:** P3 · **Size:** M · **Depends on:** none
- **Touches (throwaway probes only, not merged):** `firmware/platformio.ini`
  (probe envs), `firmware/src/board/BoardCardputerAdv.cpp` (queue/init),
  `firmware/src/app/App.cpp` (input pump), `docs/hardware.md` (new dated
  section).
- **Settings:** none.

### 1.1 What the current build gives us

The Cardputer ADV is an ESP32-S3FN8: 8 MB flash, **no PSRAM**, BLE 5.0 only.
The S3 has no Bluetooth Classic radio
([ESP-IDF S3 BLE overview](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/ble/overview.html),
[architecture table](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/bt-architecture/overview.html));
Classic-only keyboards are out of scope.

The build is `espressif32@6.12.0` = Arduino-ESP32 **2.0.17** on ESP-IDF
**4.4.7** (`framework-arduinoespressif32@3.20017`; the installed
`esp_idf_version.h` says 4.4.7). The framework already ships the ESP-IDF HID
host:

- `libesp_hid.a` exports `esp_hidh_init`, `esp_hidh_dev_open`,
  `esp_hidh_dev_close` and the input-event API.
- The S3 `sdkconfig` has `CONFIG_BT_BLUEDROID_ENABLED=y`,
  `CONFIG_GATTC_ENABLE=y`, `CONFIG_SW_COEXIST_ENABLE=y` and
  `CONFIG_ESP32_WIFI_SW_COEXIST_ENABLE=y`; `CONFIG_BT_HID_HOST_ENABLED` is
  **not** set (no Classic HID host, correct for the S3).
- `esp_hid` compiles `ble_hidh.c` for a Bluedroid build and `nimble_hidh.c`
  for a NimBLE build
  ([CMakeLists](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_hid/CMakeLists.txt)).
  This Arduino core is Bluedroid, so `esp_hidh`'s BLE transport means
  **Bluedroid's GATTC**, not NimBLE.

No Bluetooth code exists in the app today. `platformio.ini` links only
M5Unified (pinned 0.2.26, commit `e125e73`) and M5GFX (resolved 0.3.0).

The input path BLE must join:

- `board::nextKeyEvent()` is the only key source. On the Cardputer it drains a
  16-entry ring in `BoardCardputerAdv.cpp`; a full ring drops the oldest event
  and inserts `KeyEvent{Key::None, false}` as the newest.
- `App::loop()` drains every event into `onKey(e)`;
  `App::onKey()` and `syncStopRequested()` call
  `updateModifier(e.x, e.y, e.down)`, which drives five globals
  (`gShift`, `gFn`, `gCtrl`, `gOpt`, `gAlt`).
- Text entry is per-screen: review, lesson, quiz, settings, study and
  critical-list handlers call `cardputerCharAt(e.x, e.y, gShift)` directly.
  `KeyEvent` has only `key`, `down`, `code`, `x`, `y`.
- `KeyRepeat` repeats only the arrows, using `millis()`.

Budget that decides the stack: the app slot is 1,835,008 B
(`partitions/kanikey_8MB.csv`, app0/app1 at 0x1C0000); the release image is
1,556,989 B (84.8%), leaving **278,019 B**. Static RAM is 96,248 B of
327,680 B (29.4%). `README.md` §2 plans ~140 KB free internal heap; the log
has ~225 KB after boot and ~148 KB before TLS, with the 32.4 KB canvas
released around requests. Loop stack 24 KiB, HTTP body buffer 32 KiB. The
250 KB round budget is a round-2 rule, not a BT budget; BT needs its own
partition decision.

**Current build check (2026-11-20).** `pio run -e cardputer-adv` in this
worktree builds at **1,664,221 B of 1,835,008 B (90.7%)** and **105,400 B
static RAM (32.2%)**. The 2026-11-19 release figure above is historical; the
current image leaves **170,787 B (9.3%)** in the slot, not 278,019 B. The
last measured TLS worst point is 22,836 B free / 24,564 B largest during a
35-request sync on a 96,336 B-static dev image (`docs/hardware.md`,
2026-11-19 SPEED-05); the current image has 9,064 B more static RAM, so its
worst point must be re-measured before it is used as a BT budget.

### 1.2 Candidate host stacks

| | **A. `esp_hidh` + Bluedroid BLE** | **B. NimBLE-Arduino + HOGP client** | **C. Bluedroid GATTC, raw** |
|---|---|---|---|
| What | ESP-IDF HID host; HOGP on Bluedroid GATTC | NimBLE host; we write HOGP | Bluedroid GATTC; we write HOGP |
| Integration | Already in the SDK; add `btStart()`, `esp_bluedroid_init()/enable()`, the `esp_hidh_gattc_event_handler` callback, GAP security, scan, `esp_hidh_init()`, `esp_hidh_dev_open()` | New `lib_deps`; `NimBLEDevice::init()`, scan, connect, discover 0x1812, subscribe; own report-map parser and bonding | Same HOGP work as B on a heavier host |
| HOGP | Complete in `ble_hidh.c`: 0x1812, report map 0x2A4B, report char 0x2A4D, report ref 0x2908, CCCD, boot protocol, battery 0x180F, device info 0x180A ([source](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_hid/src/ble_hidh.c)) | GATT client and bonding only; HOGP is our code. The report-map parser can be ported from `esp_hid_common.c` (Apache-2.0) | Same as B |
| Pairing | Bluedroid SMP; the host GAP handler logs `PASSKEY_NOTIF`/`NC_REQ`/`PASSKEY_REQ` and answers a security request. The shared IDF GAP example's setup uses `ESP_IO_CAP_IO` + `ESP_LE_AUTH_REQ_SC_MITM_BOND` ([example](https://github.com/espressif/esp-idf/blob/v4.4.7/examples/bluetooth/esp_hid_host/main/esp_hid_gap.c)); the host probe sets its own IO cap | NimBLE SMP; we choose IO cap/bonding and store keys in NVS | Bluedroid SMP |
| Flash | Bluedroid is the large one: a third-party S3 build measured ~409 KB for the BLE host, ~534 KB full stack, vs ~79 KB/~170 KB for NimBLE ([measurement](https://benign.host/blog/esp32-ble-flash-budget/); indicative, not ours) | NimBLE-Arduino claims ~50% less flash and ~100 KB less RAM than Bluedroid ([registry](https://registry.platformio.org/libraries/h2zero/NimBLE-Arduino)); plus our HOGP code | Same Bluedroid cost as A, plus code; no advantage |
| RAM | BT controller + Bluedroid tasks + GATTC cache; measure at boot, init, connect, TLS | BT controller + NimBLE tasks + GATT client; measure the same | Same as A |
| Effort | Lowest: HOGP, report parsing and CCCD exist; work is init, scan/connect policy, pairing UI, queue | Highest: HOGP + report-map parser + pairing + tests; lower stack cost | High, no benefit over A |
| Risk | Measured +553 KB; does not fit 1.75 MiB and may not fit the TLS heap; Bluedroid RAM/coexistence | More code/failure modes; NimBLE-Arduino 2.5.2/1.4.3 compile on core 2.0.17 but runtime and HOGP are unverified | Same as A without the HOGP implementation |

`esp_hidh` can be built on NimBLE in a pure ESP-IDF build, but the Arduino
core is Bluedroid. Rebuilding the framework is out of scope; if A does not
fit, **B** is the path, not a hybrid.

**Measured on this core (2026-11-20).** A throwaway probe with Wi-Fi in both
baseline and BT builds measured the host delta over a 673,369 B flash /
43,168 B static-RAM Wi-Fi baseline: **A +553,492 B / +20,892 B**, **B
(NimBLE-Arduino 2.5.2) +201,140 B / +6,404 B** (1.4.3: +221,544 B / +6,228 B).
The probe does not include M5GFX/TLS or B's HOGP client. Full estimates and
the method are in §1.7.

API facts for the probe:

- `esp_hidh_dev_open()` takes BDA, `ESP_HID_TRANSPORT_BLE` and address type;
  it opens a **direct GATTC connection** and blocks on semaphores until
  discovery completes
  ([source](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_hid/src/ble_hidh.c)).
  Scanning is separate, so pairing/connection must not run on the UI loop.
- `ESP_HIDH_INPUT_EVENT` carries `usage`, `report_id`, `length`, `data`,
  `map_index`; the data pointer is only valid in the callback. Copy the report
  into the app queue there.
- `esp_hidh` parses the report map into report items but does **not** decode
  the bitfields. Standard keyboards are usually the boot layout (modifier +
  6 keys), but check each model; a nonstandard map may be a no-go.
- `esp_ble_hidh_init()` creates `esp_ble_hidh_events` (stack 2048 if
  `event_stack_size` is 0). Use 4096 and measure the high-water mark.

### 1.3 Questions and exact measurements

Every probe is a throwaway branch; nothing is merged. Use the release build
(`pio run -e cardputer-adv`) and `CORE_DEBUG_LEVEL=1` for all size/heap
numbers.

#### Q1 — Which stack fits the Arduino build?

Build the same minimal app for A/B (and C only if A's API blocks): start the
stack, scan 10 s, connect one keyboard, log every report, disconnect after
60 s. A: `btStart()`, `esp_bluedroid_init()`, `esp_bluedroid_enable()`,
`esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler)`, GAP security,
`esp_hidh_init()`, scan, `esp_hidh_dev_open()`. B: `NimBLEDevice::init()`,
scan, connect, discover 0x1812, subscribe; a stub parser is enough for the
size comparison, but the final probe must parse a real report map.

| Metric | Point | Method |
|---|---|---|
| Flash | release image | `pio run` size line + linker map; attribute `libbt.a`, `libesp_hid.a`/NimBLE objects |
| Static RAM | release image | `.dram0`/`.bss` from the size line |
| Internal free heap | boot; BT init; connect; Wi-Fi join; TLS `/summary`; disconnect | `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` |
| Largest block | same points | `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)` |
| Task stacks | after connect | `uxTaskGetSystemState()` for BT/NimBLE/`esp_ble_hidh_events` |
| Boot time | cold boot → first key | `millis()` at the first input event |

The probe must state its partition. The current 1,835,008 B slot has
170,787 B free (2026-11-20 build); the measured probe deltas show neither
stack fits it, so use a throwaway `kanikey_bt.csv` with two ~2.5 MB app slots
and ~2 MB LittleFS. The final table is a separate decision.

**Pass:** one stack fits a 2.5 MB slot with ≥ 10% OTA headroom, leaves
≥ 60 KB internal heap / ≥ 40 KB largest block after BT + Wi-Fi + canvas at
idle, keeps ≥ 8 KB free / ≥ 4 KB largest at the worst point of 20 `/summary`
runs, and `/summary` succeeds 20/20.

#### Q2 — Do standard BLE HID keyboards pair and type?

At least **three BLE keyboards** plus one Classic-only negative control.
Confirm each transport before the run (`verify`):

| # | Keyboard | Transport | Pairing | Why |
|---:|---|---|---|---|
| 1 | Logitech K380 / MX Keys Mini | BLE HOGP `verify` | Just Works/passkey | Common multi-host; host switching |
| 2 | Apple Magic Keyboard | BLE HOGP `verify` | Just Works | Common; aggressive idle sleep |
| 3 | Keychron K3 Pro / K7 | BLE HOGP `verify` | Just Works/passkey | Mechanical, larger report map likely |
| 4 | Microsoft Designer Compact | BLE HOGP `verify` | Just Works | Different vendor/map |
| 5 | Anker/Arteck class | **Classic only** `verify` | n/a | Negative control; expected no-go |

For each BLE keyboard record: name/BDA/address type; whether 0x1812 is
advertised (do not rely on it); appearance (keyboard 0x03C1); report-map
length/layout; boot-protocol support; 6KRO/NKRO; modifier byte;
arrow/Enter/Esc/Tab/Backspace; battery service; Caps Lock output report;
first key after connect; disconnect reason.

Pairing flows:

- **Just Works:** `ESP_IO_CAP_NONE` + `ESP_LE_AUTH_BOND` (or
  `ESP_LE_AUTH_REQ_SC_BOND`). No passkey UI.
- **Host displays, keyboard enters:** `ESP_IO_CAP_OUT`; show the 6-digit code
  on screen; the keyboard types it (`PASSKEY_NOTIF`).
- **Numeric comparison:** `ESP_IO_CAP_IO`; show the code, confirm with Enter
  (`NC_REQ`, reply `esp_ble_confirm_reply()`).
- **Host enters, keyboard displays:** `ESP_IO_CAP_IN`; type the keyboard's
  code on the built-in keyboard. This is impossible on PaperDIY without a
  keyboard, so BT on PaperDIY needs Just Works or the host-displays flows.

Bonding: pair, reboot, reconnect without re-pairing; check
`esp_ble_get_bond_device_num()`; `Forget` calls `esp_ble_remove_bond_device()`.
Bonds must survive deep sleep and OTA.

**Pass:** three BLE keyboards pair and type all UI keys; Just Works and one
passkey/numeric flow pass; unsupported report layouts are listed as no-go for
that model, not silently mis-typed.

#### Q3 — Reconnect after boot and deep sleep

Deep sleep turns BLE off. The built-in keyboard remains the only wake source;
BLE never wakes the device. Measure 20 trials per case:

| Case | Record |
|---|---|
| Cold boot, keyboard awake | connect time, first-key time, success |
| Deep-sleep wake, keyboard awake | same; bond reuse; re-pair needed? |
| Keyboard power-cycled | automatic reconnect, or a new `esp_hidh_dev_open()`? |
| Out of range 2 min, return | reconnect time, stuck/duplicate keys |
| Host reset while keyboard asleep | does a keypress make it connectable? |

Many keyboards advertise only for a window and then sleep. If a keypress is
required, document it as a UX constraint; do not promise instant reconnect.

**Pass:** p95 cold reconnect ≤ 5 s; p95 deep-sleep reconnect ≤ 3 s with the
keyboard awake; 20/20; the product text says whether a keypress is needed.

#### Q4 — Key latency and Wi-Fi coexistence

Two numbers, not one:

1. **Radio/host:** keyboard HID notification → event in KaniKey's queue. Use a
   second ESP32-S3 as a HID keyboard emulator: toggle a GPIO before
   `esp_hidd_dev_input_set()`; KaniKey toggles another in the HID callback.
   Logic analyzer ≥ 1 MHz; 100 presses per condition, 200 ms apart, discard
   the first 10.
2. **App-visible:** queue → `onKey()` in `App::loop()`, timestamped with
   `esp_timer_get_time()`. This is what the user feels.

Conditions: BLE only (Wi-Fi off); Wi-Fi idle; during a blocking TLS
`GET /summary`; during a 20-subject fetch; keyboard at 1 m with RSSI and a
fixed AP channel (1/6/11).

Record p50/p95/max, lost/stuck keys, queue overflows. Run 1,000 presses
(Shift+letter, arrows, Enter, Esc, Tab, Backspace) and count missing down/up.
The 16-entry queue can overflow in a 2 s blocking request; quantify the burst
so BT-02 can size the queue (32/64 entries or a FreeRTOS queue are design
outputs). Wi-Fi and BLE share one radio; the sdkconfig already enables
coexistence. Measure 20 TLS `/summary` runs with BLE off/idle/typing; try the
default vs `ESP_COEX_PREFER_BT` (present but deprecated in the installed
`esp_coexist.h`) and one connection interval (7.5/15/30 ms if the keyboard
allows).

**Pass:** radio/host p95 ≤ 50 ms idle, ≤ 100 ms during TLS; app p95 ≤ 100 ms
when no request blocks; zero lost/stuck keys in 1,000 presses; TLS p95
regression ≤ 20% and 20/20 handshakes. A multi-second app block during TLS is
a BT-04 finding, not a Q4 pass: a background sync must not lose keys even if
the UI cannot act until the request returns.

#### Q5 — Battery cost

Use a PPK2 or INA219 in series with the battery; 60 s windows, 10 min for
sleep. Measure: BLE off (Wi-Fi off, idle); BLE connected idle at 7.5/15/30 ms;
BLE typing at 1 key/s; BLE + Wi-Fi idle; cold connect (mAh); a simulated
20-minute review session. Report mA, mAh/h and % of 1500 mAh.

M5Stack's Cardputer-Adv figures are a sanity check, not our measurement:
operating 120.2 mA, Wi-Fi 132.3 mA, BLE 154.6 mA at 4.2 V
([specs](https://docs.m5stack.com/en/core/Cardputer-Adv)). They are
"operating" numbers, not connected-idle.

**Pass (proposed):** connected idle adds ≤ 10 mA over BLE-off; a 20-minute
session uses ≤ 20% more energy; a cold connect costs ≤ 0.1 mAh. Otherwise the
fallback is "BLE on only during a session" or a no-go.

### 1.4 Go/no-go for BT-01

Hard gates; any failure is no-go for that stack. Record numbers and decision
in `docs/hardware.md`.

1. Fits a 2.5 MB OTA slot with ≥ 10% headroom, or a documented partition
   change keeps LittleFS ≥ 2 MB.
2. After BT + Wi-Fi + canvas at idle: internal heap ≥ 60 KB, largest block
   ≥ 40 KB. During 20 `/summary` runs: ≥ 8 KB free / ≥ 4 KB largest at the
   worst point, and 20/20 succeed.
3. Three BLE keyboards pair and type; Just Works and one passkey/numeric flow
   pass; Classic-only documented out of scope.
4. 1,000 presses: zero lost/stuck; queue overflow known; p95 radio/host
   ≤ 50 ms idle, ≤ 100 ms during TLS; app p95 ≤ 100 ms when not blocked.
5. Reconnect p95 ≤ 5 s cold, ≤ 3 s after deep-sleep wake; 20/20.
6. TLS p95 regression ≤ 20%; 20/20 handshakes; no new 4xx/5xx.
7. Battery within Q5.

Decision: **A** if it passes 1–7; **B** if A fails 1 or 2 and B passes 1–7;
**C** only if A's API makes HOGP impossible. If both fail, BT-01 is a no-go
with the measured reason. No library is merged; BT-02's transport starts only
after a go.

The 2026-11-20 probe narrows this: both stacks need a larger slot (A 2.5 MB;
B at least 2.25 MB, 2.5 MB recommended; the current 1.75 MiB slot cannot hold
either). A fits 2.5 MB at an estimated 84.6% before a full app+BT link, and B
at ~71% before its HOGP client. Gate 2 (heap during TLS, not idle) is the
deciding risk, and A's fixed task-stack floor makes it the less likely of
the two. See §1.7.

### 1.5 Source-neutral input abstraction (BT-02 requirement)

Screens must not learn about Bluetooth. The board layer owns one ordered
queue; matrix, BLE and (later) PaperDIY buttons push `KeyEvent`s into it.

```cpp
enum class KeySource : uint8_t { Matrix, Ble, Button };

struct KeyEvent {
    Key key = Key::None;
    bool down = false;
    uint8_t code = 0;      // matrix key number; 0 for BLE/button
    uint8_t x = 0, y = 0;  // matrix grid; 0 for BLE/button
    char ch = 0;           // US-layout base character; 0 when none
    uint8_t mods = 0;      // Shift/Ctrl/Alt/Gui at event time
    KeySource source = KeySource::Matrix;
};
```

- **Char vs matrix.** `ch` is the unshifted US character (`a`, `1`, `;`,
  space); `mods` carries Shift/Ctrl/Alt/Gui. `keyChar(e, shiftHeld)` applies
  Shift for BLE/button events and falls back to `cardputerCharAt(e.x, e.y,
  shiftHeld)` for matrix events. Every current `cardputerCharAt` call site
  switches to it, so no screen sees `x/y` for a BLE event. The matrix path
  keeps `code/x/y` and the modifier globals; BLE sets `mods` every event.
- **HID reports are stateful.** Keep the previous modifier byte and 6-key
  array; diff each report into releases, modifier changes and presses. Emit
  modifier-down before key-down and key-up before modifier-up, so `gShift` is
  right. A no-key report produces individual releases, not `Key::None`.
  Reserve `Key::None` for matrix overflow and BLE disconnect.
- **Boot vs report protocol.** `esp_hidh` gives raw report data, not decoded
  fields. Support the boot layout (modifier + 6 keys) and the common report
  layout; log the map and raw bytes per keyboard. A nonstandard map is parsed
  explicitly or listed unsupported. NKRO bitmaps are out of scope.
- **One thread-safe queue.** The BLE callback runs on the HID task;
  `nextKeyEvent()` on the loop task. Use a critical section or FreeRTOS queue
  around the same ring; size it from Q4. Keep "oldest dropped + `Key::None`"
  only for matrix overflow; a BLE overflow must not silently cancel held keys.
- **Order/priority.** Arrival order; no source priority. Built-in always
  works. BLE pairing starts only from Settings, never during a review/lesson
  (BT-03); BLE cannot open the setup portal by itself.
- **Native tests (BT-02):** `hidUsageToKey` for letters/digits/punctuation/
  Space/Enter/Esc/Tab/Backspace/arrows; `hidModifierBits`; `HidKeyboard::diff`
  for press/release/rollover/Shift+letter/all-up; `keyChar` Shift; unsupported
  usage ignored; boot-report parse; `releaseAll`; the BLE repeat skip. A
  nonstandard or common report layout is still a pair-time/transport check.

**Current-code facts (2026-11-20, after the BT-02 seam).** `KeyEvent` now has
`source/ch/mods`; matrix events keep `code/x/y` and clear `ch/mods`, while a
BLE/button event carries its own character and modifier mask. `Key` still has
no letter members, so a BLE letter is `Key::Other` plus `ch`. `App::onKey()`
calls `updateModifier(e)`, and screen code calls `keyChar(e, gShift)`; the dev
console's synthetic matrix lookup is the only remaining direct
`cardputerCharAt` use. Five rules guided the seam; the first three are in the
tree, the last two belong to the BLE transport:

- `updateModifier` takes the event (not `x/y`): BLE events have `x=y=0`, and
  `gShift` must be right before the first letter reaches a screen.
- `keyChar(e, shiftHeld)` applies Shift from `e.mods` only for
  `Ble`/`Button`; for `Matrix` it ignores `e.mods` and calls
  `cardputerCharAt(e.x, e.y, shiftHeld)`. Shift is applied once.
- `KeyRepeat` skips non-matrix events: a BLE keyboard sends its own repeat
  reports, and repeating again in `App::loop()` would duplicate arrows and
  Backspace.
- The ring is still not thread-safe (plain head/count). A BLE callback on
  `esp_ble_hidh_events` and `nextKeyEvent()` on the loop task need a
  `portMUX_TYPE` critical section or a FreeRTOS queue. A 64-entry
  `KeyEvent` queue is under 1 KB at the proposed size; size it from Q4.
- On BLE disconnect, `HidKeyboard::releaseAll()` releases every key the diff
  still holds and the transport must push a `Key::None` up so modifiers cannot
  stick. On BLE queue overflow, do not silently use `Key::None`: release the
  held keys explicitly or set a "lost" flag for BT-03. Keep the existing
  matrix overflow rule.

### 1.6 BT-01 risks and open questions

- **Bluedroid flash.** The current image has 170,787 B free; the probe's A
  delta is 553,492 B, so the current slot cannot hold it and a 2.5 MB
  partition change is required. NimBLE is ~201–222 KB but needs HOGP code.
- **Pairing hook.** `esp_hidh` has no public way to start pairing between
  connect and discovery; a keyboard that does not send a Security Request is
  a per-model risk. NimBLE lets us call `secureConnection()` at the right
  point, but the flow is still unverified.
- **TLS heap.** The last measured minimum is 22.8 KB on a smaller image; A's
  15.6 KB task-stack floor plus 20.9 KB static delta likely exhausts it, and
  B's 8.2 KB + 6.4 KB is borderline. Measure BLE-on-TLS before the BLE transport.
- **Report protocol.** A nonstandard map may rule out a model.
- **Reconnect UX.** Keyboards that advertise only after a keypress cannot be
  always ready; deep sleep makes it worse.
- **Blocking UI.** Keys can queue but the UI cannot act during a 2 s TLS
  request; BT-04 must pump keys or accept the delay.
- **Bonds.** Confirm Bluedroid/NimBLE bonds survive OTA and deep sleep.

### 1.7 BT-01 probe evidence and BT-02 decision (2026-11-20)

Everything here was built in this worktree with `espressif32@6.12.0`
(Arduino-ESP32 2.0.17, IDF 4.4.7). It is a desktop probe, not a hardware
run: no keyboard was paired and no current was measured. A full app + BT link
was not built; the estimates below are the measured probe delta added to the
measured app image, and are labelled as estimates.

#### What the pinned core actually provides

- `pio pkg list -e cardputer-adv` resolves
  `framework-arduinoespressif32 @ 3.20017.241212+sha.dcc1105b`; the installed
  `esp_idf_version.h` reports 4.4.7.
- `tools/sdk/esp32s3/lib/libesp_hid.a` contains `ble_hidh.c.obj`,
  `esp_hidh.c.obj` and `esp_hid_common.c.obj`. `nm` exports
  `esp_hidh_init`, `esp_hidh_dev_open`, `esp_hidh_dev_close`,
  `esp_hidh_dev_free`, `esp_hidh_dev_output_set`, `esp_hidh_dev_reports_get`,
  `esp_hidh_dev_report_maps_get` and `esp_hidh_gattc_event_handler`.
  `esp_hidh_bluedroid.h` guards `esp_hidh_dev_open()` with
  `CONFIG_BLUEDROID_ENABLED` (the legacy alias is set too);
  `esp_hidh_gattc.h` guards the GATTC handler with `CONFIG_GATTC_ENABLE`.
  Both are set, so the host API is compiled in.
- `tools/sdk/esp32s3/sdkconfig` sets `CONFIG_BT_BLUEDROID_ENABLED=y`,
  `CONFIG_BT_BLE_ENABLED=y`, `CONFIG_GATTC_ENABLE=y`,
  `CONFIG_BT_BLE_50_FEATURES_SUPPORTED=y`,
  `CONFIG_BT_BLE_42_FEATURES_SUPPORTED=y`, `CONFIG_SW_COEXIST_ENABLE=y` and
  `CONFIG_ESP32_WIFI_SW_COEXIST_ENABLE=y`. `CONFIG_BT_NIMBLE_ENABLED` and
  `CONFIG_BT_HID_HOST_ENABLED` are not set.
- `libraries/BLE/src/BLEHIDDevice.h` is device-side only: its constructor
  takes a `BLEServer`, it creates `BLECharacteristic`s and exposes
  `inputReport()`/`outputReport()`/`startServices()`. There is no Arduino
  host class. The host is the ESP-IDF `esp_hidh` component, already in the
  SDK, not a PlatformIO library.
- No maintained Arduino HOGP-host library was found (the PlatformIO registry
  has no `esp_hidh` package; the GitHub projects are small, unvetted, or
  gamepad-specific). `esp_hidh` is the only complete HOGP implementation in
  the pinned core. NimBLE-Arduino supplies the GATT client and bonding, but
  HOGP is application code.
- `esp_hidh` is not a drop-in host: the Arduino core has no `esp_hid_gap.c`.
  The probe must implement the GAP callback (scan, security request, passkey
  and numeric-comparison replies), the scan, and the worker task around the
  blocking `esp_hidh_dev_open()`.

#### Size probe (compiled, not flashed)

The probe used the same platform, board (`esp32-s3-devkitc-1`), `qio_qspi`
memory type, `kanikey_8MB.csv` partition table and `-DCORE_DEBUG_LEVEL=1` as
`env:cardputer-adv`. Both baseline and BT builds call `WiFi.mode(WIFI_STA)`;
none includes M5GFX, TLS or app code.

| Probe build | Flash | Static RAM |
|---|---:|---:|
| Wi-Fi baseline | 673,369 B | 43,168 B |
| A: Bluedroid + `esp_hidh` | 1,226,861 B | 64,060 B |
| B: NimBLE-Arduino 2.5.2 | 874,509 B | 49,572 B |
| B: NimBLE-Arduino 1.4.3 | 894,913 B | 49,396 B |

Deltas over the Wi-Fi baseline: **A +553,492 B flash / +20,892 B static
RAM**; **B 2.5.2 +201,140 B / +6,404 B**; B 1.4.3 +221,544 B / +6,228 B.
Both NimBLE versions compile and link on core 2.0.17; 2.5.2 is smaller in
this probe. The probe does not include the HOGP client that B still needs.

The A probe linked 155 `libbt.a` objects, plus `ble_hidh.c.obj`,
`esp_hidh.c.obj` and `esp_hid_common.c.obj`; the B probe and the app linked
one `libbt.a` object (`bt.c.obj`), and B also linked
`libNimBLE-Arduino.a`. The controller (`libbtdm_app.a`, 53 objects) is in
both builds and in the app's current map, so the delta is the host, not the
radio. The probe included
`<esp_hidh.h>`, `<esp_gap_ble_api.h>` and `<esp_gattc_api.h>` with no extra
`build_flags` or `lib_deps`.

Adding the probe delta to the current `cardputer-adv` image (1,664,221 B /
105,400 B static RAM):

| Estimate | Flash | Current 1,835,008 B slot | 2.5 MB (2,621,440 B) slot | Static RAM |
|---|---:|---|---|---:|
| A | 2,217,713 B | over by 382,705 B | 84.6%, 15.4% headroom | 126,292 B (38.5%) |
| B 2.5.2 | 1,865,361 B | over by 30,353 B | 71.2%, 28.8% headroom | 111,804 B (34.1%) |

B's estimate excludes the HOGP client; budget another 10–20 KB and re-link
before committing. B would also fit a 2.25 MB (2,359,296 B) slot at 79.1%
before HOGP, but 2.5 MB is the safer shared table. A 2.5 MB table with two
app slots and a ≥2 MB LittleFS is arithmetic, not a committed table:
`nvs 0x5000`, `otadata 0x2000`, `app0 0x280000 @0x10000`,
`app1 0x280000 @0x290000`, `spiffs 0x2F0000 @0x510000` = 3,080,192 B. The
final table is a separate decision.

#### Pairing and connection constraints

- `esp_hidh_dev_open()` calls `esp_ble_gattc_open(..., is_direct=true)`,
  waits for open/search, reads the services, and only then posts
  `ESP_HIDH_OPEN_EVENT` and attaches report listeners. It blocks on
  semaphores and must run on a worker task, not `App::loop()`.
- Its report-map and report-reference reads use
  `ESP_GATT_AUTH_REQ_NO_MITM`. It does not initiate pairing. In IDF 4.4.7 the
  public Bluedroid GAP API has `esp_ble_gap_security_rsp()`,
  `esp_ble_passkey_reply()`, `esp_ble_confirm_reply()` and
  `esp_ble_set_encryption()`, but **no `esp_ble_gap_security_req()`**.
  Pairing therefore depends on the keyboard sending a Security Request
  (`ESP_GAP_BLE_SEC_REQ_EVT`) or on a separate `esp_ble_set_encryption()`;
  `esp_hidh_dev_open()` has no hook between connect and discovery. This is
  the main per-model risk in A. B can call `secureConnection()` at the point
  it chooses.
- Flows to probe: Just Works (`ESP_IO_CAP_NONE` + `ESP_LE_AUTH_BOND` or
  `ESP_LE_AUTH_REQ_SC_BOND`), host-displays (`ESP_IO_CAP_OUT` +
  `PASSKEY_NOTIF`; the keyboard types the code), numeric comparison
  (`ESP_IO_CAP_IO` + `NC_REQ` + `esp_ble_confirm_reply()`), and host-input
  (`ESP_IO_CAP_IN`; type the keyboard's code on the built-in keyboard).
  Host-input is unusable on PaperDIY, which has no keyboard.
- Bonds live in NVS: `esp_ble_get_bond_device_num()`,
  `esp_ble_get_bond_device_list()`, `esp_ble_remove_bond_device()`.
  `initArduino()` already calls `nvs_flash_init()`; the app does not need a
  second call. Confirm bonds survive reboot, deep sleep and OTA.
- `esp_hidh_dev_free()` must be called from `ESP_HIDH_CLOSE_EVENT`; closing
  without freeing leaks the device. The OPEN event is posted before the CCCD
  subscriptions; `esp_hidh_dev_open()` returns after them, so treat the
  return as ready and log the first key separately.
- The report-map parser gives `report_id`, `usage`, `value_len` and
  `map_index` but not decoded bitfields. Support the boot layout (modifier +
  reserved + 6 keys) and the common 8-byte report; log the raw map and bytes
  for every model. NKRO bitmaps remain out of scope.

#### Coexistence and heap

- Coexistence is compiled in (`CONFIG_SW_COEXIST_ENABLE=y`,
  `CONFIG_ESP32_WIFI_SW_COEXIST_ENABLE=y`). `esp_coex_preference_set()` is
  present but deprecated in the installed `esp_coexist.h`; use
  `esp_coex_status_bit_set/clear()` if a preference is needed. The keyboard
  chooses the connection interval; `esp_ble_gap_set_prefer_conn_params()` can
  request 7.5–30 ms but the keyboard may ignore it.
- The app turns Wi-Fi off outside sync; BLE would be the only radio at idle.
  During sync both share the radio. The BLE host's task stacks are fixed at
  init: controller 4096, BTC 3072, BTU 4352 and `esp_ble_hidh_events` 4096 =
  **15,616 B** for A; NimBLE uses controller 4096 + host 4096 = **8,192 B**
  for B, plus TCBs, queues, GATTC cache and report allocations.
- The last measured worst point is **22,836 B free / 24,564 B largest**
  during a 35-request sync on the 2026-11-19 image (96,336 B static RAM).
  The current image has 105,400 B static RAM, 9,064 B more, so its worst
  point must be re-measured; do not assume 22.8 KB still holds. The static
  deltas alone (A +20,892 B, B +6,404 B) consume most of that margin before
  the BT task stacks allocate. On this arithmetic A cannot coexist with the
  last measured TLS minimum, and B is borderline. This is the single most
  important hardware measurement; a BLE-on-TLS minimum below 8 KB free or
  4 KB largest is a no-go for that stack.

#### Recommended probe plan

1. **Keep the seam source-neutral first.** Add `KeySource`, `ch`, `mods` and
   `keyChar()` behind native tests; do not include a BLE header in screens.
   This work is useful for PaperDIY buttons even if BT is dropped.
2. **Probe B (NimBLE-Arduino 2.5.2, pinned).** Implement scan → connect →
   0x1812 discovery → report-map parse → CCCD subscribe → report diff, with
   a worker task and a FreeRTOS queue. Keep A's `esp_hidh` probe as the
   reference implementation and size baseline.
3. **Measure on the Cardputer ADV** with `CORE_DEBUG_LEVEL=1`; log
   `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` and
   `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`
   at boot, BT init, connect, Wi-Fi join, before/during/after TLS
   `/summary`, after 20 subjects, and after disconnect; dump
   `uxTaskGetSystemState()` after connect.
4. **Pairing matrix:** three BLE keyboards (Logitech/Apple/Keychron or the
   available equivalents) plus one Classic-only negative control; Just Works
   and one passkey or numeric flow; record pairing latency (connect → OPEN →
   first key), the exact GAP events per model and whether the model sends a
   Security Request.
5. **Reconnect and latency:** 20 trials cold, deep-sleep wake, keyboard
   power-cycle and 2 min out of range; 1,000 presses with a second S3 HID
   emulator and GPIO timestamps; p50/p95/max, lost/stuck keys, queue
   overflows.
6. **Coexistence and power:** 20 TLS runs with BLE off/idle/typing on a fixed
   AP channel; p95 regression; PPK2/INA219 for connected idle, typing,
   BLE+Wi-Fi and a 20-minute session.

Verification commands: `cd firmware && pio run -e cardputer-adv` (run:
1,664,221 B, 105,400 B static RAM). The probe was built with `pio run -e
baseline`, `-e bt`, `-e nimble` and `-e nimble2full` in a throwaway project
with the same platform, board, `qio_qspi` and partition table; that project
was deleted. A full app + BT link still needs a throwaway env in
`firmware/platformio.ini` and `pio run -e cardputer-adv-bt`; it was not run
here. Hardware results go to `docs/hardware.md` as a new dated section.

#### Decision

**BT-02: go for the source-neutral seam and pure HID mapping now; conditional
go for the NimBLE transport; no-go for Bluedroid A on the current table.**

- **Go now:** the seam and `hid_usage_to_key`/`hid_report_diff` tests have no
  BT dependency, are useful for PaperDIY buttons, and can ship even if BT is
  dropped.
- **Conditional go for B:** three keyboards pair and type; Just Works and one
  passkey/numeric flow pass; 20/20 cold reconnect p95 ≤5 s and 20/20
  deep-sleep p95 ≤3 s; zero lost/stuck keys in 1,000 presses; p95 radio/host
  ≤50 ms idle and ≤100 ms during TLS; app p95 ≤100 ms when not blocked; TLS
  p95 regression ≤20% and 20/20; BLE-on-TLS heap ≥8 KB free / ≥4 KB largest;
  battery within Q5. Any failure is a no-go with the measured reason.
- **A is not recommended:** it needs a 2.5 MB partition change (estimated
  84.6% before a full link) and its 15,616 B task-stack floor plus 20,892 B
  static delta make the TLS heap gate unlikely. Keep it as a fallback only if
  B's HOGP code proves impractical and the hardware heap probe passes.
- **No-go for any BT** if the pairing or heap gates fail. Do not ship a
  best-effort keyboard that loses keys or breaks TLS; the seam can still
  ship for buttons.

#### BT-02 implementation status (2026-11-20): review

**Status: review — seam and pure HID mapping merged; no BLE transport and no
hardware run.** The parts that do not depend on a radio are in the tree:

- `KeyEvent` carries `source`, `ch` and `mods`; matrix events keep `code/x/y`
  and leave `ch/mods` clear, while BLE/button events carry their own character
  and modifier mask (`ui/KeyInput.h`).
- `keyChar(e, shiftHeld)` applies Shift exactly once: the matrix path uses the
  live `gShift` and `cardputerCharAt`, a BLE/button event uses `e.mods`. Every
  screen-side text/shortcut call site uses it, including the review, both lesson
  quizzes, study, Outbox, on-device Wi-Fi and the `?` chord; the dev console's
  synthetic matrix lookup still reverses `cardputerCharAt` to find a key for
  injection.
- `updateModifier(e)` replaces the modifier globals from `mods` for a non-matrix
  event and keeps the matrix modifier-key path. `KeyRepeat` ignores non-matrix
  down/up, so a BLE keyboard's own repeat reports are not repeated again.
- `wk/HidKeyboard.{h,cpp}` is a pure, allocation-free mapping: boot-report
  parse, `hidUsageToKey`/`hidUsageChar`/`hidModifierBits`, stateful
  press/release/modifier diff, rollover and lost handling, and `releaseAll()`
  for a disconnect. It never emits `Key::None` on a normal diff.
- Sixteen native tests cover the mapping, diff order, rollover, disconnect and
  the repeat skip; the branch reports 453 tests. These are host tests only.

Not started: any BLE transport. No `esp_hidh`/NimBLE init, HID task, pairing
flow, bond storage, reconnect policy or Settings entry is linked or enabled;
the keyboard path still starts only at the TCA8418 matrix, and no code path
produces a `KeySource::Ble` event outside `wk/HidKeyboard` and its tests.
BT-01's conditional gates (three keyboards, pairing, reconnect, lost keys,
BLE-on-TLS heap, coexistence, power) and every hardware check are open. The
seam shipping does not claim a working Bluetooth keyboard.

---

## 2. PD-01/PD-02 — M5Stack PaperDIY board support

- **Priority:** P3 · **Size:** M (PD-01), L (PD-02)
- **Depends on:** none (PD-01); PD-01 (PD-02)
- **Touches:** new `firmware/src/board/BoardPaperDiy.cpp`;
  `firmware/src/board/Board.h`; `firmware/platformio.ini`;
  new `firmware/partitions/kanikey_16MB.csv`; `firmware/src/app/App.cpp`
  (`ensureCanvas()`, `present()`, rotation, loop cadence);
  `firmware/src/ui/Theme.h` (grayscale variant); `docs/hardware.md`. No code
  in this change.
- **Settings:** none; PD-03 adds button hints, PD-04 the sync/sleep cadence.

**Status (2026-11-20): PD-01 review — build only; PD-02 in progress.** The
`paperdiy`/`paperdiy-dev` environments, `partitions/kanikey_16MB.csv` (two
6 MiB app slots + 4032K LittleFS) and `board/BoardPaperDiy.cpp` are merged.
`paperdiy` selects `qio_opi` and M5GFX 0.3.0 and builds at 1,696,357 B of
6,291,456 B (27.0%); the Cardputer images moved by +28 B (rotation and
brightness now go through the board HAL). The two-button mapping is final
with PD-03 (§2.4; it replaces the temporary PD-01 map).
Nothing has run on a PaperDIY: panel autodetect via PM1, octal PSRAM size,
`Panel_EPD` buffers, buttons, SD/card detect, PM1 power key and VIN detection,
deep-sleep current, PSRAM retention in light sleep and the G3/G45/G46 strap
behaviour are all pending §2.6. `ensureCanvas()` still sizes the sprite to the
display (960×540 RGB332 in internal RAM); PD-02 moves the 240×135 canvas to
PSRAM with a 4× present.

### 2.1 Verified hardware

Sources: M5Stack PaperDIY Complete Kit/Assembly Kit pages, the PaperDIY
display/button/M5PM1 guides, and the M5GFX/M5Unified source.

| Item | Verified value | Source |
|---|---|---|
| Module | ESP32-S3-WROOM-1-N16R8 (ESP32-S3R8 die), LX7 dual-core, 240 MHz | [Complete Kit](https://docs.m5stack.com/en/core/PaperDIY_Complete_Kit) |
| Flash / PSRAM | 16 MB / 8 MB **octal** | same |
| Panel | ET047TC1, 4.7", 540×960 native, 960×540 used, 16-level grayscale | same |
| Panel interface | 8-bit parallel (DB0–DB7) | same; [CNX](https://www.cnx-software.com/2026/10/18/m5stack-paperdiy-kits-pair-an-esp32-s3-mcu-with-a-4-7-inch-e-paper-display) |
| Touch/backlight/keyboard | none / none / none | specs; `Panel_EPD`, no touch driver |
| Buttons | A = G4, B = G3; power on M5PM1 `PY_BTN` | [pin map](https://docs.m5stack.com/en/core/PaperDIY_Complete_Kit) |
| PMIC | M5PM1, SDA G41 / SCL G42, address 0x6E | [M5PM1 guide](https://docs.m5stack.com/en/arduino/paperdiy/m5pm1); [M5GFX](https://github.com/m5stack/M5GFX/blob/0.3.0/src/M5GFX.cpp) |
| Battery | 140 mAh, SH1.0-2P; charge status PM1 GPIO3 | [Complete Kit](https://docs.m5stack.com/en/core/PaperDIY_Complete_Kit); [M5Unified](https://github.com/m5stack/M5Unified/blob/0.2.26/src/utility/Power_Class.inl) |
| microSD | SCK G39, MOSI G38, MISO G40, CS G47, DET G48 | pin map |
| Expansion | 2× FPC 0.5 mm 12P; FPC-I2S + FPC-SPI, no I²C `verify` | [M5PM1 guide](https://docs.m5stack.com/en/arduino/paperdiy/m5pm1) |
| Power | 3.7 V battery, USB-C 5 V, 5 V pad; 40.95 mA operating, 161 mA peak | Complete Kit |
| Size | 120 × 68.2 × 6.3 mm, 43.1 g (Complete Kit) | same |

Pin map (M5Stack pin map + M5GFX PaperDIY autodetect):

| Signal | GPIO | Notes |
|---|---:|---|
| EPD DB0…DB7 | G6, G14, G7, G12, G9, G11, G8, G10 | 8-bit data |
| EPD XCL / XLE / XSTL | G16 / G15 / G13 | clock / latch / start pulse |
| EPD CKV / SPV | G18 / G17 | gate clock / source power |
| EPD BST_EN | G46 | boost enable; **strapping pin** `verify` |
| EPD OE | G45 | M5GFX `pin_oe`; not in M5 pin table; **strapping pin** `verify` |
| EPD power | PM1 GPIO2 | `EPD_PWR`, high in `M5.begin()` |
| PM1 I²C | SDA G41 / SCL G42 | address 0x6E |
| Button A / B | G4 / G3 | `M5.BtnA/B`; RTC-capable; G3 strapping/JTAG `verify` |
| microSD | SCK G39, MOSI G38, MISO G40, CS G47 | DET G48 |
| Power button | PM1 `PY_BTN` | not an ESP32 GPIO; deep-sleep wake `verify` |
| FPC-I2S / FPC-SPI | PM1 GPIO0 + G5 / PM1 GPIO1+GPIO4 | full pinout `verify` |

G45/G46 (EPD OE/BST_EN) and G3 (button B) are strapping pins. Boot 20 times
with the panel connected and the boost off/on, and record the boot mode.

### 2.2 Versions and the PlatformIO target

The PaperDIY display guide requires **M5Unified ≥ 0.2.26** and **M5GFX
≥ 0.2.33**, with M5Stack board package ≥ 3.3.10 in the Arduino IDE
([guide](https://docs.m5stack.com/en/arduino/paperdiy/display)). The repo
already meets the library minimums:

- `platformio.ini` pins M5Unified to the 0.2.26 commit `e125e73`; its
  `library.json` requires `M5GFX >= 0.2.31`.
- `pio pkg list -e cardputer-adv` resolves **M5GFX 0.3.0** (latest satisfying
  `>= 0.2.31`), which includes the PaperDIY autodetect and `Panel_EPD`.
- The board package version is an Arduino IDE concept; PlatformIO uses
  `board = esp32-s3-devkitc-1` and M5GFX autodetects `board_M5PaperDIY` by
  probing the M5PM1 at 0x6E. `verify` that M5Unified 0.2.26 + M5GFX 0.3.0 +
  Arduino core 2.0.17 initialises the panel; if not, a newer library pair or
  core 3.x is a separate decision and must keep the Cardputer build green.

Pin M5GFX explicitly to `0.3.0` for the PaperDIY env (and consider it for the
Cardputer) so a future 0.4.x cannot silently change the driver. Do not bump
M5Unified to 0.3.0 unless the probe needs it; 0.3.0 requires M5GFX ≥ 0.2.35
and re-opens Cardputer display/audio verification.

```ini
[env:paperdiy]
platform = espressif32@6.12.0
board = esp32-s3-devkitc-1
framework = arduino
board_upload.flash_size = 16MB
board_build.arduino.memory_type = qio_opi
board_build.partitions = partitions/kanikey_16MB.csv
board_build.filesystem = littlefs
build_flags =
    -std=gnu++17
    -DESP32S3
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
    -DCORE_DEBUG_LEVEL=1
    -Isrc
    -DTGC_BOARD_PAPERDIY
    -DKANIKEY_ENV=\"paperdiy\"
build_unflags = -std=gnu++11
lib_deps =
    M5Unified=https://github.com/m5stack/M5Unified#e125e73f28b6b83e621e48df432af426bb2ff935
    m5stack/M5GFX@0.3.0
```

`paperdiy-dev` extends it with `-DKANIKEY_DEV=1`. `qio_opi` should produce
`CONFIG_SPIRAM_MODE_OCT=y`; **verify by build** (`ESP.getPsramSize()` = 8 MB,
no M5GFX `need OPI-PSRAM enabled`). The 16 MB table keeps the labels `app0`,
`app1` and `spiffs` because OTA addresses slots by label; two ~6 MB app slots
plus ~3 MB LittleFS, NVS and otadata is a reasonable first table. The
Cardputer table is unchanged.

`M5.begin()` with `cfg.clear_display = false` avoids a full white flash before
the first frame (the M5Stack example does this). Set rotation 1 for 960×540;
M5GFX uses `offset_rotation = 3`, rotation 1. The app currently hard-codes
`gDisp->setRotation(0)` in `App::setup()` for the Cardputer; PD-01 must move
rotation into the board so PaperDIY is not portrait. `fixedLandscape()` should
return true.

### 2.3 Display path

`Panel_EPD` is not a normal framebuffer. It allocates in **PSRAM** and runs a
background update task:

- `_buf`: `panel_w * panel_h / 2` = **259,200 B** (4bpp current pixels).
- `_step_framebuf`: `(memory_w * memory_h / 2) * 2 * sizeof(uint16_t)` =
  **1,036,800 B** (2 bytes/pixel).
- `_dma_bufs[2]`: small internal DMA buffers.
- If either PSRAM allocation fails, the panel is unavailable. Octal PSRAM is
  a hard requirement.

These are code-derived sizes from
[`Panel_EPD.inl`](https://github.com/m5stack/M5GFX/blob/0.3.0/src/lgfx/v1/platforms/esp32/Panel_EPD.inl),
not measurements. The 4× path adds a 32.4 KB 240×135 canvas; the native path
adds a 960×540 8-bit canvas (518,400 B, `setPsram(true)`), for ~1.82 MB total
PSRAM. The TLS rule still applies: internal RAM, not PSRAM, must hold the
contiguous block.

Refresh modes ([M5Stack guide](https://docs.m5stack.com/en/arduino/paperdiy/display)):

| Mode | Use | Trade-off |
|---|---|---|
| `epd_quality` | images, grayscale, first frame, full refresh | slowest, best ghosting control |
| `epd_text` | text screens | faster, keeps grays; recommended for text |
| `epd_fast` | navigation | faster, weaker ghosting control |
| `epd_fastest` | fastest navigation | fastest, weakest ghosting control |

Faster modes have lower quality and weaker ghosting control; a full
`epd_quality` refresh is recommended after several partial fast refreshes.
Timings are content-dependent and must be measured.

**4× bring-up path.** The panel is exactly 4× the 240×135 UI. Keep the
existing layout, draw into a 240×135 `grayscale_8bit` canvas (so the glyph
pack's 4bpp alpha blends into 16 grays without an RGB332 detour), and add a
grayscale `Theme` (black, white, 2–3 grays; no colour-only meaning). Present
with `gCanvas->pushRotateZoom(gDisp, 0, 0, 0, 4.0f, 4.0f)` (or
`pushImageRotateZoom`); confirm the overload and nearest-neighbour 4×. A
full-screen 4× push writes all 518,400 destination pixels, so `Panel_EPD`
sees a full-screen dirty rectangle: **partial updates are ineffective in the
4× path**. Use `epd_quality` for the first frame/transitions and `epd_fastest`
for navigation if the measured time allows; coalesce presents. The pack's
64/32/20 px glyphs become 256/128/80 px (readable but blocky). A native
960×540 layout needs new glyph sizes (e.g. 96/64/40 px) in a `KKGP` v2 pack
and new layout constants — a separate go/no-go.

**Native path (if 4× is rejected).** Draw into a 960×540 `grayscale_8bit`
canvas (518,400 B, PSRAM) and use the panel's dirty-rect tracking for partial
refreshes. Do not start this until the 4× measurements say it is needed.

**Present policy.** The current loop draws when `gDirty` and 16 ms pass, and
screens call `present()` directly. On e-paper: coalesce presents (push after
~150 ms stable, at most once per ~500 ms), never while `displayBusy()`; skip
periodic clock/battery presents; `waitDisplay()` before sleep, a full refresh,
or an `epd_quality` change; count partials and run a full refresh every N
(start N = 10, tune with ghosting photos). A 4× full-screen push is not a
partial update; sub-second navigation needs the native path or a cropped
240×135-region push.

### 2.4 No-keyboard UI and read-only scope

`Board.h` has `hasKeyboard()`/`nextKeyEvent()`; today `hasKeyboard()` is only
logged and the no-keyboard branch returns no events. PaperDIY needs:

- `hasKeyboard()` false; `nextKeyEvent()` still delivers events from the two
  buttons (plus a dev injection for bring-up). The board-owned queue reads
  `M5.BtnA/B` in `update()` and synthesizes `KeyEvent`s. PD-03 owns the final
  mapping; PD-01/02 need at least the dev injection.
- `fixedLandscape()` true, rotation 1; UI width/height 240×135 in the 4× path.
- Proposed map (PD-03 decides): A short = Next/Down; A long = Confirm; B
  short = Back/Esc; B long = Prev/Up. Hint bar names `A`/`B`. `kHoldMs` 600 ms.
  G3/G4 are RTC-capable, so `ext1` wake is possible (`verify`).
- Read-only: Home, lesson list/card, item detail, Stats, Sync, Outbox, About,
  Settings (settings write NVS; no review/lesson POST/PUT). No reviews, quiz,
  `PUT /start` or text entry. The lesson card can be viewed, not started.
  The setup portal is browser-based, so Wi-Fi/token entry works without a
  keyboard; SETUP-09 is Cardputer-only.
- Full reviews need a text source: BT-01…BT-04, or a wired keyboard on the FPC
  expansion (I2S/SPI; no I²C `verify`). A USB keyboard needs host mode/power;
  the roadmap's wired option is FPC, not USB — `verify` the USB-C wiring.
- On PaperDIY, a BLE keyboard requiring the **host to type a passkey** is
  unusable without the built-in keyboard. BT must support Just Works or the
  host-displays/numeric-comparison flows for PaperDIY; this is a BT-03
  constraint.

**PD-03 status: review (2026-11-20: code; hardware pending).** The two-button
mapping and the read-only scope are in the firmware and the native tests pass;
no PaperDIY has run them yet (button feel, hold timing on the real switches,
and the Home strip at 1 m are §2.6 items).

**Mapping** (`wk/ButtonNav.h`, fed by `board/BoardPaperDiy.cpp` once per loop):

| Input | Key sent | Action | Why |
|---|---|---|---|
| A tap | Down | Next (row, option, page) | the common step; one finger for lists |
| A hold 600 ms | Enter | Confirm (open, choose) | deliberate, so scrolling does not open items |
| B tap | Esc | Back (up a level, close) | the quick exit |
| B hold 600 ms | Up | Prev (row, option, page) | the less frequent backward step |

A hold sends one key on the threshold and never repeats; the release after a
hold sends nothing. A press of both buttons together sends nothing until both
are up. The same press-and-release pair a keyboard key makes goes into the
queue, so every screen sees the same `KeyEvent`s (`source = Button`, which
`KeyRepeat` never repeats).

**Scope.** `buttonsOnly()` (`board/Board.h`) is true on PaperDIY and false on
the Cardputer ADV (compile-time, so the Cardputer image drops the branches; the
M5 image asks the board). When it is true:

- Home is a focus ring over Lessons, Stats, Sync and Settings (A tap moves, A
  hold opens, B opens Settings). Reviews are shown as a count, not a target.
- Lessons: the list opens the card of the item under the cursor (no Space);
  the card pages with A tap / B hold. Quizzes and the `PUT /start` are refused
  with the notice `Needs a keyboard` (the card stays put; any button closes it).
- Reviews (`startReview()`, every caller), the study menu, and Wi-Fi entry
  (`openWifiSetup()`, the password step) are refused the same way. The boot
  Wi-Fi path opens the Setup portal instead.
- Settings: every row except Wi-Fi and Firmware works (Enter cycles a value or
  runs an action). Setup portal, Sync now, Outbox, clear cache, sign out, reset
  and About stay; firmware shows `Firmware updates: Cardputer only`.
- Stats pages with A tap (next) and B hold (previous). Sync and Outbox use the
  same keys as on a keyboard.

**Hints.** Every hint on a reachable PaperDIY screen names buttons through
`app/ButtonHint.h` (`pickHint(keys, buttons)`): Home `A: next  hold A: open
B: settings`, lessons `A: next  hold B: prev  hold A: open`, notices
`hold A: sync  B: home`, Boot `hold A: setup`. Long lines use `fitHint()` and
its shorter fallbacks; the layout stays 240×135 (`docs/ux.md` §3.5).

**Cardputer.** The keyboard hints, keys and paths are unchanged; the button
strings are not in its image. The Cardputer ADV `firmware.bin` is 1,665,008 B
against 1,664,545 B before PD-03 (+463 B; no button text is in it, so the
difference is code shape in shared functions, not measured per function).

### 2.5 Power and sleep

M5PM1 levels: L0 (battery/PM1 + FPC-I2S), L1 standby, L2/L3A (ESP32-S3,
microSD, user buttons, FPC-SPI), L3B (e-paper power, FPC control, charge
status). `M5.begin()` sets PM1 GPIO2 high for `EPD_PWR` and PM1 GPIO3 as the
charge-status input ([M5PM1 guide](https://docs.m5stack.com/en/arduino/paperdiy/m5pm1);
[M5Unified](https://github.com/m5stack/M5Unified/blob/0.2.26/src/utility/Power_Class.inl)).

Compare two strategies:

1. **ESP32 deep sleep:** `ext1` on G3/G4 plus a timer, `setPowerSave(true)` /
   `setSleep(true)` to turn off the EPD boost, PM1 stays in L2. Fast wake, full
   `setup()`, image retained. The power button is on the PM1; `verify` whether
   it wakes the ESP32 or only power-cycles.
2. **PM1 shutdown:** `pm1.shutdown()` cuts 3V3_L2 and the ESP32; the PM1 timer
   or power button brings it back. Lowest current, full cold boot. The M5PM1
   guide warns not to cut e-paper power before its state is saved.

The 140 mAh cell is small: at 40.95 mA the upper bound is ~3.4 h before
Wi-Fi/TLS and refresh peaks. Treat it as a desk companion: USB for long
sessions, timer/button wake, sync, refresh only on change, sleep. PD-04
chooses the strategy; PD-01 needs a working deep sleep and wake path.

### 2.6 Spike questions and measurements

**PD-01 — board/build.** (1) Builds with `qio_opi`, 16 MB flash, 8 MB PSRAM?
(2) `board_M5PaperDIY` detected and 960×540 panel initialised, no
`need OPI-PSRAM enabled`? (3) SD mounts and the pack opens (Cardputer CRC
~3.6 s; measure here)? (4) Buttons read and `ext1` wakes; does the power
button wake or power-cycle? (5) Battery/charge status from PM1? (6) Flash,
static RAM, internal heap/largest and PSRAM at boot, after `M5.begin()`, after
the canvas, before/after TLS?

**PD-02 — rendering/refresh.** (1) Refresh time per mode for a full 960×540
frame and a 240×135-equivalent region: `t0 = micros(); push...;
gDisp->waitDisplay(); t1 = micros();` 20 runs, min/median/max. Do not time
only `pushSprite()` (it queues). (2) Ghosting after 1/5/10/20/50 partial
updates of a text screen, photos, then one `epd_quality` full refresh; record
the unacceptable N. (3) 4× quality of Home/lesson/Stats at 1 m; glyphs and
grays. (4) 4× cost vs native full frame; is partial navigation ineffective?
(5) Present policy: 150 ms stable + 500 ms floor; does clock/battery trigger
a refresh; presents per 10-key burst? (6) Native layout go/no-go: scope a
960×540 layout + `KKGP` v2 96/64/40 px glyphs only if 4× fails.

| Metric | Method | Proposed pass |
|---|---|---|
| Flash / static RAM | release size | fits 16 MB table with OTA |
| PSRAM free after display + canvas | `MALLOC_CAP_SPIRAM` | ≥ 5 MB |
| Internal heap / largest | `MALLOC_CAP_INTERNAL` | ≥ 60 KB / ≥ 40 KB |
| Boot to first frame | `millis()` at first `present()` | ≤ 10 s |
| SD mount + pack open | existing log lines | same order as Cardputer |
| `epd_quality` / `epd_text` / `epd_fastest` full frame | `waitDisplay()` | ≤ 3 s / ≤ 2 s / ≤ 1.5 s |
| 240×135-equivalent region | same | ≤ 500 ms |
| Ghosting after N partials | photos | N = 10 acceptable with full refresh |
| Deep-sleep current | PPK2/INA219, 10 min | ≤ 1 mA |
| Wake to first frame | 20 trials | ≤ 5 s |
| Refresh energy | PPK2/INA219 per frame | report mAh |

All thresholds are proposed; none is a measurement.

### 2.7 Go/no-go

**PD-01:** builds with 16 MB + octal PSRAM; `board_M5PaperDIY`; 960×540; SD;
both buttons; PM1 battery/charging; deep sleep + wake; internal heap ≥ 60 KB /
largest ≥ 40 KB after display + canvas; PSRAM free ≥ 5 MB; boot to first frame
≤ 10 s. If 0.2.26 + 0.3.0 + core 2.0.17 cannot init the panel, a newer
library pair or core 3.x is a separate decision; the Cardputer stays green.

**PD-02:** 4× renders Home/lesson/item/Stats legibly at 1 m, no colour-only
meaning; refresh gates as above; presents coalesced, no per-key or periodic
clock refresh, full refresh every N, `waitDisplay()` before sleep/mode change.
If 4× fails, native 960×540 is a separate go/no-go with `KKGP` v2; do not
ship a half-native layout. The first release stays read-only; full reviews
wait for BT-01…BT-04.

### 2.8 PD risks

- **E-paper speed/ghosting.** 4× writes the full screen and makes partials
  ineffective; native is more work. Batch/debounce if `epd_fastest` is slow.
- **PSRAM.** `Panel_EPD` hard-fails without octal PSRAM; the `qio_opi` build
  is a gate.
- **Strapping pins.** G45/G46/G3; boot-test with the panel and boost on/off.
- **Power/wake.** The power button is on the PM1; wake vs power-cycle is
  unverified. 140 mAh makes sync cadence a product decision.
- **M5GFX drift.** 0.3.0 is resolved, not pinned; pin it.
- **Native scope.** 4× is a bring-up layout; native 960×540 + pack v2 is a
  separate task.

---

## 3. Open questions for the owner

1. Which three BLE keyboards and which Classic-only keyboard are available?
   Confirm transports before the run.
2. Is the test Cardputer ADV 1500 mAh (current spec) or an early 1750 mAh?
3. Accept a partition-table change (smaller LittleFS, larger app slots) if
   Bluedroid is chosen?
4. Is 4× acceptable for the first PaperDIY release, or is native 960×540
   required before PD-03?
5. Does the PaperDIY power button wake the ESP32, or only power-cycle through
   the PM1? This decides PD-04.
6. Does the FPC expansion expose enough SPI/I2S for a wired keyboard, and is
   I²C absent? `verify` from the schematic.
7. Should BLE stay connected during reviews/lessons, or only during a session
   (BT-04)? This changes battery/coexistence requirements.
8. Does the first PaperDIY release need OTA, or is USB flashing enough? The
   partition table depends on it.

## 4. Key sources

The inline links are the citations; these are the primary pages.

- PaperDIY Complete Kit + pin map: https://docs.m5stack.com/en/core/PaperDIY_Complete_Kit
- PaperDIY display guide (modes, library minimums): https://docs.m5stack.com/en/arduino/paperdiy/display
- PaperDIY M5PM1 guide (power levels, EPD_PWR): https://docs.m5stack.com/en/arduino/paperdiy/m5pm1
- Cardputer-Adv specs (1500 mAh, currents): https://docs.m5stack.com/en/core/Cardputer-Adv
- M5GFX PaperDIY autodetect: https://github.com/m5stack/M5GFX/blob/0.3.0/src/M5GFX.cpp
- M5Unified PaperDIY power: https://github.com/m5stack/M5Unified/blob/0.2.26/src/utility/Power_Class.inl
- ESP-IDF S3 BLE overview: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/ble/overview.html
- ESP-IDF `ble_hidh.c`: https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_hid/src/ble_hidh.c
- ESP-IDF `esp_hid_host` example: https://github.com/espressif/esp-idf/tree/v4.4.7/examples/bluetooth/esp_hid_host
- NimBLE-Arduino: https://github.com/h2zero/NimBLE-Arduino
- Third-party S3 Bluedroid/NimBLE size (indicative): https://benign.host/blog/esp32-ble-flash-budget/

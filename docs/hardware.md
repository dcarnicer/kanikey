# Hardware log

Measurements from the actual M5Stack Cardputer ADV. This file is a log, not a
promise: numbers change as the firmware grows.

## 2026-10-25 — M0 bring-up

Build: `cardputer-adv`, app-only USB flash of the M0 firmware.
App: display, keyboard controller, microSD, heap report, Wi-Fi, TLS
`GET /v2/user`.

### Device

| | |
|---|---|
| Board | M5Stack Cardputer ADV |
| MCU | ESP32-S3FN8, 8 MB flash |
| PSRAM | 0 MB (none) |
| USB | `/dev/cu.usbmodem1101`, USB Serial/JTAG |
| microSD | 30,528 MB FAT32, mounted in ~6 ms |

### Firmware size

| | |
|---|---|
| Flash used | 1,632,217 B of 1,835,008 B (88.9%) |
| Static RAM | 50,204 B of 327,680 B (15.3%) |

### Heap

All values are internal heap, bytes.

| Point | Free | Largest | Minimum free |
|---|---:|---:|---:|
| After boot, before SD | 256,136 | 225,268 | — |
| After SD mount | 240,768 | 208,884 | — |
| Before TLS | 186,808 | 176,116 | 182,080 |
| After TLS `/user` | 183,096 | 106,484 | 109,208 |
| After `/summary` | 178,720 | 106,484 | 109,184 |
| Idle after sync | ~186,300 | — | — |

The 64.8 KB full-screen canvas is now allocated. It costs the expected ~65 KB
and leaves 178.7 KB free / 106.5 KB largest after `/summary`. That is above the
design target of 80 KB free in a review, but the subject cache and the answer
engine still have to fit; batches of subjects must stay small until the
streaming subject parser exists.

TLS cost in this build is small in free bytes (about 3 KB) but it lowers the
largest block by ~70 KB because mbedTLS allocates and frees handshake buffers.
The minimum free stayed at ~178 KB, which is the number that matters for a
later allocation.

### Wi-Fi and API

- Saved credentials in NVS namespace `net` (shared with Tangocho during
  development).
- Join: 3.8–3.9 s, RSSI −51 to −55 dBm.
- SNTP time sync: ~1 s after the join.
- TLS handshake + `GET /v2/user`: ~2.0 s, 719 body bytes, `200 OK`.
- TLS handshake + `GET /summary`: ~2.1 s, 5,094 body bytes, `200 OK`.
- Both bodies parse; the Home screen shows the account level, due reviews,
  lesson count, next review and sync time.
- The request uses **HTTP/1.0** so WaniKani sends a plain body instead of
  chunked transfer encoding; no chunk decoder is needed.
- The token is not logged.
- The first join after a cold boot sometimes stalls in `WL_DISCONNECTED` for
  the whole 15 s attempt; a second attempt with a clean radio connects in
  ~3.9 s. The dev helper now does two attempts. Production will use
  Tangocho-style scan/pin/retry logic.

### Home screen

- After the summary parses, the Home screen shows the level, username, due
  reviews, lessons, next review and last sync time.
- `Tab` toggles the keyboard/debug screen.
- The debug screen renders a test kanji with M5GFX's `efontJA_16`; that font
  adds ~468 KB of flash and leaves ~203 KB for the review UI. It is a bring-up
  fallback, not the final glyph-pack path.
- The Home layout was rebalanced after a live screenshot: 16 px brand, 32 px
  counters, 8 px labels, 12 px lower rows. `tools/shot.py` reads the 240×135
  canvas over USB and writes a PNG.
- A physical key-press pass still needs to be recorded here.

### Keyboard

- `TCA8418 ready` on boot.
- 26 native tests cover the grid mapping, characters, modifiers, answer
  checker, romaji IME, review queue, JSON, ISO 8601, user and summary parsing.
- A physical key-press pass still needs to be recorded here.

### Serial

The ESP32-S3 USB Serial/JTAG delivers data only with **DTR and RTS deasserted**.
A raw `pio device monitor` can therefore look silent. Use:

```sh
uv run --with pyserial python3 tools/device.py monitor
```

### Benign boot messages

```
E (221) esp_core_dump_flash: No core dump partition found!
[   707][E][esp32-hal-cpu.c:110] addApbChangeCallback(): duplicate func=...
```

- No coredump partition is configured; the app does not use one yet.
- The APB callback warning comes from the M5Unified/SD bring-up path and does
  not affect the run.

### Next

- Assignments + subjects cache.
- Real review session screen with the typed answer flow.
- Canvas + renderer, then measure the real UI budget.

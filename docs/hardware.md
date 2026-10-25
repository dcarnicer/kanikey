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
| Flash used | 1,717,673 B of 1,835,008 B (93.6%) |
| Static RAM | 51,664 B of 327,680 B (15.8%) |

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

### Review session (M1)

- `/v2/assignments?subject_ids=...` for 20 summary IDs: 9.8 KB, ~2 s.
- Subjects fetched in four batches of five: 12.7–17.4 KB each, ~1.9–2.4 s.
- The canvas is released before the batch and before each review POST; this is
  required: with the canvas allocated, the largest free block dropped to
  ~20 KB and the TLS handshake failed with `SSL - Memory allocation failed`.
- With the canvas released, the batch and the POST fit. After the 20 subjects,
  the retained batch + canvas leaves ~171 KB free (largest ~19–40 KB).
- The typed review screen uses `efontJA_16`; a vocabulary prompt is drawn at
  2× (32 px), a single kanji at 3× (48 px).
- `POST /v2/reviews` returns **201** with the updated resources; the client
  accepts any 2xx. One completed item was submitted and the session moved on.
- The app still lacks a persistent outbox: if the immediate POST fails, that
  review is lost. This is the next reliability item.

### Persistent outbox (M3 start)

- Completed reviews are written to `/state/outbox.bin` **before** the POST.
- File format: `KKOB` header (version 1, count, CRC-32) + 24-byte records;
  atomic `.tmp` write and a `.bak` copy.
- A real review: queued (`seq 1`), canvas released for TLS, `POST /reviews`
  returned **201**, record removed, `o` showed an empty outbox.
- A synthetic review (assignment `999999999`): queued, survived a reboot
  (`loaded 1 pending`), flushed on the next boot, got **422**, stayed in the
  file as `Rejected` (`need attention`), and was cleared manually.
- **Offline end to end passed:** Wi-Fi off, a real review completed, outbox
  `Pending`; reboot loaded 1 pending, flushed it, `POST /reviews` returned 201,
  and the outbox was empty. This is the key reliability path.
- Ambiguous requests (timeout/5xx after the request was sent) are marked
  `Ambiguous` and reconciled with `GET /assignments?ids=...` before retrying.
  That reconciliation has unit-level coverage but not a hardware timeout test.
- Rejected records stay until a user-facing way to dismiss them exists; for
  now the dev command `z` clears the outbox.
- Flash headroom is now ~117 KB; the efont fallback is the next thing to
  replace when the visual pack lands.
- Home shows `Outbox: N pending` or `N need attention` below the sync time.
- Power: dim after 30 s, blank after 60 s, deep sleep after 2 min only when no
  review is active and the device is on battery. A key wakes the panel; the
  first key on a blank screen only wakes it.

### Next

- Offline end-to-end test and a way to dismiss rejected outbox items.
- Lessons.
- Visual pack (replace the efont fallback).
- Real renderer/UI font instead of the M5GFX text calls.

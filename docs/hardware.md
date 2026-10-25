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
| Flash used | 1,126,621 B of 1,835,008 B (61.4%) |
| Static RAM | 50,036 B of 327,680 B (15.3%) |

### Heap

All values are internal heap, bytes.

| Point | Free | Largest | Minimum free |
|---|---:|---:|---:|
| After boot, before SD | 322,548 | 286,708 | — |
| After SD mount | 307,084 | 270,324 | — |
| Before TLS | 253,772 | 241,652 | 248,676 |
| After TLS `/user` | 250,764 | 172,020 | 177,612 |
| Idle after sync | ~253,300 | — | — |

The current M0 draws directly to the panel and does **not** allocate the
planned 64.8 KB full-screen canvas. With the canvas, expect roughly 186 KB free
after TLS and ~107 KB largest. That is still above the design target, but the
real number must be measured again when the canvas and the glyph cache exist.

TLS cost in this build is small in free bytes (about 3 KB) but it lowers the
largest block by ~70 KB because mbedTLS allocates and frees handshake buffers.
The minimum free stayed at ~178 KB, which is the number that matters for a
later allocation.

### Wi-Fi and API

- Saved credentials in NVS namespace `net` (shared with Tangocho during
  development).
- Join: 3.8 s, RSSI −54 dBm.
- TLS handshake + `GET /v2/user`: 1.8–2.0 s.
- Response: `HTTP/1.1 200 OK`, 1,785 bytes.
- Username and level parsed from the body; the token is not logged.
- First join during bring-up needed a retry after 15 s; the 30 s budget now
  connects on the first attempt. Production will use Tangocho-style
  scan/pin/retry logic.

### Keyboard

- `TCA8418 ready` on boot.
- 21 native tests cover the grid mapping, characters, modifiers, answer
  checker, romaji IME and review queue.
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

- `/summary` + assignments + subjects cache.
- First Home screen with real due counts.
- Canvas + renderer, then measure the real UI budget.

# Memory headroom

Status: **investigation, 2026-11-20.** Four read-only audits (static RAM, runtime
heap, TLS/Wi-Fi, peripherals) plus measurements on the owner's Cardputer ADV.
MEM-01 phase 1 (the Arduino 3 migration) is merged and owner-verified (§2.2). MEM-02 (custom core) is built on the host, hardware pending (§2.3); nothing else is implemented.
Figures are marked **M** (measured on the device with the dev `hh`/`h` commands
or the boot/HTTP log lines), **B** (from the build/map of `9a95e38`) or **E**
(estimate from code and struct sizes).

Owner decisions (2026-11-20): Arduino 3 is fine (M5Unified runs on it with the
Cardputer ADV in the owner's tangocho project); breaking OTA compatibility with
existing installs is fine, the app has no users yet; update the libraries.

## 1. Why this matters

Three things need large blocks of internal RAM (no PSRAM on the Cardputer ADV):

| Need | Size | Notes |
|---|---:|---|
| TLS connection | 2 × ~16.7 KB contiguous + handshake temporaries | mbedTLS is built with 16 KB records both ways (`CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384`, no asymmetric lengths) in both the 2.0.17 and the 3.3.x prebuilt libs (B) |
| MP3 playback | 31,616 B (largest piece 16,240) | minimp3 state 6,672, scratch 16,240, window 4,096, 2 × 2,304 PCM (M) |
| Display canvas | 32,404 B, DMA-capable | 240×135 RGB332 sprite; already released for TLS and lent to audio (M/B) |

What failed on 2026-11-20 because of it: the Home sync (`-32512` SSL alloc at
98 KB free / 30.7 KB largest), R in reviews (37 KB free / 21.5 KB largest), the
Home clip drain (127 KB free / 41 KB largest). All were patched case by case
(`880b1be`, `e1277ad`, `9a95e38`); this spec is about getting real headroom.

## 2. The budget today (Arduino 2.0.17, `9a95e38`)

| Point | Free | Largest | Source |
|---|---:|---:|---|
| Static DRAM (`.data` + `.bss`) | 108,880 B used | — | B |
| Boot, before SD | ~200 KB | 164 KB | M |
| SD mounted (`SD.begin(…, 4)`) | −23,344 B | | M |
| Home after boot, no Wi-Fi | 160,620 B | | M |
| + review batch (20 assignments, 49 subjects) and session | 102,780 B (−57.8 KB) | | M |
| In that review, feedback frame | 94,916 B | 69,620 B | M |
| Same review, Wi-Fi joined (idle) | 42,104 B (−52.8 KB) | 31,732 B | M |
| After `WiFi.mode(WIFI_OFF)` | 78,428 B | 32,756 B | M |

So in a review with Wi-Fi up there is no room for TLS or audio without
borrowing the canvas. Wi-Fi off gives back 36 KB of free memory but not a
large block (the netifs, event loop and tcpip task stay, E/B).

### 2.2 Arduino 3.3.11 baseline (MEM-01, prebuilt libs, M)

| Point | 2.0.17 | 3.3.11 |
|---|---:|---:|
| Static DRAM (B) | 108,880 B | 110,880 B |
| Release image (B) | 1,669,264 B (53.1%) | 1,786,927 B (56.8%) |
| Boot heap, before SD | ~200 KB free / 164 KB largest | 179,512 / 122,868 |
| SD mount | −23,344 B | −26,104 B |
| Home after the boot sync | ~100–106 KB | 80,972 / 31,732 (61% fragmented) |
| First TLS request (canvas released) | — | before 114,572 / 38,900; after 71,816 / 31,732 |
| Review feedback, Wi-Fi up, clip playing | — | 31,136 / 17,396 after the 31.6 KB alloc (no canvas borrow needed) |

The prebuilt 3.3.x core uses about 20 KB more heap than 2.0.17, so MEM-02 is
what pays for the move.

### 2.4 MEM-02 on the device (2026-11-20, dev image, M)

| Point | MEM-01 (prebuilt core) | MEM-02 (tuned core) |
|---|---:|---:|
| Boot heap, before SD | 179,512 free / 122,868 largest | 186,160 / 131,060 |
| SD mount | −26,104 B | −8,184 B |
| One TLS connection (first request, before → after) | 114,572 → 71,816 (−42.8 KB) | 108,588 → 78,584 (−30.0 KB) |
| Home after the boot sync | 80,972 / 31,732 | **108,008 / 61,428** |
| Review question, batch from the card, Wi-Fi up | ~37 KB / 21.5 KB | **49,180 / 23,540** |
| Home after End session and the clip drain | — | 107,268 / 59,380 |

The runtime gain is about 27 KB on Home and 12 KB in a review, and the largest
block on Home doubles. A clip (39.8 KB free, 20.3 KB block) now fits in a review
with Wi-Fi up without borrowing the canvas. The review batch (58 KB, Track C)
is the next lever: the largest block in a review is still under 24 KB.

### 2.3 MEM-02 custom core (2026-11-20, host measurements, B)

Setup: `cardputer-adv` on pioarduino 55.03.311 (Arduino 3.3.11, IDF 5.5.5), PlatformIO
Core 6.1.19 (the CI pin). `[mem_tuned]` in `firmware/platformio.ini`. The effective
values were read from the generated `firmware/sdkconfig.cardputer-adv`, and each
was checked against its default.

**Effective settings** (one line each: why)

| Setting | Effective | Why |
|---|---|---|
| `CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN=y`, `SSL_IN_CONTENT_LEN=16384`, `SSL_OUT_CONTENT_LEN=4096` | yes | Output records are small requests; input keeps 16 KB servers may send. IDF estimate −12.3 KB per connection (E, runtime). |
| `CONFIG_WL_SECTOR_SIZE_512=y`, `CONFIG_WL_SECTOR_SIZE_4096=n` | yes | `WL_SECTOR_SIZE` is a Kconfig choice; `CONFIG_WL_SECTOR_SIZE=512` alone stayed at 4096 (the base template's `_4096=y` line came after the custom lines). `ffconf.h` takes `FF_MAX_SS = max(512, WL)`, so this fixes the FatFs buffers at 512 (per `FIL`, FATFS: −3.5 KB each, E, runtime). Needs `-D_MAX_SS=512` (below). No FFat or wear-levelled partition is used. |
| `CONFIG_ESP_WIFI_IRAM_OPT=n`, `RX_IRAM_OPT=n`, `EXTRA_IRAM_OPT=n` | yes | Wrapper hot paths go to flash; the freed SRAM goes to the heap. Only 1.6 KB moved (see analysis); the Wi-Fi blobs keep their IRAM. |
| `CONFIG_ESP_WIFI_CSI_ENABLED=n`, `CONFIG_ESP_WIFI_FTM_ENABLE=n` | yes | Unused. Arduino's `initiateFTM()` is not guarded by the option, but the link succeeded. |
| `CONFIG_ESP_WIFI_MGMT_SBUF_NUM=16` | yes | Management frame buffers, 32 → 16. Not overridden by Arduino. |
| `CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=16` | **not applied, left out** | Arduino overrides `dynamic_rx_buf_num = 32` (and `dynamic_tx_buf_num = 32`) in `WiFiGeneric.cpp:280-287` at `wifiLowLevelInit`. The Kconfig value has no effect. A framework patch would be needed; not done. |
| `CONFIG_ESP_TIMER_TASK_STACK_SIZE=4096`, `CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH=2560` | yes | The same high-water measurements as the paperdiy reader (esp_timer under 1 KB of 8 KB; timer service under 1 KB of 4 KB). These are runtime heap, not static. |
| `CONFIG_ARDUINO_SELECTIVE_COMPILATION=y` + 23 wrappers `=n` | yes | Disabled only where `firmware/src` and the M5Unified/M5GFX sources (not examples) do not include the library: ArduinoOTA, BLE, BluetoothSerial, DNSServer, EEPROM, ESPmDNS, ESP_SR, ESP_Video, Ethernet, FFat, HTTPClient, Insights, Matter, NetBIOS, OpenThread, PPP, RainMaker, SD_MMC, SPIFFS, SimpleBLE, Ticker, WiFiProv, Zigbee. Kept: SD, LittleFS, FS, WiFi, WiFiClientSecure, WebServer, Update, Preferences, SPI, Wire (M5GFX's ESP32 platform includes SPI and Wire). |
| `CONFIG_BT_ENABLED=y` | yes | BT-01 plans a BLE keyboard. The Arduino BLE and SimpleBLE wrappers are off; NimBLE-Arduino does not need them. |
| `custom_component_remove`: insights, rainmaker, diagnostics, diag_data_store, cbor, zboss-lib, zigbee-lib, esp-sr | removed | Cloud and Zigbee components the framework lists in `idf_component.yml`. esp-sr is S3-only and unused. |
| Core dumps, IPv4 | unchanged | Prebuilt values (`ESP_COREDUMP_ENABLE=y`). |
| `CONFIG_LWIP_IPV6` (kept `y`), `CONFIG_MBEDTLS_DYNAMIC_BUFFER` (kept unset) | optional, not enabled | IPv6 off: about 1.3 KB (E, not measured). Dynamic buffers allocate the SSL record buffers only while a connection is active, so they reclaim the full record buffers between connections. Known link trouble on pioarduino (§3, MEM-02). Commented in the ini. |

**Build settings that are not sdkconfig** (each needed for this build):
- `-D_MAX_SS=512` (cardputer envs): with a fixed 512-byte FatFs there is no
  `FATFS::ssize` field, and Arduino's `SD.cpp` reads it unless `_MAX_SS` is 512.
- `build_src_flags = -Isrc` instead of `build_flags`: as a build flag, `-Isrc` reached
  the managed libsodium component, where `#include "version.h"` matched
  `src/Version.h` on macOS's case-insensitive filesystem.
- `pre:scripts/patch_pioarduino_cache.py` (copied from paperdiy-reader; its cache
  check is what keeps the global core from being reused for the wrong env).
- PlatformIO Core **6.1.19**. PlatformIO 6.2.0 fails at the link with
  `No module named 'SCons.Tool.FortranCommon'`. Core 6.2 loads its own SCons from a
  `site-packages` path, and the platform's `penv_setup.py` removes that path
  from `sys.path`. Not a CMake issue: the bundled CMake 4.0.3 configured fine.

**Numbers** (`cardputer-adv` release, MEM-01 prebuilt → MEM-02):

| | MEM-01 | MEM-02 | Δ |
|---|---:|---:|---:|
| Flash image (B) | 1,786,927 | 1,774,559 | −12,368 |
| Static RAM, `.data`+`.bss` (B) | 110,880 | 110,668 | −212 |
| `.iram0.text` (B) | 81,799 | 80,187 | −1,612 |
| `.dram0.dummy` (IRAM mirror, B) | 66,560 | 65,024 | −1,536 |
| `.dram0.data` / `.bss` (B) | 23,528 / 87,352 | 23,436 / 87,232 | −92 / −120 |
| Heap start, offset from `0x3FC88000` (B) | 177,440 | 175,696 | −1,744 |
| Flash `.flash.text` (B) | 1,387,008 | 1,377,180 | −9,828 |

Other envs: `cardputer-adv-dev` 1,864,643 B flash, 110,988 B static RAM; `-badapi`
1,857,731 B, 78,204 B (its 32 KB `gBodyBuffer` is dropped by the linker on the fail-API
path, so its RAM is not comparable). `paperdiy` stays on the prebuilt core: 1,874,187 B,
111,548 B (not re-baselined against MEM-01; its config path is unchanged). Native tests:
621 pass.

**Build times** (Apple silicon, 8+ cores): cold build of the core and firmware about
1 min 54 s; cached `cardputer-adv` 41 s; `-dev` 44 s; `-badapi` 49 s; `paperdiy`
1 min 43 s; switching back to `cardputer-adv` 2 min 22 s (rebuilds the core).

**IRAM and heap.** On the S3 the 512 KB internal SRAM is one physical pool. Code in
IRAM, static data, and the heap all come from it. The linker's `.dram0.dummy` reserves
in DRAM address space the bytes IRAM sections occupy, because the two aliases map the
same SRAM. Anything moved out of IRAM to flash therefore enlarges the heap. Here only
1.6 KB moved: the Wi-Fi blobs (`libnet80211`, `libpp`) are prebuilt with IRAM placement
and are not recompiled by the custom core, which rebuilds only IDF's wrapper sources.
So the 25–30 KB the paperdiy reader measured on the C3 does not appear here; the S3
heap gains 1.7 KB statically. The runtime gains come from the allocations, not the
linker: TLS record buffers (about 12 KB per connection), FatFs `FIL`/`FATFS` buffers
(the SD mount), the two task stacks, and the Wi-Fi buffer counts. None of these is in the
static numbers above.

**Pending on hardware (parent).** Repeat §5 points 1–7 on the MEM-02 image and compare
with MEM-01 §2.2. Check in particular:
- `[sd] mounted … heap a -> b`: expect the mount to take noticeably less than the
  26.1 KB of MEM-01 (FatFs buffers at 512).
- `[http] before/after` at the first TLS connect: expect about 12 KB more free per
  connection.
- Boot heap after the boot sync (`hh`): the 80,972 B / 31,732 B baseline.
- A clip playing (`[audio] alloc … heap a/b after`) and Wi-Fi throughput during an OTA
  download (IRAM_OPT=n costs some throughput).
- Any `ESP_WIFI` or FatFs misbehaviour, and that Bluetooth and core dumps behave as before.

### 2.1 Where the memory goes

**Static DRAM, 108.9 KB (B).** Our code 58.6 KB, Wi-Fi libs 14.8 KB, IDF/lwIP/
mbedTLS/FreeRTOS 11.3 KB, unresolved SDK 8.3 KB, unnamed `.data` 10.3 KB,
M5Unified/M5GFX 3.1 KB. Ours:

| Symbol | Bytes | Note |
|---|---:|---|
| `WkClient gBodyBuffer` | 32,768 | whole-body HTTP buffer for JSON pages (`WkClient.cpp:24`) |
| `gAppState` | 11,908 | includes two `std::mt19937` (~2.5 KB each) in the review and lesson `ReviewQueue`s |
| `uploadSdFile()::buf` | 8,192 | dev serial upload, compiled into the release |
| `AudioQueue gQueue` | 3,272 | 24 × 136 B URL slots |
| `dumpCanvas()::line` | 978 | dev screenshot, compiled into the release |

**Review batch, 57.8 KB (M; split E).** `Subject` is 384 B inline on xtensa;
the 49-subject array is 18.8 KB, the 20 review subjects' strings/vectors about
23 KB, the **29 component subjects about 19 KB in total** (they only feed the Tab card),
assignments and queue about 2 KB, block overhead the rest. Detail-only fields
(mnemonics, context sentences, parts of speech, related ids) are about 12 KB of
it. `Subject::audios` (all clip structs and URLs) is never cleared after the URLs
are chosen: 3–7 KB on a fresh fetch.

**Wi-Fi, 52.8 KB joined (M).** Arduino forces dynamic TX (32), dynamic RX 32,
static RX 4, cache TX 4; CSI, FTM, AMPDU on; lwIP TCP window 5,760. The Wi-Fi
driver's hot paths are in IRAM (`CONFIG_ESP_WIFI_IRAM_OPT`), which on the S3
comes out of the same SRAM as the heap.

**Peripherals (B/E).** SD mount 23.3 KB, of which about 17.9 KB is FatFs
buffers sized for 4 KB sectors (`CONFIG_WL_SECTOR_SIZE=4096`) on a card that
uses 512-byte sectors: 1 `FATFS` + 4 `FIL` × 3,584 B wasted. The speaker task and
I2S DMA hold about 6.4 KB (E) for the whole session after the first sound
(`M5.Speaker.end()` is never called). The loop task stack is 24,576 B
(`main.cpp:15`), and its measured peak use is about 8.8 KB. Fixed task
stacks: esp_timer 8 KB, tcpip 4 KB, event 2 KB (prebuilt).

**Fragmentation (M/E).** Things allocated while TLS buffers are live outlive
them and split the heap: subject arrays (7.7 + 11.1 + 18.8 KB requests during a
fresh fetch), the DOM parse of `study_materials`/`review_statistics` (~20–25 KB
in ~100 blocks per 10-id page), vectors grown by `push_back` without `reserve`
(subjects, summary ids, audio index), Arduino `String` header reads (one
realloc per 16 chars). One full TLS handshake per downloaded clip (new
`WiFiClientSecure`, `Connection: close`) and per OTA request. The 11 PEM roots
in `RootCerts.h` are parsed before the record buffers on every handshake.

## 3. Tasks

Savings are free heap gained (static savings count fully). "Peak" means the
transient maximum during a flow, not steady state.

### Track A — platform (biggest lever)

| ID | Task | Saving | Risk | Depends |
|---|---|---|---|---|
| MEM-01 | **Arduino 3 migration, phase 1.** pioarduino `55.03.311` (Arduino 3.3.11, IDF 5.5.5, as the owner's paperdiy-reader), prebuilt libs, M5Unified `96576a1` (0.3.0) and M5GFX 0.3.0 pinned, code fixed for IDF 5/mbedTLS 3. The firmware builds with the framework's gnu++2b (gnu++17 linked part of Matter: +560 KB flash, +20 KB RAM). **Done 2026-11-20, owner-verified** (orientation, keyboard, sync, reviews, R audio). Costs ~20 KB of heap against 2.0.17 (§2.2). | baseline only (−20 KB vs 2.0.17) | OTA from 0.2.x breaks (accepted) | — |
| MEM-02 | **Done 2026-11-20; device-measured (§2.4).** `custom_sdkconfig` for memory (§2.3; the PlatformIO Core 6.1.19 pin, not a CMake pin, is what macOS needed). Candidates, each A/B measured: `CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN=y` + `SSL_OUT_CONTENT_LEN=4096` (−12.3 KB per connection; IN stays 16384); `CONFIG_WL_SECTOR_SIZE=512` (−~17.9 KB SD mount); `CONFIG_ESP_WIFI_IRAM_OPT=n`, `CONFIG_ESP_WIFI_RX_IRAM_OPT=n` (paperdiy-reader measured ~25–30 KB on the C3; on the S3 the freed IRAM also returns to DRAM, to measure); `CONFIG_ESP_WIFI_CSI_ENABLED=n`, `CONFIG_ESP_WIFI_FTM_ENABLE=n` (−~2.8 KB static); dynamic RX 32→16 and `MGMT_SBUF_NUM` 32→16 (peak); `CONFIG_ESP_TIMER_TASK_STACK_SIZE=4096`, `CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH=2560` (−~5.5 KB); `CONFIG_ARDUINO_SELECTIVE_COMPILATION` for unused wrappers. Keep BT (BT-01 plans a BLE keyboard), keep core dumps unless space is short, keep IPv4 only if IPv6 is off without side effects (−~1.3 KB, measure). Optional: `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y` (known link trouble on pioarduino; try last). | **Static −12.4 KB flash, heap start −1.7 KB (B); on the device +27 KB free and a 61 KB largest block on Home, +12 KB in a review, SD mount −17.9 KB, TLS −12.8 KB per connection (M, §2.4).** | med: build time and CI, Wi-Fi throughput (OTA download time), one-time setup | MEM-01 |

### Track B — quick code wins (any platform)

| ID | Task | Saving | Risk |
|---|---|---|---|
| MEM-10 | Put `uploadSdFile`'s 8 KB buffer and `dumpCanvas`'s line buffer behind `KANIKEY_DEV` (or allocate on use). Check first whether release serial `up`/`p` are needed for field recovery. | 9,170 B static | low |
| MEM-11 | Replace the two `std::mt19937` in `ReviewQueue` with a 4-byte xorshift32 (as `wk/IdSample.cpp`); keep deterministic tests by seed. | ~4,990 B static | low–med |
| MEM-12 | Loop stack 24,576 → 16,384 (`main.cpp:15`) after checking the high-water mark from `h` across a lesson fetch, an OTA install and a full sync; keep ≥4 KB margin. | 8,192 B | low–med |
| MEM-13 | Clear `Subject::audios` once `audioUrl[]` is chosen (one line in `AudioPlan.cpp`/the parsers). | 3–7 KB on a fresh fetch | none |
| MEM-14 | SD `max_files` 4 → 3 (`CacheController.cpp:442`): steady state holds font pack + radical pack + one clip. | 4,136 B | low–med |
| MEM-15 | Speaker: `M5.Speaker.end()` after ~2 s idle (no clip, no cue), and `dma_buf_count` 8→4 / `dma_buf_len` 256→128 via `M5.Speaker.config()`. Listen for clicks and first-sound latency. | ~6.4 KB idle; ~3.6 KB while on (E) | med |
| MEM-16 | Playback set: minimp3 `MAX_FREE_FORMAT_FRAME_SIZE` 2,304 → 1,440, file window 4,096 → 2,880, half-frame PCM buffers (576 samples). Drops the clip set from 31.6 KB to ~27.2 KB and its largest piece stays 15.4 KB. | ~4.4 KB per clip | low–med |
| MEM-17 | `std::move` instead of copies when applying study materials; read HTTP header lines into a fixed `char[]` instead of `String`. | 1–2 KB peak; fewer reallocs | low |
| MEM-18 | `reserve` in the subject and summary parsers and for the audio index (`gAudio.entries`) from known counts. | fragmentation (no bytes) | low |

### Track C — batch structure

| ID | Task | Saving | Risk |
|---|---|---|---|
| MEM-20 | **Components out of the review batch.** Keep a compact fixed record per component (id, type, characters, primary meaning; ~40 B, no heap) for the Tab card instead of 29 full `Subject`s; drop the 11.1 KB and 18.8 KB array requests during a fresh fetch. | ~19 KB resident, largest request under TLS 18.8 → 7.7 KB | med |
| MEM-21 | Stream-parse `study_materials` and `review_statistics` (as subjects already are) instead of the DOM path; add a `reserve` parameter to `wkGetSubjectsRetry`. Also removes the latent 500-item cap failure in `fetchCriticalStatistics`. | 15–25 KB peak per page; ~100 blocks | med |
| MEM-22 | Detail-only fields (mnemonics, context sentences, parts of speech, related ids) loaded when the Tab/lesson card opens, from an offset table in `subjects.bin` (v14) or the subject catalog; `Subject` 384 → 276 B. | ~12 KB + 108 B × N | high |
| MEM-23 | Smaller `gBodyBuffer` (32 KB static): every JSON page parsed in streaming from the socket, so the body buffer shrinks to a few KB. Needs MEM-21 and response-size data first (log the largest bodies). | up to ~28 KB static | high |
| MEM-24 | One string arena per batch instead of ~450 small blocks (after MEM-20/22). | ~5–6 KB overhead + fragmentation | high |

### Track D — network behaviour

| ID | Task | Saving | Risk |
|---|---|---|---|
| MEM-30 | One TLS connection per audio drain (HTTP/1.1 keep-alive, `Content-Length`), and per OTA download sequence, instead of a handshake per clip/request. | N−1 handshake peaks per drain | low |
| MEM-31 | Per-host CA set (parse only the roots a host chains to) or the IDF certificate bundle (policy change: trusts the bundle's roots). | most of the transient CA parse per handshake | low–med |
| MEM-32 | Wi-Fi policy: keep the link up during a review session, off after an idle timeout on Home (after the sync and the clip drain); end the WkClient session and reset its clients before `WIFI_OFF`; store channel/BSSID for a fast rejoin. | total free (not the largest block) and power | med |
| MEM-33 | Measurement hooks (dev): heap free/largest right before each TLS `connect` (after the canvas release), and around `WiFi.mode(WIFI_STA)`/`begin`/connected; repeat join/off three times to see whether the residue grows. | data for MEM-02/32 | none |

### Not recommended

- 4 bpp palette canvas (16.2 KB): the UI uses 19 colour roles and anti-aliased
  glyph text needs 16 levels per colour pair.
- Two half-height canvas strips (16.1 KB): no origin shift in M5GFX; every draw
  twice.
- TLS `max_fragment_length`: deprecated, server support unknown, and it does
  not shrink fixed-size buffers.
- Patching the Arduino Wi-Fi library to free netifs and the event loop on
  `WIFI_OFF`.

## 4. Order

1. MEM-01 (running), flash and smoke on the device; record the §2 table again
   on Arduino 3 as the new baseline (`hh` at the same points).
2. MEM-33, then MEM-02 one group at a time (TLS, SD sector, Wi-Fi IRAM/buffers,
   task stacks), measuring each against the baseline.
3. Track B in one or two batches (each item is small and independent).
4. MEM-20 and MEM-21, then MEM-30/31/32.
5. MEM-22/23/24 only if the review with Wi-Fi up still has less than ~70 KB
   free with a 40 KB block after the above.

Target after steps 1–4: a review with Wi-Fi up keeps at least one 40 KB block
and ~70 KB free, so TLS (≤ 21 KB with MEM-02) and a clip (27 KB with MEM-16)
can both run without borrowing the canvas.

## 5. Measuring

Use the dev `hh` (free, largest, fragmented %, free/allocated blocks) and `h`
(free, largest, minimum, loop stack high-water mark) at fixed points:

1. Home after boot, no Wi-Fi.
2. Home after the boot sync.
3. Review question after a batch loaded from the card.
4. Same review after a fresh fetch (`H`, Enter).
5. Review feedback with Wi-Fi joined and idle.
6. During a clip (`[audio] alloc … heap a/b after`).
7. Home after End session and the clip drain.

and read the `[sd] mounted … heap a -> b` and `[http] before/after` lines.
`heap_caps_dump` over USB trips the interrupt watchdog: do not use it.

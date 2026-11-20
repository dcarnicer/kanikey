# Memory headroom

Status: **investigation, 2026-11-20.** Four read-only audits (static RAM, runtime
heap, TLS/Wi-Fi, peripherals) plus measurements on the owner's Cardputer ADV.
MEM-01 (the Arduino 3 migration) is in progress; nothing else is implemented.
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
| MEM-01 | **Arduino 3 migration, phase 1.** pioarduino `55.03.311` (Arduino 3.3.11, IDF 5.5.5, as the owner's paperdiy-reader), prebuilt libs, M5Unified/M5GFX updated and pinned, code fixed for IDF 5/mbedTLS 3 (OTA SHA-256 API, headers, watchdog). Same behaviour. **In progress (worktree `arduino3`).** | baseline only | med: OTA from 0.2.x breaks (accepted), API drift | — |
| MEM-02 | **`custom_sdkconfig` for memory** (pioarduino rebuilds the core; slow first build, CMake pin on macOS, cache patch script as in paperdiy-reader). Candidates, each A/B measured: `CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN=y` + `SSL_OUT_CONTENT_LEN=4096` (−12.3 KB per connection; IN stays 16384); `CONFIG_WL_SECTOR_SIZE=512` (−~17.9 KB SD mount); `CONFIG_ESP_WIFI_IRAM_OPT=n`, `CONFIG_ESP_WIFI_RX_IRAM_OPT=n` (paperdiy-reader measured ~25–30 KB on the C3; on the S3 the freed IRAM also returns to DRAM, to measure); `CONFIG_ESP_WIFI_CSI_ENABLED=n`, `CONFIG_ESP_WIFI_FTM_ENABLE=n` (−~2.8 KB static); dynamic RX 32→16 and `MGMT_SBUF_NUM` 32→16 (peak); `CONFIG_ESP_TIMER_TASK_STACK_SIZE=4096`, `CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH=2560` (−~5.5 KB); `CONFIG_ARDUINO_SELECTIVE_COMPILATION` for unused wrappers. Keep BT (BT-01 plans a BLE keyboard), keep core dumps unless space is short, keep IPv4 only if IPv6 is off without side effects (−~1.3 KB, measure). Optional: `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y` (known link trouble on pioarduino; try last). | **~60–75 KB** (E, before measuring) | med: build time and CI, Wi-Fi throughput (OTA download time), one-time setup | MEM-01 |

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

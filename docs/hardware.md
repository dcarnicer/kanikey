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
| Flash used | 1,740,389 B of 1,835,008 B (94.8%) |
| Static RAM | 93,208 B of 327,680 B (28.4%) |

### Heap

All values are internal heap, bytes.

| Point | Free | Largest | Minimum free |
|---|---:|---:|---:|
| After boot (32.4 KB canvas, static buffers) | 225,464 | 188,404 | — |
| After SD mount | 208,044 | 180,212 | — |
| Before TLS `/user` | 148,476 | 139,252 | 143,796 |
| After `/user` | 145,492 | 69,620 | 73,116 |
| After `/summary` | 145,488 | 65,524 | 70,880 |
| Idle after sync | ~146,800 | — | — |

The canvas is 8-bit RGB332 and costs 32,400 B. A 16-bit canvas (64,800 B) is
released for TLS and cannot be reallocated after the subject batches: the
largest free block falls to 28–63 KB after 5–20 subjects. The 8-bit canvas
always reallocate successfully. During a review fetch the canvas is freed only
for each TLS request and restored before the response is parsed.

Static RAM includes a 32 KiB response buffer in `WkClient`, an 8 KiB serial
upload buffer and a 16 KiB USB receive ring. Keeping response bodies out of the
heap is what leaves the canvas block intact across requests.

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
- The debug screen renders a test kanji from the SD pack. The `efontJA_16`
  fallback was removed on 2026-10-28; the build dropped from 1,741,705 B to
  1,273,425 B before the cache work. A missing glyph shows `no glyph`.
- The Home layout was rebalanced after a live screenshot: 16 px brand, 32 px
  counters, 8 px labels, 12 px lower rows. `tools/shot.py` reads the 240×135
  RGB332 canvas over USB and writes a PNG.
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
- Review prompts are drawn from the SD visual pack at 64 px; readings use
  32 px and feedback uses 20 px. `efontJA_16` is no longer linked.
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
- Flash headroom is now ~498 KB after removing the efont fallback; the next
  large blocks are lessons and settings.
- Home shows `Outbox: N pending` or `N need attention` below the sync time.
- Power: dim after 30 s, blank after 60 s, deep sleep after 2 min only when no
  review is active and the device is on battery. A key wakes the panel; the
  first key on a blank screen only wakes it.

### Setup portal (M5)

- First boot with a missing Wi-Fi/token starts a WPA2 AP `KaniKey-XXXX`; the
  screen shows the key and `http://192.168.4.1/`.
- The AP page was served and rendered correctly on hardware on 2026-10-28:
  pack status, browser clock/zone and the home-Wi-Fi form.
- After saving Wi-Fi the device joins the network and serves the station page at
  its IP; the token is validated with `GET /user` and stored only on success.
- Setup makes no `PUT /assignments/{id}/start` calls, so it does not move
  lessons. The normal first sync afterwards may flush the existing outbox.
- Hardware test: the page reached a laptop on the setup AP; the station phase
  finished setup on the same session. The AP is closed afterwards.

### Pause and settings (M5)

- `Esc` in a review opens Continue / End session / Settings; `Esc` from Home
  opens Settings.
- Brightness, dim/blank/sleep timings, review batch/order, lesson batch,
  Setup portal, Sync now, About, Clear cache and Sign out are stored/executed
  on device; values persist in NVS.
- About reads the live pack, cache batch, heap, Wi-Fi/token presence, time zone
  and last sync.
- Below 20% on battery, starting reviews or lessons opens a confirmation; USB
  power skips it.
- 401/403, 429, 5xx/other and network failures have distinct boot/Home states;
  Enter retries or opens setup and a successful sync clears the notice.
- The screens compile in both images; a physical key/visual pass is pending.

### Lessons (M4 start)

- `/v2/assignments?subject_ids=...` for 5 lesson IDs: ~2.3 KB.
- Subjects fetched in batches of 3 and 2, with mnemonics/components: ~9.2 KB and
  ~5.7 KB.
- The lesson card uses the SD pack for the 64 px prompt and 32 px reading, 3
  wrapped mnemonic lines, paging and `PUT /v2/assignments/{id}/start` ->
  **200**.
- The summary DOM was replaced by a small streaming parser: the 568-id summary
  tree had caused an intermittent `std::bad_alloc` (`operator new` in
  `JsonParser::array`). The new parser keeps counts and at most 20 IDs per
  bucket. Two consecutive boots and a lesson start have been clean.
- Lesson list/picker (2026-10-28): the list sorts a 10-subject candidate batch
  by level and `lesson_position`, then trims to the configured batch. It only
  uses GET /assignments and GET /subjects; opening the card does not start a
  lesson. Verified on hardware with 120 available lessons: 5 rows rendered
  with prompts and meanings, and no lesson was started. The component subjects
  are fetched separately (one batch, 10 ids in the test) and their meanings plus
  hints appear on the last card page.
- The no-lessons screen uses the local summary and needs no request; it shows
  the next review time.
- Lesson quiz (2026-10-28): `Q` opens a local meaning/reading quiz with typed
  answers, retries and a completion screen. Hardware-verified: 欠点/けってん
  answered correctly for meaning and reading, completion showed
  `No /start sent.`. The quiz makes no network or `/start` call.

### Hardware verification 2026-10-28 (WK-01/WK-03/LESSON-04)

- Review batch: 20 assignments and 20 subjects fetched. Four study materials
  were applied (subjects 2556, 3406, 4071 and 7679); all are synonyms and this
  account has no meaning/reading notes.
- Real radical prompt: radical 舌 (Tongue) rendered at 64 px from the 96 px
  bitmap; the feedback screen showed `CORRECT` for `Tongue`.
- Review order: the account preference is `shuffled`; the log showed
  `order shuffled, first levels 7 7 7 7 7 7 3 7`. `lower_levels_first` remains
  native-tested until the account preference is changed.
- Offline review: fetched 20 items, disconnected Wi-Fi, completed 舌, saw the
  outbox item pending (`HTTP 0`), reconnected and flushed with `f` ->
  `POST /v2/reviews` **201**. The cache kept 19 assignments / 399 due. One real
  review was submitted during this test.
- Lesson flow: 5 VOCAB L7 rows, card 欠点 / けってん, and the quiz meaning and
  reading both correct. No `/start` call was made.
- SRS indicator: the review header showed `SRS 1` with a synthetic cached
  review. The summary line needs a successful live POST; Wi-Fi was flaky at the
  end of the session (-80 dBm), so it is native-tested only.
- Review statistics: with a synthetic cached review, the header showed
  `SRS 1 86%` and the summary showed `History 18 reviews, 89%`. The outbox
  sends failed offline, so the stage-change arrow was not exercised in this
  run; the stats display itself is verified.
- Long readings: feedback uses `Y`/`A` side labels and a right-aligned 20 px
  fallback, so かつようけい fits without clipping.
- Crash found and fixed: growing `batch.subjects` while a subject page DOM was
  alive threw `std::bad_alloc` at 15/20. Reserving the vectors and clearing each
  page DOM before appending fixed it; a 20/20 fetch then passed.
- Still pending: a reboot with Wi-Fi actually unavailable, a radical-only lesson
  on a low-level account, `lower_levels_first` with the account setting, and
  note display (this account has no notes).

### Visual pack on microSD (M5)

- Path: `/kanikey/glyphs.kgp`, format `KKGP` v1: 32-byte header, 20-byte glyph
  entries sorted by `(codepoint, size)`, 4bpp RLE payload, optional 16-byte
  radical entries.
- Tokenless pack: 2,207 characters / 6,621 glyphs at 64/32/20 px / 2,895,555 B,
  made from Tangocho's local WaniKani kanji cache (2,026 kanji + kana +
  `々〜・〇`). It contains no radical images.
- Token pack (on the card since 2026-10-27): **7,269 glyphs + 498 radical
  bitmaps**, 3,698,476 B. Radical prompts now render from the 96 px bitmaps.
- Boot: `[pack] 7269 glyphs, 498 radicals, 64/32/20 px`; lookups binary-search
  the entry table and decode one glyph at a time. The pack is not loaded into
  RAM.
- The 64 px review/lesson prompt, 32 px reading and 20 px feedback are drawn
  from the pack. `efontJA_16` was removed on 2026-10-28; missing glyphs show an
  explicit state instead.
- Review screenshot: 思う and おもう rendered from the pack; lesson screenshot:
  欠点 / けってん with the mnemonic.

Live captures are kept out of the repository because they contain the user's
WaniKani subjects and mnemonics.
- The canvas is 8-bit RGB332 (32,400 B); `tools/shot.py` decodes the `#shot`
  dump. The `#shot` mode field says `rgb332`.
- Upload: `S` starts a one-shot HTTP server on port 8080 and
  `tools/device.py push` writes the file with `curl -T`; 2.9 MB transferred
  reliably. The USB `up` path uses 8 KiB ACKed chunks and is a fallback.
- Offline cache: `/state/cache.bin` (LittleFS, `KKCA` v6) and
  `/kanikey/cache/subjects.bin` (SD, `KKCS` v6), with the summary
  `data_updated_at`, 24 h stale state and manual clear/sync. A synthetic-cache
  run on 2026-10-28 restored Home offline, ran a cached session, kept the
  completed review in the outbox and restored both after a reboot. Real-account
  fetch/reconnect is still pending.
- Still missing: real review/lesson prompts on a configured account and a
  radical-only lesson on a low-level account.

### Next

- Radical-only lesson on a low-level account.
- Study-material notes and `lower_levels_first` on a configured account.
- A reboot with Wi-Fi actually unavailable (offline boot).
- Vacation mode with an account that is actually on vacation.
- Ways to dismiss rejected outbox items.
- Real renderer/UI font instead of the M5GFX text calls.

# Hardware log

Measurements from the actual M5Stack Cardputer ADV. This file is a log, not a
promise: numbers change as the firmware grows.

> Partition note (2026-11-20): the 8 MB table is now 2 × 3 MiB app slots with
> LittleFS at 0x610000 (`firmware/partitions/kanikey_8MB.csv`). Entries dated
> before that use the old 1,835,008 B slot and LittleFS at 0x390000; their
> figures are not restated here.

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
  counters, 8 px labels and 8 px lower rows. `tools/shot.py` reads the 240×135
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
```

- No coredump partition is configured; the app does not use one yet.
- Builds before 2026-11-01 also printed `addApbChangeCallback(): duplicate`.
  It was not benign: the SD card and M5GFX's panel were started on the same
  Arduino SPI bus (HSPI), which could freeze the device (FIX-28). The card now
  has its own bus and the line is gone; if it comes back, check the buses.

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
- At this point the app still lacked a persistent outbox. **Superseded:** the
  outbox landed in M3; see “Persistent outbox (M3 start)” below.

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
  `Pending`; reboot loaded 1 pending, then after reconnecting it flushed,
  `POST /reviews` returned 201, and the outbox was empty. This is the key
  reliability path; a reboot with Wi-Fi still unavailable was not part of it.
- Ambiguous requests (timeout/5xx after the request was sent) are marked
  `Ambiguous` and reconciled with `GET /assignments?ids=...` before retrying.
  That reconciliation has unit-level coverage but not a hardware timeout test.
- Rejected records stay until the user opens `O: outbox` on Home, inspects the
  HTTP reason and confirms a per-item discard; the dev command `z` still clears
  the whole outbox.
- Flash headroom was ~498 KB at this point after removing the efont fallback.
  The later lessons/settings/cache work used part of it; see `ROADMAP.md` for
  the current build size.
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
- Level progress: `/v2/level_progressions` returned 200 (3,275 bytes) and the
  log showed level 8 with its unlock/start timestamps. Home showed
  `L8 since 03 Feb` in the status line. The request took ~2.2 s.
- Item detail: `Tab`/`D` on feedback opened the detail card with meanings,
  readings, synonyms, mnemonics, hints, notes and components on a synthetic
  cached review. The vocabulary item showed the sixth page with `noun` and
  `This is Japan.` The related page showed `Similar: 1` and
  `Amalgamations: 1` with the cached `Japan` meaning. A real review batch
  fetched 24/24 component subjects; the subsequent cache save then hit
  `std::bad_alloc` while copying 44 subjects.
  The copy was removed (`encodeSubjectCache` now takes the vector directly),
  but the fix still needs a clean hardware run because Wi-Fi failed afterwards.
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
- Offline cache: `/state/cache.bin` (LittleFS, `KKCA` v11) and
  `/kanikey/cache/subjects.bin` (SD, `KKCS` v11), with the summary
  `data_updated_at`, 24 h stale state and manual clear/sync. A synthetic-cache
  run on 2026-10-28 restored Home offline, ran a cached session, kept the
  completed review in the outbox and restored both after a reboot. The
  real-account online fetch → Wi-Fi off → review → reconnect → 201 path is
  recorded above; a deliberate reboot with Wi-Fi unavailable is still untested.
- Still missing: a radical-only lesson on a low-level account, study-material
  notes/`lower_levels_first` on a configured account, and the final OUTBOX
  screen/API-02…05 hardware passes.

### Next

- Radical-only lesson on a low-level account.
- Study-material notes and `lower_levels_first` on a configured account.
- A reboot with Wi-Fi actually unavailable (offline boot).
- Vacation mode with an account that is actually on vacation.
- Real renderer/UI font instead of the M5GFX text calls.

### Hardware verification 2026-10-28 (UI-01)

- `/v2/assignments` does not accept `per_page`; the count requests use
  `srs_stages=<bucket>&hidden=false&page_after_id=999999999`. The cursor past
  the last assignment leaves `data` empty while `total_count` still reports the
  full filtered collection. Each response was 335-360 B.
- The five requests returned Apprentice 145, Guru 143, Master 81,
  Enlightened 147 and Burned 586 (1,102 assignments). Home drew the segmented
  224 px bar and the `A/G/M/E/B` labels; the layout is bar y=100, labels y=106,
  status y=117 and hint y=125.
- The sync clock moved to the header (`Synced 20:39`) with a gap before the
  battery. The counts were stored in `KKCA` v10; the next boot logged
  `[cache] srs A 145, G 143, M 81, E 147, B 586`, so the bar also has cached
  data offline. A live screenshot was captured in `tools/screenshots/out/`
  (not committed because it contains user data).

### Hardware verification 2026-10-28 (UI-02)

- A normal boot drew the six sync rows and completed Wi-Fi, Outbox, Account,
  Summary, Level and SRS in order; the log ended with `[sync] complete` and
  Home showed the fresh counts. The last screen frame (before Home) showed
  `Synced`, `6/6`, all rows `ok` and the full progress bar.
- A temporary forced Summary failure rendered `Sync failed`, the failed row in
  red with `X`, `3/6` and `Summary: network unreachable`, with
  `Enter: retry  Esc: back`. A local capture is in
  `tools/screenshots/out/sync-failed.png` (not committed).
- A temporary auto-cancel after Summary logged `[sync] cancelled`, made no
  Level or SRS requests and returned to Home with the already-fetched summary;
  what had arrived was kept.
- The failure and cancel checks used temporary builds; the final image was
  reflashed and completed a normal sync. The cancel input is cooperative: a key
  pressed during a blocking request is read when it returns.

### 2026-10-29 — API robustness (implementation landed, hardware check pending)

- The shared 429 gate, shared 401 gate, parse-failure retries and the 50-attempt
  stuck state are code/native/host-inspected only. They have not been exercised
  on hardware.
- The earlier outbox hardware runs covered a real 201, a synthetic 422 retained
  as `Rejected`, and the offline flush. The final `OUTBOX` screen, attempts/age
  detail and per-item discard still need a hardware pass.
- No firmware was flashed or changed for this documentation audit.

### Hardware verification 2026-10-29 (UI-05/UI-06)

- After a real sync, Home showed `L8 3% since 03 Feb`: the current level's
  passed-kanji percentage came from the two `total_count` requests, and the
  level timestamp still fit on the status line. No level-up occurred, so the
  `Level up!` notice still needs a real transition.
- Serial `l` fetched the lesson list and `e` opened the first card. The live
  card showed `1/5` and `p1/2` in the header, the 2 px item bar at y=14 and
  `Space p  Q quiz` / `Enter start  Esc list` at y=125 without clipping. No
  `PUT /start` was sent; the card was only opened for the screenshot.
- The final image was reflashed after the UI-05/UI-06 changes. Live captures
  remain in `tools/screenshots/out/` and are not committed.

### Hardware verification 2026-10-29 (TECH-01)

- The streaming assignment/subject parser was exercised with the real account.
  A review batch fetched 20 assignments and four 5-subject pages; the logs
  showed `subjects 5/20` … `20/20`, then 24 component subjects, with no
  `bad_alloc` and no DOM parse errors. Free heap after the review pages was
  ~148.6 KB with a 52.3 KB largest block; after components it was ~126.1 KB
  with a 51.3 KB largest block.
- A lesson batch fetched 10 assignments and four 3-subject pages; the largest
  free block stayed at ~35.8 KB and the free heap was ~108.2 KB after the last
  page. No crash or truncated JSON.
- The session was only opened for the fetch and then exited; no review was
  submitted and no `PUT /start` was sent.

### 2026-10-29 — TECH-02 phase 1 (Home screen extraction)

- Home was moved to `app/HomeScreen` with `ui/Theme.h`; the live capture kept
  the same brand, counters, next-review row, SRS bar, hint variants and status
  line. The level-count requests hit a transient network error in that run, so
  the status line correctly fell back to `L8 since 03 Feb` instead of a
  percentage. The 72 native tests and both firmware builds passed.

### 2026-10-29 — TECH-02 phase 2 (Review, Lesson and dev screens)

- Flashed the extracted release build (1,421,589 B) and captured Home, the
  review prompt, wrong-answer feedback, both detail pages, the session summary,
  the empty lesson state, the lesson card and its second page, the lesson quiz
  prompt/feedback, the radical test and the dev debug frame. Layout, colors,
  glyphs, counters, notes and hints matched the pre-refactor captures.
- The review summary used the dev cache seed: three synthetic items were
  answered offline with Wi-Fi disconnected, then the synthetic outbox was
  cleared. No real review or `PUT /start` was sent.
- The lesson list frame could not be captured: the lesson fetch releases the
  sprite canvas and the largest free block afterwards is ~31.7 KB, below the
  32,400 B canvas. The canvas recreation failure is reproducible on the
  pre-refactor baseline (`42684ea`), so it is not a regression from this
  extraction. The lesson card/quiz captures came from a release session where
  the canvas reallocated successfully after the fetch.
- The `KANIKEY_DEV` build also reproduces a pre-existing `loopTask` stack
  overflow during the same lesson fetch on the baseline; the release build
  completes the fetch and draws the lesson card/quiz. The extraction adds about
  112 B to the lesson-list/card draw stack, so this claim is limited to the
  fetch path, not the draw frames. Both issues remain for the release-hardening
  pass.

### 2026-10-29 — TECH-03/06/08 (dev hooks, OTA, hardening)

- **Lazy review batch:** boot logs now read
  `[cache] loaded: 20 cached assignments (batch lazy)`; starting a review loads
  them with `[cache] batch loaded: 20 assignments, 44 subjects`, and ending the
  review or starting lessons logs `[cache] batch evicted, 20 assignments remain
  on disk`. The lesson fetch minimum free heap rose from **~3.5 KB** to
  **~56.3 KB** and the canvas recreated after every request (no
  `canvas attempt failed` lines). The largest block during the review-batch
  fetch stayed >=98 KB.
- **Stack:** the loop task is now 24 KiB. A dev-build lesson fetch completed
  with **17,876 B** of stack high-water headroom (`h` prints `stack`); the same
  fetch overflowed the old 8 KiB loop task on the pre-refactor baseline. The
  USB RX ring is 4 KiB and `tools/device.py upload` sends 2 KiB chunks.
- **Dev hooks:** the release image accepts only `h`, `i`, `p`, `S`, `up` and
  `r`; the dev image keeps the remote/token/synthetic/OTA commands. `F`/`I`/
  `V`/`U`/`Y` were used for the checks below. The `gDirectReading` bypass is
  no longer in release code.
- **OTA check:** the default GitHub URL returned a clean `404` (no release
  published yet) and the Firmware screen showed the HTTP error without changing
  the badge. A local `http://192.168.1.10:8765/kanikey-firmware.txt` test
  manifest (dev override `U`) parsed and offered v0.1.1/v0.1.2/v0.1.3.
- **OTA install:** v0.1.0 in `app0` installed a v0.1.2 dev image into `app1`
  (1,474,016 B, SHA-256 checked) and booted it. v0.1.2 then installed v0.1.3
  into `app0` (1,474,192 B) and booted it. Settings, the cache and the outbox
  survived. The install record was detected on the first boot; the new image
  was marked valid after 10 s.
- **Rollback:** an earlier v0.1.1 attempt was reset from USB before its 10 s
  validation window; the bootloader returned to the old slot and the next boot
  logged `not offering v0.1.1 again (rolled back)`. This is the expected
  behaviour, not a bug. The test record was cleared with `Y`.
- **Final image:** the release `cardputer-adv` build is flashed over USB at the
  end of the run; the dev-only test hooks are not present in it.
- **Final release:** after the last USB flash the release image reported
  `version 0.1.0 (cardputer-adv)`, 219,620 B free / 180,212 B largest at boot,
  and `h` after sync showed 141,284 B free / 63,476 B largest / 47,664 B min /
  17,812 B stack headroom. No `[ota] rolled back` line was present after the
  test record was cleared.

### 2026-11-01 — storage robustness (FIX-24/25/26)

Implementation landed, hardware checks still open (no device was attached).

- **Key queue (FIX-24 a).** `push()` wrote the "events lost" marker at the slot
  of the event it had just dropped, so the marker was never delivered. The
  marker is now the newest event (`wk/RingQueue.h`, five native
  `test_key_queue_*` cases model push/pop order). Hardware: hold a key through a
  key storm in a dev build and check that the `Key::None` event arrives last in
  the burst, not in the middle of a press/release pair.
- **Pack CRC (FIX-24 b).** `GlyphPack::open()` now streams the entry table and
  the payload — `[tableOffset, dataOffset + dataLength)`, 3,698,444 B of the
  3,698,476 B card pack — in 1 KB chunks and compares the CRC-32 with the header
  field at offset 24. This matches `tools/glyph-pack/build.py`, which stores
  `zlib.crc32(table + data)` at offset 24 and verifies `raw[32:data_off+len]`
  (it asserts `table_off == 32`). A mismatch fails with `pack: bad crc`. Every
  full open logs `[pack] crc ok, N ms` (or `bad`). **Measured: 3,587 ms for the
  3.7 MB card pack** (hardware, 2026-11-01). That is far above the ~1 s bound,
  so the CRC is no longer on the boot path: `open()` takes
  `checkCrc = false` by default, and `setup()`, `retryBoot()` and the recovery
  path open without it. Only an explicit check (the portal's *Check pack*, on
  the next `loop()`) or a newly uploaded pack (verified as `glyphs.kgp.tmp`
  before it replaces the old file) runs the pass.
- **Pack handle recovery (FIX-25).** Repro of 2026-11-01: open the station page,
  press *Check pack*, finish setup and start a review — every prompt showed
  "no glyph · check SD" until a reboot while `i` still reported 7,269 glyphs.
  Debug output showed `seek 72712 failed, size 3698476 pos 4294967295`: the
  `File` of a pack that was re-opened inside the HTTP request had a dead
  descriptor even though its header and table had loaded. `readAt()` and the
  draw seeks now re-open the file once when `seek()` fails, `position()` reads
  `UINT32_MAX` or a read is short, log `[pack] file handle lost, re-opening`,
  and retry; the recovery path validates the header and table but not the CRC.
  `GlyphPack::reopen()` (same validation, no CRC) is public for the app.
  `portalRecheckPack()` only reports a ready pack; a missing one is opened by
  `loop()` after the HTTP request, with the CRC. Both upload paths close the
  client (Wi-Fi) before re-opening the new pack on its final path. The repro
  still has to be re-run on hardware.
- **Cache save (FIX-26).** A snapshot save called `exists("/state/cache.tmp")`,
  which logged `open(): … does not exist, no permits for creation` on every
  save. The tmp file is now opened with `w` (truncates a leftover) and the old
  file is copied to `.bak` before `.tmp` is renamed over `main`, as the outbox
  does. Hardware: a sync in a release build should print no `cache.tmp` line,
  and the cache should still load after a reboot.
### 2026-11-01 — Response sizes

The serial log prints `[summary] body N bytes` and
`[review] subject page a-b: N bytes` (lessons print the same with `[lesson]`).
Measured on the owner's account (416 reviews due, 120 lessons):

| Request | Body bytes | Notes |
|---|---:|---|
| `GET /summary` (owner account, 1000 IDs per bucket kept) | 4,345 | well below the 32 KB cap |
| `GET /subjects?ids=` 5 review subjects | 11,405-15,405 | fallback 5 → 2 → 1 not needed |
| `GET /subjects?ids=` 3 lesson subjects | 2,484-6,791 | fallback 3 → 1 not needed |
| Subject cache file, 20-item batch (46-54 subjects) | 20,319-25,550 | streamed (FIX-27): save 137-1,381 ms, load 51-838 ms |

The 5 → 2 → 1 and 3 → 1 retry paths still need a dev build with a forced small
cap. Two fresh 20-item batches from the 416 due started with different items
(FIX-08).

### 2026-11-01 — subject cache bad_alloc (FIX-27)

Crash on hardware (2026-11-01): a 20-item review batch with 55 subjects (20
assigned + 35 components) aborted with `std::bad_alloc` right after the batch
fetch. Backtrace: `runBatch()` → `saveCacheAll()` → `kk::cacheSaveSubjects()`
(`platform/CacheStore.cpp`) → `kk::encodeSubjectCache()` (`wk/Cache.cpp`) →
`Writer::str()` → vector insert → `operator new` → abort. Heap at that moment:
103 KB free, 73 KB largest block. The encoder built the whole payload in one
`std::vector` that grows by doubling, and `encodeFile()` then copied it into a
second vector with the header, so a save needed about three times the payload
in large contiguous blocks. The load had the same shape: `readFile()` resized
one vector to the whole file before `decodeSubjectCache()`, and
`loadCachedBatch()` runs from `startReview()` on Home after the batch was
evicted, when the heap is just as tight.

Fix: the subject cache is streamed through fixed 1 KB blocks in both
directions (`wk/Cache.h`: `ByteSink`/`ByteSource`, `writeSubjectCache()`,
`readSubjectCache()`; `platform/CacheStore.cpp`: `FileSink`/`FileSource`). The
save counts the payload and CRC in a first pass, writes the 24-byte `KKCS` v11
header, then streams the payload in 1 KB blocks. The load validates the header,
decodes from a 1 KB refill buffer with a running CRC, and requires the payload
to be fully consumed. The on-disk bytes are unchanged (no version bump); the
old vector encoder and decoder still exist and share the same field code. Peak
extra memory is one 1 KB stack buffer per direction instead of ~3× the payload
on the heap. Hardware check still open: repeat the 20-item/55-subject batch and
confirm the save and load log lines (`[cache] subjects saved: … bytes, … ms`)
with no `bad_alloc`, especially after `startReview()` on Home with an evicted
batch.

### 2026-11-06 — round 2 verification (dev image)

Dev image of `spec/round-2` (1,495,169 B, 81.5% flash, 95,576 B RAM), synthetic
cache (6 assignments: 1 radical, 2 kanji, 3 vocabulary), Wi-Fi off, no real
`POST /reviews` or `PUT /start`. New dev hooks used: `L` radical-only lesson,
`A <code>` fake lesson start, `B` bad pack CRC, `E` zero due, `H` drop batch +
lesson bucket, `O` force battery, `Z <seconds>` sleep, `G <n>` key queue,
`T` token without probe. `tools/session.py` keeps one serial session open.

| Check | Result |
|---|---|
| Boot heap / pack | 219,344 B free, 180,212 B largest; pack `7269 glyphs, 498 radicals, 64/32/20 px` |
| Heap after synthetic cache / review | 195,236 B free, 163,828 B largest, min 188,716 B; loop stack high-water 21,740 B |
| AP setup | `KaniKey-AAC8`; two boots produced different random passwords; 15-minute close not run |
| Radical-only lesson | list `RAD L1 ground`, card and quiz draw the radical bitmap; synthetic, no low-level account |
| Lesson start states | fake 200/422 advance; 500 shows `Could not start lesson / HTTP 500`; 0 shows `No connection`; 401 arms auth; no real request |
| Deep sleep in quiz | sleep 30 s + forced battery + quiz open 40 s -> no `[power] idle sleep` |
| Corrupt LittleFS | prompt `Storage unreadable`; Esc kept `KKOB`/`KKCA` in a full partition dump; Enter formatted and loaded 0 outbox items |
| Zero due | `No reviews now`, next review time, `Enter: sync  Esc: home`; no request |
| Failed fetches | reviews and lessons show their error and `Enter: retry  Esc: home` |
| Key queue storm | 12 events, then the lost-events marker as the newest event |
| Bad pack CRC | `pack: crc bad, 0 ms` / `pack: bad crc`; good pack restored |
| USB upload | after the 2 KB block fix, a 64 KB `up` completed; with the setup portal active it timed out at 0 bytes |

The release image after the pass is 1,476,485 B (80.5%) flash and 95,528 B RAM;
163 native tests pass. The device was restored to its Meshtastic image from a
full 8 MB backup afterwards. Captures from the pass are in
`tools/screenshots/out/` and are not committed.

### 2026-11-06 — AUD-01 audio spike (pending hardware)

The parser, speaker path and SD cache are merged; the measurements and playback
are not. `Subject` keeps up to four `pronunciation_audios` clips (URL, content
type, voice), the Cardputer build enables `internal_spk`, serial `n` plays
880 Hz for 150 ms, and serial `a` caches up to four clips from the current
review/lesson batch. Still to measure on the device: clip sizes for
`audio/mpeg`/`audio/ogg`, the decoder flash/RAM/decode time, and whether
`M5.Speaker.playRaw()` covers the I2S/ES8311 path. Until then AUD-03 (playback)
is a deliberate no-go, not a silent omission.

### 2026-11-19 — v1 hardware pass (Cardputer ADV, dev image)

Full pass on the real account (level 8, 464 due, 120 lessons, 7 critical) with
the dev image: 1,622,625 B / 1,835,008 B flash (88.4%), 96,336 B RAM (29.4%);
216 native tests pass. The release image is 1,556,989 B (84.8%), 96,248 B RAM.

- **Panel rotation (fix).** The ST7789V2 panel is configured with
  `offset_rotation = 1`, so `setRotation(1)` produced 135×240 portrait.
  Rotation 0 is the landscape orientation (240×135). `board::begin()` and
  `App::setup()` now use 0; the dev `h` command prints `rot`, size and panel
  config (`rot 0 240x135 panel 135x240 off 1 mem 240x320`).
- **OUTBOX (API-01/05).** Dev `J` seeded a 422, a 409 and a 50-attempt stuck
  item. The screen listed all three (`HTTP 422`, `HTTP 409`, `gave up`); the
  discard confirmation showed `WaniKani rejected this review (HTTP 422). 1
  attempt, age 8m.`; Enter discarded only seq 1 (2 items left).
- **Offline boot (CACHE-07).** With a real synced cache and a fake saved
  network (`OfflineTest`), a reset loaded the cached Home (464 due, 120
  lessons, SRS A145 G143 M81 E147 B586), `[cache] loaded: 20 cached
  assignments`, and started a review from the cache (宝, 1/20) with no
  network. The NVS was restored afterwards. The review → outbox → 201 path
  was already verified in the previous run.
- **API-03 401.** `T` saved a dev token; the boot `/v2/user` returned a real
  401. The shared gate armed (`401 token rejected; blocking requests until the
  token changes`); a following `u` logged `[http] /v2/user blocked by auth
  (401)` with no TLS request. NVS restored. 403 was not run (needs a
  read-only token).
- **API-04 retry.** A real `/v2/assignments?...&srs_stages=1,2,3,4` response
  returned 200 with 32,768 bytes truncated; the log shows the retry and a
  second parse failure. This exposed the practice-pool bug below.
- **Practice pool (new fix).** The full apprentice set exceeded the 32 KB
  body cap and saved 1 assignment. Fetching stages 1-4 separately now saves
  all 145 (12/28/54/51), with bodies of 5,768/13,529/26,157/24,606 bytes.
  This is the real FIX-14 case; the subject-page 5 → 2 → 1 fallback was not
  triggered.
- **Wi-Fi upload (FIX-24c).** `S` plus `curl -T` uploaded 64 KB to
  `/kanikey/test-upload.bin`; `[asset] done 65536/65536`.
- **OTA good path (FIX-20).** A local v0.1.1 image and manifest were served
  over HTTP; the dev `U`/`F`/`I` path installed it into app1. It booted as
  0.1.1, the first sync succeeded, and a reset stayed on 0.1.1. The device was
  then reflashed with the dev 0.1.0 image. The bad-first-boot/two-strike
  rollback was not run.

- **SPEED auto-sync (merged 2026-11-19).** The first smoke stopped at
  `SyncStep::Time`; the blocking `configTzTime()` was replaced by the ESP-IDF
  SNTP task with progress recorded first. The second smoke reached
  `[time] synced` but the loop returned on a blank panel before
  `syncAdvance()`; a background job now advances while blank and the idle
  timer is refreshed after each step. The automatic quick sync then logs the
  step transitions, `/v2/summary` and `[sync] complete`; a recent cache skips
  the sync (expected) and the first on-demand lesson/review fetch joins Wi-Fi.
  The lesson list/card/quiz smoke below runs with that path.
- **Lesson batch smoke (LES-01/02/03).** With the automatic sync skipped, `L`
  joined Wi-Fi on demand, fetched 5 lessons and showed the `1/5` list; Enter
  opened the `1/5 p1/4` card and the batch quiz showed `LESSON QUIZ 1/5`. No
  `[lesson] start` was sent. A later real batch started 5 `PUT /start` (all
  200) and Home dropped from 120 to 115 lessons. The full quiz with mistakes,
  offline start and 422 remain.
- **CFG menu smoke (CFG-01/02/03).** Home → Esc showed the eight sections with
  their counts (`Reviews 12`, `Lessons 6`, `Study 5`, `Sync & data 6`,
  `Display & power 6`, `Reminders & sound 7`, `Account token set`, `System`);
  Reviews showed 12 rows; `Batch size` cycled 20 → 30 with Right and back to
  20 with Left, with the help line. Review lock showed the faint destructive
  rows with `End the review session first`. Reset settings after ARCH-04
  (System → Reset settings, two Enters) showed `Settings reset`; token, Wi-Fi,
  cache (A150 G143 M81 E147 B586) and the empty outbox survived. `Pause →
  Settings → System → About → Esc ×3` returned to Pause. Origin/scroll/upgrade
  values and the post-reset brightness/timers remain.
- **REV feedback (REV-01/03).** 13 native tests cover the accepted tokens,
  synonyms, grouped readings and hint fallback; the hardware feedback look
  (multiple meanings, kanji groups, lesson quiz) remains.
- **SPEED-07 Home clock (2026-11-19).** Two native Summary tests; the 60 s
  Home tick and the offline-clock line still need the hardware run.

Remaining hardware checks: API-02 429 (native-tested; a real 429 was not
provoked), API-03 403 (read-only token), FIX-06 slow server, FIX-07 15-minute
AP close, FIX-14 forced small cap, FIX-20 bad-first-boot/two-strike, RAD-04
low-level account and a real level-up.

### 2026-11-19 — SPEED-05 keep-alive stall and fix

The keep-alive/chunked transport (SPEED-05) first stalled at `Outbox` with no
`[http]` line; it only continued after serial input. The cause was the idle
timer: `App.cpp` captures `now` before a sync step refreshes `gLastInputAt`, so
`now - last` underflowed to ~49 days, the panel blanked, and the modal sync
waited for a key. `wk/IdleTimer.h` clamps a future timestamp; dim/blank/sleep
use it, and a modal sync wakes the panel if it is already off.

After the fix, a manual `S` sync on the dev image (Wi-Fi connected first, no
serial input during the run) completed:

```
[sync] step Time -> Outbox (sub 0)
[sync] step Outbox -> Account (sub 0)
[http] ... /v2/user reuse ... -> 200 ...
...
[http] /v2/subjects?ids=419 reuse
[http] ... -> 200, 2199 body bytes, 199 ms, cl=-1 chunked keep
[http] session close after 35 requests
[sync] complete
[power] blank after 60002 ms idle
```

Minimum free heap in the request logs was 22,836 B (largest 24,564 B at the
end); the panel blanked only after 60 s of real idle. This confirms the
`Outbox` stall is gone and chunked responses parse. The TLS spike timing/heap
comparison, a dropped reused connection and a real `/summary` parse check
remain; the 2026-10-29 HTTP/1.0 timings above are not directly comparable.

### 2026-11-19 — SETUP-09, LES and REV-06 smoke (dev image)

Run on the same Cardputer ADV with the real account cache restored after each
synthetic check (`X`, reboot, full sync; Home 464 due / 115 lessons, SRS
A150 G143 M81 E147 B586).

- **SETUP-09.** Settings › Account showed `Wi-Fi scan`, `Setup portal` and
  `Sign out`; `Wi-Fi` opened a 2.4 GHz list with SSID, signal bars and `WPA2`
  (no 5 GHz rows; `Sakurakou Keionbu!`, `Shimokitazawa`, `TP-Link_A1B3` and the
  DIGIFIBRA APs). `Esc` returned to Account. The password screen showed the
  selected SSID, a masked field and `Tab: show`; no password was printed.
  Selecting Shimokitazawa with `wrongpass` showed `wrong password; try again`,
  kept the password editable and left the saved Shimokitazawa credentials
  intact (a later join succeeded). The successful on-device join/save, manual
  hidden SSID, boot flow, review lock and power guard remain.
- **LES-04/05/06.** Lessons showed `Order: WaniKani`; Right changed it to
  `Radicals>Kanji>Vocab` and again to `Interleave types`, and Left restored
  WaniKani. `Apprentice limit warning: 100` with A150 opened
  `WORKLOAD CHECK — Apprentice items: 150 (limit 100)`, `Enter: continue
  Esc: back`; `Esc` returned Home without a fetch. The first lesson card showed
  `1/5 p1/6`, `VOCAB L7`, `以前`, `Meaning`; page 2 was `READING` and page 3
  continued the reading topic. L5/L6, the 60-candidate ordering check and the
  radical/vocabulary page set remain.
- **REV-06.** With `Auto-advance on correct = 2 s` and the synthetic cache,
  answering `tonight` for 今夜 moved to the `READING` question without Enter.
  R8 was returned to Off. Back-to-back Off, minimal header and the
  wrong/warning/detail/pause/blank guards remain.
- **CFG-04.** `?` on Home opened the `home` table with Enter/L/O/S/T/P/Esc
  rows and `Esc: close`; the normal Home hint shows
  `Enter: reviews  L  S  Esc: menu` without clipping. On Settings, `?` showed
  Up/Down Move, Enter Open/next value, Left/Right Previous/next value and Esc
  Back/close. In a review answer field `?` opened the `reviews` table (Enter
  Check the answer, Backspace Delete a character, Esc Pause) without typing a
  question mark. The lesson/Outbox/Firmware tables, the swallow/close path and
  the portal/Wi-Fi refusal remain.
- **ONB-01.** The card still holds the v1 combined `glyphs.kgp`; Home, lessons
  and About render from it through the fallback. About shows both `Font:` and
  `Rad:` lines, and the status is now split by role so a combined pack does not
  repeat the other table. The split font/radical files, missing-radical boot
  and both upload paths remain.
- **ONB-02.** The station page (code 915461) showed `Font: 7269 glyphs,
  64/32/20 px`, `Radicals: 498 radicals`, `Check pack again`, `Upload radical
  pack` and `Card free: 29.5 GB`; `Download pack` was correctly absent with a
  ready font. Uploading the 56-byte `glyphs-bad-small.kgp` returned
  `Not a radical pack: pack: no radicals.`, kept the old 498 radicals and left
  serial `i` at `[pack] radicals: 498 radicals`. Six native manifest tests and
  the release `--pack` record cover the parsing side. The empty-card download,
  cut Wi-Fi, valid phone upload, `Esc` cancel and heap checks remain.
- **REV-05.** Pause showed the four rows `Continue / Wrap up / End session /
  Settings`. On the synthetic cache, after completing 今夜's meaning and
  opening Wrap up, the reading question stayed with `WRAP UP 1` and `1/1`. The
  multi-item prune, idempotence and summary checks remain.
- **REV-02.** Default WaniKani policy: `tonigh` for 今夜 showed CORRECT with
  `Close: tonight`; `tonite` (distance 3) correctly stayed NOT QUITE. The Strict
  path and the lesson quiz remain; R6 was returned to WaniKani after the smoke.
- **REV-10.** 15 native tests cover the repeat timer, the kana-aware Backspace,
  the clear path and the 64-byte cap; the hardware hold/clear/kana/paste checks
  remain.
- **REV-04.** With R7 On, a wrong `banana` for 今夜's meaning followed by `C`
  moved to the `READING` question; a wrong kana reading followed by `A`
  requeued the item and advanced to the next item (人) with no completion. The
  synthetic outbox was cleared before restoring the cache. The journal, warning,
  bar/help and practice checks remain.
- **REV-07.** No native test (routing only). With R9 On, a wrong `banana` for
  今夜's meaning opened `ITEM DETAIL 2/6`, `MEANING MNEMONIC`, with `Tab/Esc:
  back`; the synthetic cache was cleared and the real full sync restored. The
  reading page, return path and empty-mnemonic state remain.
- **SPEED-01.** Six native tests cover the policy values, the Every-5 boundary,
  the summary decision and the bounded StageTracker. The summary send loop,
  Esc-stop, offline failure, Every-5/Immediately and heap/canvas checks remain
  on hardware.
- **REV-11.** Six native tests cover the mistake filter, both-part counts, the
  empty list, stage text, selection wrap and next-batch availability. The
  three-row list, `R`, stage updates during the send loop and the 240×135
  layout remain.
- **REV-08.** Eleven native tests cover every order, type grouping, the limit,
  unknown levels and `availableAt`. R2 `Oldest available` (online, cached batch
  present) fetched the full due set: 8 pages, 228,077 B, 464 assignments,
  20 chosen, then a 20-item queue (`二斤`, `氏`, `出す`, …), with no reset. The
  first run aborted in `loadCachedBatch` with a `bad_alloc` while copying the
  52 cached subjects; moving them instead, not reserving the full due vector
  before the TLS handshake, and catching `bad_alloc` fixed it. R2 was returned
  to Account and the outbox was empty. The visible SRS/level/type order,
  fallback and cached-offline checks remain.
- **ARCH-04.** Six native tests cover the modal stack, sleep predicate and
  screen table. On hardware: Home, Settings, Pause (Continue/Wrap up/End
  session/Settings), About (`Font: 7269`, `Rad: 498`), the empty Outbox, the
  lesson list (`1/5`, five vocab rows) and the setup portal (`835570`) render
  normally. The full `Pause → Settings → System → About → Esc ×3` chain returned
  to Pause with Settings selected. Sleep/dim, firmware install, low battery and
  debug routes remain.
- **PD-01.** The `paperdiy` and `paperdiy-dev` targets compile with the 16 MB
  table, `qio_opi`, M5GFX 0.3.0 and the new board HAL (960×540, buttons, SD,
  PM1 power/sleep); `paperdiy` is 1,696,357 B of its 6,291,456 B slot. No
  PaperDIY hardware was available: panel/PSRAM/button/SD/PM1/EPD power and
  strapping-pin checks remain, as does PD-02 rendering.
- **SPEED-06.** Five native tests cover the P5/P6 policy and sleep slices. A
  bare dev `Z` entered the 10 s light-sleep test; the device was alive and
  syncing after the timer wake (`cmd h` answered, no boot). The post-wake
  serial line was lost on the USB CDC. Current draw, key/G0 wake latency,
  first-key, Wi-Fi rejoin, panel recovery, P4/P6 deadlines and USB remain.
  After that test the dev image kept treating the cable as battery power
  (`HWCDC::isPlugged()` can read false after a light sleep) and idled into
  sleep repeatedly, so `/dev/cu.usbmodem*` vanished for minutes at a time.
  Since `9290212` dev builds only light/deep sleep when `O` forces the battery
  path; release builds keep the `externalPower()` check, so the USB-vs-battery
  decision on a release image is still a hardware check.
- **TNG-03.** Fifteen native tests cover the list-window math and hint
  fallbacks. Phase 2 migrated Home hints, Settings, the Wi-Fi scan list, Outbox,
  the lesson list window and Study/critical windows to the shared primitives;
  the 20 px lesson/critical/study rows moved to `drawList` on 2026-11-20
  (TNG-03H): the lesson list captured byte-identical to the earlier
  ARCH-04/TNG-03 capture. (Correction: the first 'Study menu' capture that day
  pressed `s`, which is Sync; the menu is `P`. It was captured later and draws
  its four rows and counts correctly.) That comparison exposed an older bug: the
  lesson-list prompt boxes were empty because `drawTextCentered()` received
  the centre in the size slot; after the fix the panel shows 以/月/仕/金/映 in
  the boxes with the frames intact. The critical list, opened later the same
  day, showed two older bugs: with the subjects missing from the SD catalog and
  Wi-Fi not joined (a recent cache skips the boot sync) it said `No critical
  items` although the menu counted 7, and its 22×20 prompt boxes drew the full
  64 px missing-glyph block for multi-kanji words, spilling over the rows. Fixed:
  the list joins Wi-Fi on demand (7 subjects in 2 requests), says `N items could
  not be loaded` when it cannot, and draws the first character like the lesson
  list; the panel then showed 町/風/失/内/走/生/見 with percentages and the thumb.
  The menu's `Current level` was always 0 / `Current level complete`: the
  pool's `/assignments` entries have no lesson position and the selection
  dropped them. Fixed (position orders, it no longer filters); the menu then
  counted 1 and practice opened 住 (KANJI L8). Practice writes nothing. Home,
  Settings and the Wi-Fi list rendered with the same layout after flashing;
  the remaining window/scroll cases are in the table. Phase 3 moved wrapping,
  ellipsis and UTF-8 trimming into `TextLayout` and added the 1bpp icon tables;
  Home and a real review feedback (answer list, faint synonyms and a two-line
  hint with `...`) rendered correctly after flashing. UiFont replaced 151
  size-1 font pairs across the screens; a real review prompt typed through the
  matrix path still works. TNG-03 icons were adopted afterwards: Home shows the
  sync icon, About the Font/Rad/Token icons, and a real `NOT QUITE` feedback
  shows the cross; the pill/answer layout stays readable. The full icon sweep
  (Sync/Outbox/Wi-Fi/Setup, 1–3-digit counts, pill width) remains.
- **UI-08.** The new `kErr` (`#FF6D00`) is visible as the orange `Answer` and
  `NOT QUITE` on the real panel; the full gamma/contrast sweep across every
  status screen remains.
- **BT-02.** The source-neutral seam keeps matrix typing working in a real
  review (`zzz` reached the checker and produced the expected feedback);
  shortcuts/IME/Wi-Fi password and the BLE transport/HID reports remain.

## Round 2 hardware checks

Round-2 tasks use the same device tools as the v1 passes. Record each check
from a task's **Tests** line here instead of only in prose; the P0/P1 rows are
the gate: a task stays in `review` until they are `pass` (or the owner accepts
a `blocked` row with the reason). P2/P3 checks are indexed when a run covered
them or when the dated section names them as remaining; the v1 API/OTA/Wi-Fi
rows record what the 2026-11-19 pass closed. Rows start as `todo`; a run fills
the date and result and may add a short dated subsection below the table with
the serial log. Do not rewrite the dated sections above; link to them by date
instead.

| Task | Check | Build | Date | Result | Evidence |
|---|---|---|---|---|---|
| `API-01/05` | OUTBOX: rejected/stuck list, confirmation with attempts/age, discard one sequence | `cardputer-adv-dev` | 2026-11-19 | `pass` | 2026-11-19 v1 pass: `J` seeded a 422, a 409 and a 50-attempt stuck item; the screen read `HTTP 422`, `HTTP 409` and `gave up`; the confirmation showed `WaniKani rejected this review (HTTP 422). 1 attempt, age 8m.`; Enter discarded only seq 1 (2 items left). |
| `API-02` | Real 429: shared gate, `RateLimit-Reset` countdown, no immediate retry | `cardputer-adv-dev` | 2026-11-20 | `partial` | A real 429 (provoked by the FIX-14 forced-cap run): `[http] 429 rate limited: reset 1795169820, remaining 0, wait 19 s, clock ok`; the lesson fetch stopped with `No lessons available / subjects HTTP 429 / Enter: retry Esc: home` and made no further request until Enter; the manual retry after the window loaded the batch. The Home/boot countdown text and the shared gate across sync were not observed. |
| `API-03` | 401: shared gate arms; the next request is blocked before TLS | `cardputer-adv-dev` | 2026-11-19 | `pass` | `T` saved a dev token; boot `/v2/user` returned a real 401; log `401 token rejected; blocking requests until the token changes`; a following `u` logged `[http] /v2/user blocked by auth (401)` with no TLS request. |
| `API-03` | 403: endpoint-specific "missing permissions", not a global gate | — | — | `blocked` | Needs a read-only token (missing token permission); 403 was not run on 2026-11-19. |
| `API-04` | Retry a truncated or parse-failed 200 once; the retry is bounded | `cardputer-adv-dev` | 2026-11-19 | `partial` | Real `/v2/assignments?...&srs_stages=1,2,3,4` returned 200 with 32,768 bytes truncated; the log showed the retry and a second parse failure. The retry path ran; a successful retry was not observed. The practice-pool stage split fixed the over-cap fetch. |
| `FIX-24c` | Wi-Fi upload: `S` + `curl -T` 64 KB | `cardputer-adv-dev` | 2026-11-19 | `pass` | `S` plus `curl -T` wrote `/kanikey/test-upload.bin`; `[asset] done 65536/65536`. The pack-path power-cut check is separate. |
| `FIX-20` | Bad first boot / two-strike rollback | `cardputer-adv-dev` + `cardputer-adv-badapi` | 2026-11-20 | `pass` | Over the air from `tools/ota_local.py` (dev `U`/`F`/`I`). First run exposed a bug: the new image cleared the install record on its first boot, so after `r` the bootloader rolled back to 0.1.0 silently and no strike was counted. Fixed (record kept until `markValid()`). Re-run: `v0.1.2 installed over the air in app1`, `first boot … valid after an API success or 10 minutes`, every request `dev fail-api`; `r` → `v0.1.2 did not start in app1: back on 0.1.0`, `rollback count 1: still offered`; second install + `r` → `rollback count 2: blacklisted`, `not offering v0.1.2 again`, and `F` → `Previous update failed: v0.1.2`. Good path: a 0.1.1 dev image `marked valid after boot (API success)` 5.7 s after boot and stayed on 0.1.1 across a restart with no rollback lines. Restored 0.1.0 by USB, default manifest URL, record cleared. |
| `FIX-20` | Bad first boot / two-strike rollback | `cardputer-adv-dev` + `cardputer-adv-badapi` | 2026-11-20 | `pass` | Over the air from `tools/ota_local.py` (dev `U`/`F`/`I`). First run exposed a bug: the new image cleared the install record on its first boot, so after `r` the bootloader rolled back to 0.1.0 silently and no strike was counted. Fixed (record kept until `markValid()`). Re-run: `v0.1.2 installed over the air in app1`, `first boot … valid after an API success or 10 minutes`, every request `dev fail-api`; `r` → `v0.1.2 did not start in app1: back on 0.1.0`, `rollback count 1: still offered`; second install + `r` → `rollback count 2: blacklisted`, `not offering v0.1.2 again`, and `F` → `Previous update failed: v0.1.2`. Good path: a 0.1.1 dev image `marked valid after boot (API success)` 5.7 s after boot and stayed on 0.1.1 across a restart with no rollback lines. Restored 0.1.0 by USB, default manifest URL, record cleared. |
| `SPEED-01` | Submission policy: `Sending n/12`, Esc stop, pending count, next sync | `cardputer-adv-dev` | 2026-11-20 | `partial` | Synthetic cache + `devhost` to a local log server (no WaniKani writes). R12 `Immediately`: each completed item was posted at once (1005 at 16:23:16, 1004 at 16:23:26). After `d` (Wi-Fi off) the next four failed fast (`connect failed after 16 ms`, `HTTP 0, state 0`) and stayed queued; the summary read `6 REVIEWS 100% 1m`, `0 mistakes`, `4 pending`, `History 14 reviews, 86%`. `w` + `f` sent the four (`0 pending`); the server saw each of the six exactly once. The at-session-end send loop is covered by the REV-11 run (six POSTs, `All reviews sent`). Esc during `Sending n/N` needs a physical key (serial input is not read during the loop); `Every 5` not run. Esc (owner, 2026-11-20): synthetic session, `slow_server.py slow` (5 s per POST); four POSTs went out, Esc stopped the loop after the request in flight, summary `2 pending` (attempts 0); both cleared with `z` before any real sync. |
| `SPEED-02/03/04` | Quick/full sync, non-modal Home, prefetch | `cardputer-adv-dev` | 2026-11-19 | `partial` | Auto quick sync logged the step transitions, `/v2/summary` and `[sync] complete`; a recent cache skipped the sync; the first on-demand lesson/review fetch joined Wi-Fi. Remaining: cold vs deep-sleep request list/time to Home, Home `sync n/m`, `Esc`/`Enter` mid-sync, a full auto sync with prefetch, untrusted clock. |
| `SPEED-05` | Keep-alive/chunked transport; idle-age underflow fix | `cardputer-adv-dev` | 2026-11-19 | `partial` | Manual `S` sync: one session, `[http] session close after 35 requests`, `reuse` and `chunked keep`; `/v2/subjects?ids=419` returned 200, 2,199 B; min free heap 22,836 B; `[power] blank after 60002 ms idle`. Remaining: TLS spike numbers, a dropped reused connection, an explicit `/summary` parse check. |
| `SPEED-07` | Home due count/next review updates within 60 s | — | — | `todo` | Two native Summary tests; the 60 s Home tick and the offline-clock line still need the hardware run (2026-11-19 v1 pass). |
| `CFG-01` | Eight sections, open/back, cycle both ways, review lock, scroll, origin | `cardputer-adv-dev` | 2026-11-19 | `partial` | Home → Esc showed eight sections with counts (`Reviews 12`, `Lessons 6`, …); Reviews showed 12 rows; `Batch size` cycled 20 → 30 with Right and back to 20 with Left, with the help line. Remaining: `Esc`-back chain, wrap at both ends, review lock, scroll thumb, origin return. |
| `CFG-02` | Registry persistence, defaults, R4 upgrade | `cardputer-adv-dev` | 2026-11-19 | `partial` | 47 rows and eight sections are live in the menu; one value (`Batch size`) cycled. Remaining: one change per section plus a power cycle, a fresh-NVS default check, and the v0.1.0 `order` → `rv.first` upgrade. 2026-11-20: R5 Off and R10 Minimal persisted across a reboot. |
| `CFG-03` | Reset: second Enter, Esc cancel, keeps account, brightness, review lock | `cardputer-adv-dev` | 2026-11-19 | `partial` | Two Enters showed `Settings reset`; token/Wi-Fi/cache/outbox survived. Remaining: brightness/timers after reset and reset during a review. 2026-11-20: first Enter shows `Reset settings? Enter again, Esc cancel`; Esc cancels (and also leaves the System section); the second Enter showed `Settings reset` and Batch size 30→20, Back-to-back Off→On, Header Minimal→SRS+accuracy, R12 Immediately→At session end; Wi-Fi, token, cache and outbox kept. |
| `CFG-04` | Help on Home/Settings/feedback/lesson/Outbox/Firmware; Esc closes; hint width | `cardputer-adv-dev` | 2026-11-19 | `partial` | `?` on Home opened the seven-row `home` table with `Esc: close`; Settings showed Up/Down/Enter/Left/Right/Esc; in an answer field `?` opened the `reviews` table without typing `?`; the Home hint `Enter: reviews  L  S  Esc: menu` fits. Remaining: lesson/Outbox/Firmware tables, the swallow/close path, plain `/` paging, portal/Wi-Fi refusal. |
| `REV-01/03` | Feedback: all accepted answers/synonyms, grouped readings, hint line, lesson quiz | `cardputer-adv-dev` | 2026-11-20 | `partial` | Real review, wrong answers only, session dropped by a reboot (outbox 0). 何月 `month`: `NOT QUITE`, `You typed month`, `Answer What Month / Which Month` (all accepted, wrapped). Reading `gatsu`: `Y がつ` / `A なんがつ`. 南 `zzz`: `Answer South` plus a two-line meaning hint ending in `...`; reading `kana`: `Y かな` / `A みなみ` (its only accepted reading) plus the reading mnemonic line. A kanji with several accepted readings (on/kun grouping) and the lesson-quiz variant were not seen. |
| `REV-02` | WaniKani vs Strict typo policy | `cardputer-adv-dev` | 2026-11-19 | `partial` | Default: `tonigh` for 今夜 was CORRECT with `Close: tonight`; `tonite` (distance 3) correctly stayed NOT QUITE. Strict path and lesson quiz not run; R6 was returned to WaniKani. |
| `REV-04` | C/A on Incorrect; warning; bar/help | `cardputer-adv-dev` | 2026-11-19 | `partial` | R7 On: wrong `banana` for 今夜's meaning + `C` moved to the `READING` question; a wrong kana reading + `A` requeued the item and advanced to 人 with no completion. Journal, warning, bar/help and practice checks remain. |
| `REV-05` | Pause four rows; wrap-up prune/count/summary | `cardputer-adv-dev` | 2026-11-19 | `partial` | Pause showed `Continue / Wrap up / End session / Settings`; after completing 今夜's meaning, Wrap up kept the reading with `WRAP UP 1` and `1/1`. Multi-item prune, idempotence and summary checks remain. |
| `REV-06` | Auto-advance delay, typed-key advance, guards | `cardputer-adv-dev` | 2026-11-19 | `partial` | R8 `2 s`: answering `tonight` for 今夜 advanced to the `READING` question without Enter. Back-to-back Off, minimal header and the wrong/warning/detail/pause/blank guards remain. |
| `REV-07` | Wrong meaning/reading opens the matching detail page; return | `cardputer-adv-dev` | 2026-11-19 | `partial` | R9 On: a wrong `banana` for 今夜's meaning opened `ITEM DETAIL 2/6`, `MEANING MNEMONIC`, with `Tab/Esc: back`. Reading page, return path and empty-mnemonic state remain. |
| `REV-08` | Order fetch/selection, visible order, fallback, cached | `cardputer-adv-dev` | 2026-11-19 | `partial` | R2 `Oldest available` fetched the full due set: 8 pages, 228,077 B, 464 assignments, 20 chosen, then a 20-item queue (`二斤`, `氏`, `出す`, …), with no reset. Visible SRS/level/type order, fallback and cached-offline checks remain. |
| `REV-09/12` | Back-to-back Off; minimal header | `cardputer-adv-dev` | 2026-11-20 | `pass` | R5 Off + R10 `Minimal`: the header showed only `REVIEWS` and `1/20` (no `SRS n %`); after 何月's meaning the next prompts were 門 and 小文字 meanings, not 何月's reading (with R5 On the reading follows at once, seen in REV-10/REV-11). Both values survived a reboot and were restored by the reset. |
| `REV-10` | Backspace repeat, Fn/Ctrl clear, kana units, 64-byte cap | `cardputer-adv-dev` | 2026-11-20 | `pass` | Real review, 学 reading prompt through the matrix IME: `tabe` showed `たべ`, Backspace left `た` (romaji `ta`); `kann` gave `たかん`, Backspace left `たか` (romaji `taka`). Meaning prompt: `tabe` + Backspace left `tab`. Only wrong answers were typed and the session was dropped by a reboot; outbox stayed 0. Held-Backspace repeat, Fn/Ctrl+Backspace clear and the 64-byte cap (the box scrolls, so the length is not visible) remain; serial `K` cannot hold keys or add modifiers. 2026-11-20 by the owner on the keys: held Backspace deletes one, pauses, then repeats until empty and stops on release; Fn+Backspace and Ctrl+Backspace each cleared the field. |
| `REV-11` | Mistakes list, navigation, `R` next batch, stage updates, layout | `cardputer-adv-dev` | 2026-11-20 | `partial` | Synthetic 6-item cache (`C`) with `devhost` to a local `slow_server.py log` (no WaniKani writes; six `POST /v2/reviews` with assignments 1001–1006 reached the test server, each `200 {}`). Summary: `6 REVIEWS 61% 5m`, `No stage changes`, `7 mistakes`, `All reviews sent`, `Enter: home` and no `R: next batch` (0 due). Tab opened `1-3/3`: 人 `person M0 R1` じん, 写真 `photograph × zzz` しゃしん, 今夜 `tonight × zzz` こんや, each `pending` (the test server returns no stage); Down moved the focus, Esc returned to the stats, Enter went Home. The stats footer did not name Tab; it now reads `Enter: home  Tab: mistakes` (not yet seen on the panel). `R` with due remaining and real `SRS a -> b` stages remain. Side note: `konnya` types こんにゃ by design (`n'` before y, FIX-05). |
| `LES-01` | Browse a 5-card batch with all keys; no `[lesson] start` | `cardputer-adv-dev` | 2026-11-19 | `partial` | `L` fetched 5 lessons and showed the `1/5` list; Enter opened the `1/5 p1/4` card; no `[lesson] start`. The full page walk with Space/Left and the five-card sequence were not recorded. |
| `LES-02` | Batch quiz of 5 with two mistakes; requeue; no outbox growth | `cardputer-adv-dev` | 2026-11-20 | `pass` | Real 5-lesson batch (以前, 月末, 仕組み, 金魚, 映画), all cards paged, `[lesson] quiz 5 items`. Two mistakes on 映画 (`zzz` meaning → `NOT QUITE` + cross, `You typed zzz`, `Answer Movie / Film`; `かな` reading). The other four completed in turn (`2/5`…`4/5`); 映画 came back last as `5/5` and passed (`CORRECT` + check). Enter opened `Start 5 lessons? / They join your reviews in about 4 hours`; Esc left without a `PUT /start` (none in the log). Outbox stayed `0 items`, Home still 115 lessons. |
| `LES-03` | Start 5 on Wi-Fi, offline, already-started 422 | `cardputer-adv-dev` | 2026-11-19 | `partial` | A later real batch sent 5 `PUT /start`, all 200; Home dropped from 120 to 115 lessons. Offline start, 422 and already-started-elsewhere were not run; the confirmation/progress text was not recorded. |
| `LES-04` | L2 order; fresh level shows the radical first, whole unlock reachable | `cardputer-adv-dev` | 2026-11-19 | `partial` | Lessons showed `Order: WaniKani`; Right changed it to `Radicals>Kanji>Vocab` then `Interleave types`; Left restored WaniKani. Fresh-level radical-first and the 60-candidate checks remain. |
| `LES-05` | L4 threshold screen + Esc; L5/L6 and multi-reason | `cardputer-adv-dev` | 2026-11-19 | `partial` | L4 100 with A150 opened `WORKLOAD CHECK — Apprentice items: 150 (limit 100)` and `Enter: continue  Esc: back`; Esc returned Home without a fetch. L5/L6 and the multi-reason guard remain. |
| `LES-06` | Kanji/radical/vocab topic pages, long mnemonic, `p` counter | `cardputer-adv-dev` | 2026-11-19 | `partial` | First VOCAB card `1/5 p1/6`, `VOCAB L7`, 以前, `Meaning`; page 2 `READING` and page 3 continued the reading topic. Kanji/radical card sets and the Context page remain. |
| `SETUP-09` | Wi-Fi scan, masked password, join/save/failure, hidden SSID, boot, lock | `cardputer-adv-dev` | 2026-11-19 | `partial` | 2.4 GHz list with SSID/signal/`WPA2`; `Esc` returned to Account; password masked with `Tab: show`; `wrongpass` for Shimokitazawa showed `wrong password; try again` and kept the saved network. Successful join/save, hidden SSID, boot flow, review lock and power guard remain. |
| `ONB-01` | Split packs, v1 fallback, missing radical, About two lines | `cardputer-adv-dev` | 2026-11-19 | `partial` | The card still held the v1 combined `glyphs.kgp`; Home, lessons and About rendered through the fallback; About showed both `Font:` and `Rad:` lines split by role. Split `glyphs.kgp`/`radicals.kgp`, missing-radical boot and both upload paths remain. |
| `ONB-02` | Portal download/upload, failure/cancel, heap | `cardputer-adv-dev` | 2026-11-19 | `partial` | Station page (code 915461) showed `Font: 7269 glyphs, 64/32/20 px`, `Radicals: 498 radicals`, `Check pack again`, `Upload radical pack` and `Card free: 29.5 GB`; `Download pack` was absent with a ready font. A 56-byte `glyphs-bad-small.kgp` returned `Not a radical pack: pack: no radicals.` and kept the 498 radicals. Empty-card download, cut Wi-Fi, valid phone upload, truncated upload, `Esc` cancel and heap checks remain. |
| `FIX-06` | Slow/blackholed server read timeout; no resend | `cardputer-adv-dev` | 2026-11-20 | `pass` | Dev `devhost` to `tools/slow_server.py` on the LAN (certificate checks off, dev only). `stall`: `t` connected in ~520 ms and reported `no status line` 19.4 s later (not 1 s); the GET was retried once, as API-04 allows. `x` + `f` sent one synthetic `POST /v2/reviews` (assignment 999999999): the server logged exactly one POST, the device waited 19.6 s, logged `HTTP 0, state 2`, kept it pending and did not resend. `slow` (headers at 3 s, body 1 B/s): `200, 17 body bytes, 20001 ms (truncated)`, then one GET retry. `devhost off` and `z` restored the state; outbox 0. |
| `FIX-07` | AP key: new random 10-character key per portal start | `cardputer-adv-dev` | 2026-11-06 | `partial` | Two boots produced `KaniKey-AAC8` with two different random passwords; the new-key join was not separately recorded. |
| `FIX-07` | 15-minute idle AP close | — | — | `todo` | Not run (2026-11-06); the device has a saved network, so the portal opens in station mode. The idle AP needs a separate setup. |
| `FIX-14` | Real 32 KB truncation: fetch stages separately, save all 145 | `cardputer-adv-dev` | 2026-11-19 | `pass` | Real 32,768 B truncation on `srs_stages=1,2,3,4`; fetching stages 1–4 separately saved all 145 (12/28/54/51) with 5,768–26,157 B bodies. The subject-page 5→2→1 fallback was not triggered. |
| `FIX-14` | Forced small cap: 5→2→1 fallback | `cardputer-adv-dev` | 2026-11-20 | `pass` | Real GETs with dev `cap`. First run (`cap 5000` + `l`): the fallback worked but never stepped back up and API-04 re-sent every cap-truncated page, ~100 requests → real `429`. After the fix (`capOverflow` not retried, `wk/PagePlan` recovery): lessons logged `cap overflow, not retried`, assignment pages 30→10 with a 20 probe, level pages 10→5→2→1 then probes back to 2/4; 72 requests in 51 s still met a 429 because at 5,000 B every level page holds 1–2 subjects and the boot sync had used part of the minute (not reachable with the real 32 KB cap). Review batch (`H`, `cap 12000`, `b`): subject pages 5→2, back to 4 and 5, 5→2 again, 4, 3 — 20/20 subjects in 23 requests, no 429. The 20-subject assignments request has no fallback (it overflowed at `cap 5000`); at the real cap it is ~10 KB. |
| `RAD-04` | Radical-only lesson on a low-level account | `cardputer-adv-dev` | 2026-11-06 | `blocked` | Synthetic `L` → `RAD L1 ground` card and quiz rendered the radical bitmap. A real low-level account is not available, so the real first-lesson run is blocked. |
| `UI-05` | Real level-up notice | — | — | `todo` | No level-up occurred in the 2026-10-29 run; the `Level up!` transition still needs a real level change. |
| `SPEED-06` | Light sleep: key wake mid-review, current, G0, first key | `cardputer-adv-dev` | 2026-11-20 | `partial` | `Z` 10 s light-sleep test woke on the timer and kept syncing; the post-wake CDC line was lost and `isPlugged()` read false afterwards, so dev builds now sleep only with `O`. Key/G0 wake latency, first key, current and the release USB detection remain. |
| `REM-01` | Timer wake, wake cost, tone heap, LED | `cardputer-adv-dev` | 2026-11-20 | `partial` | `rem 20 3`: three timer wakes, `slept_ms 20545` each, probe wake boot→sleep 544 ms with no Wi-Fi; first `playTone` keeps 7,880 B of heap (largest unchanged), a second tone none. The record moved to `RTC_NOINIT_ATTR` after the USB-open reset wiped an `RTC_DATA_ATTR` one. Sleep current (needs a meter) and the EXT1 key abort remain. Details in `docs/spec/08-reminders-audio.md`. |
| `FLASH-01` | `fs` LittleFS usage; scanf-free date parse | `cardputer-adv-dev` | 2026-11-20 | `pass` | After a full sync on the real account: LittleFS 4,653,056 B total, 94,208 B used (2.0%); `/state` 74,888 B (`journal.bin` 65,032, `practice.bin` 3,620, `cache.bin`/`.bak` 3,064 each, `critical.bin` 76, `outbox.bin`/`.bak` 16 each). Home after the `sscanf` removal: `Synced 12:53`, `L8 3% since 03 Feb`, `Next review now`, 469/115. |
| `FLASH-03` | 8 MB table 2 × 3 MiB + 1.94 MiB LittleFS; NVS kept; blank format; OTA into new app1 | `cardputer-adv-dev` | 2026-11-20 | `pass` | erase_region 0x610000 0x1F0000 + USB upload: Wi-Fi/token survived (sync 469/115), slot 3,145,728 B; blank LittleFS formatted silently in 42 ms; `fs` 2,031,616 B total; badapi 0.1.2 installed into app1 in 12.7 s and rolled back with strike 1. Stale-bytes path not run. |
| `FLASH-02` | Release image (`CORE_DEBUG_LEVEL=0`) on the device | `cardputer-adv` | 2026-11-20 | `pass` | Release build flashed by USB: boot, Wi-Fi in 0.8 s, sync 469/115 and Home as in dev; the Arduino `[E]` library lines (e.g. `vfs_api`) are gone, only the prebuilt ESP-IDF `esp_core_dump_flash` line remains. Dev image restored afterwards. |
| `REL-0.2.0` | Published v0.2.0 release: assets, manifest, OTA from GitHub | `cardputer-adv-dev` 0.1.0 → release 0.2.0 | 2026-11-20 | `pass` | Tag `v0.2.0` on `108d67d`; the release workflow published six assets (`kanikey-cardputer-adv.bin` 1,666,016 B, `glyphs.kgp` 9,358,845 B, manifest, `SHA256SUMS`, `LICENSE`, `THIRD_PARTY_NOTICES.md`) with the curated notes as the body; `SHA256SUMS` verified. On the device (dev 0.1.0, new table, default manifest URL) `F` → `v0.2.0 available (1.6 MB), http 200`; `I` downloaded it from `releases/latest/download` in 18 s into `app1`, booted `0.2.0 … in app1`, `marked valid after boot (API success)` 8 s later, synced 469/115. Dev 0.2.0 reflashed by USB afterwards. The Settings › Firmware `Up to date` screen and **Download pack** from this release were not run. |
| `REM-02` | Reminder re-sleep, banner, key → quick sync | `cardputer-adv-dev` | 2026-11-20 | `pass` | With the owner at the device, dev `remind 30` six times on the real cache (469 due): every timer wake landed on the expected second (`drift 0 s`) with `due 469 banner yes`. No key in 30 s → `outcome expiry`, dark re-sleep until a key (runs 1–4); a key while asleep → `ended_by key`, normal boot (run 5); a key while the banner showed → `outcome key`, Home continued (run 6). Finding: the one-line accent banner read as the normal Home and was missed twice; it is now a full-width accent band (`469 reviews ready`), which the owner saw at once. Not run: M3 daily time, quiet hours at night, battery energy. |
| `REM-03` | Answer/session/reminder cues, volume, heap | `cardputer-adv-dev` | 2026-11-20 | `pass` | Dev `cue chime|correct|wrong|end` all played through the speaker (260/190/120/380 ms, level 128); `cue vol low|high` played at 64/255. The first cue kept 7,880 B (104,340 → 96,460 B free; largest 77,812 B unchanged), later cues none. Nobody listened, so audibility per volume, input lag with M4 On in a review and the no-tone-in-sync/portal checks remain. 2026-11-20 with the owner listening: all six cues heard (chime, correct, wrong, end, low and high volume). Input lag with M4 On in a review and the sync/portal guard remain unobserved. |
| `AUD-02/03` | Batch clip prefetch, MP3 playback, heap, stack | `cardputer-adv-dev` | 2026-11-20 | `partial` | Lesson batch fetch → `[audio] batch prefetch 5/5 clips, 107955 bytes, 3784 ms` (16–28 KB MP3s; it had skipped at 27,636 B largest until the canvas was released for it). Lesson card reading page shows `Q quiz  R: audio`; `R` played `2940 voice 0 (1 ch)`: `alloc 31616` (state 6,672 + scratch 16,240 + window 4,096 + 2×2,304 PCM), heap 48,880/29,684 during, `freed` after ~1.0 s for a ~1 s clip. A second `R` was refused at largest 22,516 B before the state/scratch split; after it, three `R` in a row all played and the heap came back to 77,016/45,044 B each time; loop stack free 15,476 B. Not heard by anyone (no listener); per-frame decode time, 100-cycle soak, review feedback/detail `R`, OGG-only/missing clips and current draw remain. M6 autoplay (2026-11-20): with `Autoplay audio: On` the first lesson card played `2940` once without `R` (`alloc 31616`, heap 41,116/20,468 during, `freed` ~1 s later) and paging or going back did not replay; M6 set back to Off. Owner listening 2026-11-20: 以前 played twice from the lesson card, clear and without dropouts. |
| `BT-01` | BLE HID host spike: pairing, reconnect, BLE-on-TLS heap | — | — | `todo` | Desktop probe done (NimBLE-Arduino 2.5.2 conditional go, compiled sizes in `docs/spec/10-bluetooth-paperdiy.md` §1.7); BT-02 seam merged and matrix typing re-checked. No BLE keyboard has been paired; the hardware run needs one. |
| `PD-01…04` | PaperDIY board, 4× e-paper render, two-button UI, sleep cadence | — | — | `todo` | Code merged and `paperdiy`/`paperdiy-dev` build (2026-11-20); no PaperDIY hardware available, so every §2.6 check in `docs/spec/10-bluetooth-paperdiy.md` remains. |

Rows marked `partial` are not passes: the Evidence cell and the dated section
name what is still open. The table is an index; the dated sections above remain
the narrative.

- **Result:** `todo` (not run), `pass`, `partial` (some checks in the row ran;
  the rest of the Tests line is still open), `fail` or `blocked` (state the
  missing hardware/account/permission). Status names follow
  [docs/spec/README.md](spec/README.md) §6: a task stays `review` until its
  P0/P1 checks are done.
- **Evidence:** a serial line (`[pack]`, `[cache]`, `[ota]`, `[sync]`, …), a
  `tools/shot.py` image under `tools/screenshots/out` (not committed), or a
  committed `docs/screens` render/capture. Say which.
- **Run it with:** `tools/device.py monitor` (serial), `tools/device.py cmd`
  (one command) and `tools/device.py upload` (asset upload). Capture a canvas
  with `tools/shot.py --out /tmp/kk.png`.
- **Sources:** P0/P1 task **Tests** lines in [docs/spec/01-fixes.md](spec/01-fixes.md),
  [02-speed.md](spec/02-speed.md), [03-settings.md](spec/03-settings.md),
  [04-reviews.md](spec/04-reviews.md), [05-lessons.md](spec/05-lessons.md),
  [06-study.md](spec/06-study.md), [07-stats.md](spec/07-stats.md),
  [08-reminders-audio.md](spec/08-reminders-audio.md) and
  [09-docs-onboarding.md](spec/09-docs-onboarding.md). Add the row when the
  task's Tests line is final.
- **Historical sections:** the 2026-10-25 … 2026-11-19 sections above remain
  the record of those runs; this table only indexes round-2 checks.


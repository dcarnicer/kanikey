# KaniKey roadmap

Status snapshot: **2026-11-19**. This is the working backlog after the offline
cache, the `efontJA_16` removal and the first full v1 hardware pass landed, not
the original product plan. Product scope and UX live in [docs/design.md](docs/design.md),
[docs/ux.md](docs/ux.md) and [docs/api-sync.md](docs/api-sync.md).

Round 2 (branch `spec/round-2`, 2026-11-01): the fixes from the 2026-11-01
review are tracked in [docs/spec/01-fixes.md](docs/spec/01-fixes.md). 23 of 28
are done and verified on hardware (some with the dev-only simulations in the
wave 3 log); FIX-06, 07, 14, 20 and 24 still need the network/OTA and Wi-Fi
upload checks listed there. FIX-07's random AP key and FIX-24 (a)/(b) have
passed; FIX-24 (c) is partial. The rest of round 2 is specified in
[docs/spec/README.md](docs/spec/README.md); the per-task status tables there
are authoritative for that work.

The review loop works on hardware. The 2026-11-19 pass closed the deliberate
offline boot, the final OUTBOX screen, the 401 gate, the parse-retry path, the
Wi-Fi upload and the OTA good-install path. The main v1 gaps are the last
lesson hardware checks and the lesson-ordering limitation, API-02 429 and
API-03 403, the OTA bad-first-boot/two-strike rollback, and the
slow-server/forced-body-cap FIX checks. OTA, the MIT license and the release
tooling are done; the cache batch is loaded lazily. The persistent offline
cache restores Home, the user and a cached review batch after a reboot without
network; first boot uses an on-device setup portal, and Settings covers
display/power/study preferences plus sync, firmware, cache and sign-out.

## Where we are

Working on hardware:

- Home from `/user` + `/summary`, with due reviews, lessons, next review and
  outbox status.
- 20-item review session with typed meaning/reading, romaji-to-kana, answer
  checking, feedback and requeue. The session follows WaniKani
  `reviews_presentation_order` (`shuffled` or `lower_levels_first`); the local
  `Order` setting still selects meaning-first or reading-first.
- `POST /v2/reviews` with a persistent LittleFS outbox. The real-account path
  online fetch → Wi-Fi off → review → reconnect → 201 is verified; the
  deliberate offline reboot and a cached review session were verified on
  2026-11-19.
- 429 handling: `RateLimit-Reset` / `RateLimit-Remaining` are parsed and one
  shared gate blocks every request until the reset; Boot, Home and the sync
  error line show the remaining seconds. This is native-tested; a real 429 was
  not provoked on hardware on 2026-11-19.
- 401/403 handling and parse-failure retries are implemented and native-tested;
  the 401 gate and the retrying GET helpers were verified on hardware on
  2026-11-19, while the 403 half is still pending. Rejected/stuck outbox rows,
  their attempt/age detail and dismissal are native-tested and were verified on
  the final OUTBOX screen on 2026-11-19.
- Lessons: a list/picker with character, type, level and main meaning, sorted
  by level and `lesson_position`; the card shows meaning/reading, paged
  mnemonic, component meanings and hints, and `PUT /assignments/{id}/start`
  returns 200. The list, card and quiz were hardware-verified with GETs; the
  card's `PUT /start` returned 200 in an earlier card run, not in that
  list/picker pass.
- Lesson quiz: `Q` on a lesson card opens an optional local meaning/reading
  quiz with typed answers, incorrect-answer feedback, retries and a completion
  screen. It is read-only: the quiz itself never calls `/start`; Enter on the
  card still starts the lesson. Hardware-verified; the completion screen says
  `No /start sent.`
- Study materials: `GET /study_materials` is fetched for review and lesson
  subjects, user synonyms are accepted by the answer checker, and meaning/
  reading notes appear on the lesson card and review feedback. They are part of
  the cached subject, so they work offline too.
- Vacation mode: `current_vacation_started_at` is parsed and cached, and Home
  shows a non-blocking `Vacation mode` notice when nothing more urgent is
  displayed.
- SRS indicator: the review header shows the current stage (`SRS 3`) and the
  session summary shows the stage returned by `POST /reviews` (`SRS 3 -> 4`).
  The parser is native-tested; the header was verified on hardware with a
  synthetic cached review.
- Review statistics: `/review_statistics` is fetched for review subjects and
  merged into the cached subject. The review header shows lifetime accuracy
  (`SRS 1 86%`) and the session summary combines the stage from the last
  successful `POST /reviews` with the last completed item's history
  (`History 18 reviews, 89%`).
- Level progress: `/level_progressions` and two kanji count requests are
  fetched on sync and cached. Home shows `L8 42% since 03 Feb` in the
  low-priority status line and a session `Level up! L8 -> L9` notice after a
  level increase. The `/level_progressions` request returned 200 on hardware;
  the percentage and real level-up transition still need a hardware pass.
- SRS distribution: sync fetches five `/assignments` buckets by `srs_stages`
  and reads `total_count` only; Home draws the Apprentice/Guru/Master/
  Enlightened/Burned bar with counts. The counts are cached, so the bar works
  offline. The five requests add ~350 B each to a sync and are skipped after a
  sync error.
- Sync screen: a dedicated six-row screen (`Wi-Fi`, `Outbox`, `Account`,
  `Summary`, `Level`, `SRS`) shows values and `ok` / active / warn / fail
  states with a progress bar. `Esc` cancels at the next step boundary; failure
  stays on screen with retry (or setup for auth) and the completed data is kept.
- Item detail: review batches now keep mnemonics/hints and fetch their
  component subjects; `Tab` on feedback opens a detail card (meanings/readings/
  synonyms, meaning mnemonic, reading mnemonic, notes, components). Vocabulary
  adds a page with parts of speech and context sentences, and related subjects
  add a page with similar/amalgamation counts and cached meanings. The detail
  screen was hardware-verified with a synthetic cached review.
- SD visual pack: the card now has the token pack with **7,269 glyphs + 498
  radical bitmaps** (3,698,476 B), uploaded over USB on 2026-10-27. Wi-Fi
  asset upload also works.
- Radical prompts render from the pack's 96 px bitmaps; radical-only lessons
  and reviews are no longer filtered out. `efontJA_16` is gone: missing glyphs
  show an explicit state instead. A real radical review prompt (radical 舌) was
  verified on hardware on 2026-10-28; a radical-only lesson still needs a
  low-level account.
- Offline cache: Home counts, the user and the cached review batch are
  restored at boot without network. Assignments live in LittleFS
  (`/state/cache.bin`), subjects on SD (`/kanikey/cache/subjects.bin`), with
  version, token fingerprint, CRC32 and `.bak` recovery. Completed reviews are
  pruned from the cache and stay in the outbox.
- Setup portal: first boot brings up a WPA2 `KaniKey-XXXX` access point with
  the password on screen. The phone saves the home Wi-Fi; the device joins it
  and serves the second page at its station IP, where the token is validated
  with `GET /user`, the browser sets the clock/time zone and the pack can be
  checked again. Setup only reads `/user` during validation; it never starts lessons.
  The normal first sync afterwards may flush the existing outbox.
- Pause and settings: `Esc` in a review opens Continue / End session /
  Settings; `Esc` from Home opens Settings and `S` starts the same manual sync
  as Settings › `Sync now`. Brightness, dim/blank/sleep timings, Setup portal,
  Sync now and About are live; values persist in NVS.
  About shows version, build, pack, cache, last sync, Wi-Fi, token state, zone
  and heap.
- User-facing boot states for missing SD/pack, token or Wi-Fi and sync
  failures, with `Enter` retry. The M0 debug screen is dev-build only.
- 8-bit RGB332 canvas (32.4 KB), static 32 KB HTTP body buffer, dim/blank/deep
  sleep, screenshot capture.
- 77 native tests and the `cardputer-adv` build run in GitHub Actions alongside
  gitleaks on pushes to `main` and on pull requests; the release workflow adds
  the manifest tests and a tag-driven GitHub release.
- OTA uses the second 1.75 MiB slot with a size/SHA-256 manifest and
  bootloader rollback; Settings › Firmware and About expose it.

Current build: **1,556,989 B / 1,835,008 B flash (84.8%)**, **96,248 B /
327,680 B static RAM (29.4%)**. The dev image is 1,622,625 B (88.4%), 96,336 B
RAM. Removing `efontJA_16` freed ~468 KB and leaves ~272 KB of flash headroom.
The loop task uses a 24 KiB stack; the lazy review batch keeps the lesson-fetch
minimum free heap above ~44 KB in the 2026-11-19 logs.

## Priority summary

| Priority | Workstream | Why | Status | Start with |
|---:|---|---|---|---|
| 1 | Radicals and `efontJA_16` removal | Early levels and lessons are incomplete; frees ~468 KB flash | Done except a real low-level radical-only lesson; the synthetic radical lesson UI passed | RAD-04 |
| 2 | Persistent offline cache | The outbox survives reboot; cached Home/batch now do too | Done: real-account online fetch, deliberate offline reboot and cached session verified (2026-11-19) | CACHE-07 |
| 3 | Complete lesson flow | List/picker, card, components, quiz, empty state, start and ordering exist; the round-2 browse → quiz → batch-start flow is in review | v1 flow complete; LES-01/02/03 in review 2026-11-19 (hardware pending); LES-04/05/06 and the real low-level run open | LES-04 |
| 4 | WaniKani data and preferences | Synonyms, notes, review order, vacation, SRS, review stats, level progress, vocabulary details and related subjects are live; stats and local review history are live; audio remains | Partial; round 2 specified in `docs/spec` (STATS/AUD) | AUD, STATS |
| 5 | Outbox and API robustness | Rejected/stuck items, the 50-attempt cap, 429 backoff, parse retries, auth gate and Home/Settings manual sync are in | Code and docs done; OUTBOX (API-01/05), 401 (API-03) and parse retry (API-04) verified 2026-11-19; 429 (API-02) and 403 still pending | — |
| 6 | Setup, settings and pause | Setup, pause, settings, low battery and error screens work | Done for v1; on-device Wi-Fi setup is v1.1 (SETUP-09) | SETUP-09 |
| 7 | UI, stats and sync | Accuracy, the SRS bar, sync screen, item detail, level progress and the Stats screen are live | Partial | UI-08 |
| 8 | Tangocho modules | Reuse the portal, OTA, widgets, storage and sleep plumbing | Portal, OTA, storage and sleep done; widgets pending | TNG-03 |
| 9 | Technical debt, CI and release | Maintainability and shipping | Done (TECH-01…06, TECH-08); TECH-07 only if needed | TECH-07 |
| 10 | Kakehashi-inspired extras | Valuable after v1; not all fit a keyboard device | EXT-01…05 done; audio, reminders and stroke order remain; round 2 specified in `docs/spec` | STUDY |
| 11 | Bluetooth keyboard | Optional full-size input; BLE-only, shares the 2.4 GHz radio and the no-PSRAM heap | Post-v1; needs the BT-01 host-stack spike | BT-01 |
| 12 | M5Stack PaperDIY port | Desk e-paper companion; 960×540, no keyboard/touch, two buttons | Post-v1; read-only companion first, full reviews need BT | PD-01 |

## Execution order

Radicals are code-complete except for the real low-level lesson; the offline
cache and its deliberate reboot are verified. The lesson flow is code-complete
except for the real low-level checks; LESSON-08 now sorts a 60-item candidate
pool by level and `lesson_position`. OTA, the license and the release tooling
are done. The 2026-11-19 pass closed the OUTBOX, 401, parse-retry, Wi-Fi upload
and OTA good-install checks. What remains is API-02 429, API-03 403, the
slow-server/forced-cap FIX checks, the OTA bad-first-boot/two-strike path, the
real low-level lesson and a real level-up.

| Order | Phase | Tasks | Outcome |
|---:|---|---|---|
| 0 | Close radicals (parallel) | RAD-04 (RAD-02 done) | Real radical prompts and a radical-only lesson |
| 1 | Offline cache | CACHE-05 → CACHE-07, TNG-04, TNG-05 | Review and Home work after reboot without network (offline boot verified 2026-11-19) |
| 2 | Complete lessons | LESSON-01 → LESSON-08, WK-02, TNG-03 | v1 flow complete; LES-01/02/03 batch flow in review (hardware pending) |
| 3 | Setup and device UX | TNG-02 (done) | OTA release path (setup/settings complete) |
| 4 | WaniKani data and detail | WK-11 (WK-01/03/04/05/06/07/08/09/10 done) | Forecast, SRS, level stats and local history/streak done |
| 5 | API robustness | API-07 (API-01/02/03/04/05/06 done) | OUTBOX/401/parse retry verified 2026-11-19; 429 (API-02) and 403 pending |
| 6 | UI, stats and sync | UI-07 → UI-08 (UI-01/02/03/04/05/06 done) | Polish |
| 7 | Technical debt and release | TECH-03 → TECH-08 (done; TECH-07 optional) | Version, OTA, license and hardening |
| 8 | On-device Wi-Fi (v1.1) | SETUP-09 | Pick the network and type the password on the Cardputer |
| 9 | Kakehashi-inspired extras | EXT-01 → EXT-09 | Practice modes, critical items, audio and stats |
| 10 | External input (post-v1) | BT-01 → BT-04 | Optional BLE keyboard with the built-in keyboard as fallback |
| 11 | PaperDIY (post-v1) | PD-01 → PD-05 | Read-only e-paper companion; full reviews after BT |
| 12 | Round 2 (spec) | Waves 0a–4 in [docs/spec/README.md](docs/spec/README.md#5-execution-waves-and-file-ownership): FIX, CFG, REV, LES, STUDY, STATS, REM, AUD | Fixes, settings, reviews, lessons, study/stats and reminders/audio; per-task status in `docs/spec` |

## 1. Radicals and remove `efontJA_16` (v1 blocker)

The card has the token pack with 7,269 glyphs and 498 radical bitmaps
(3,698,476 B), generated and uploaded on 2026-10-27. The renderer, the
missing-glyph state and a real radical review prompt have been verified on
hardware. The last verification step is a radical-only lesson, which needs a
low-level account.

- **RAD-01 — Generate the radical pack.** Use the user's token and the radical
  SVG URLs (`cairosvg`), including the 16-byte radical entries. **Done
  (2026-10-27):** 498 radical entries, no empty bitmaps, uploaded to the card.
- **RAD-02 — Verify radical rendering on hardware.** Check review and lesson
  prompts at 64/32 px, radical-only lessons and the missing-glyph state.
  **Done except radical-only lessons (2026-10-28):** the 96 px bitmap, scaling,
  missing state and a real radical review prompt (radical 舌) are verified with
  the configured account. Radical-only lessons remain in RAD-04.
- **RAD-03 — Remove `efontJA_16`.** Replace the remaining fallbacks with the
  pack + missing state, then confirm the flash drop. **Done (2026-10-28):**
  readings and feedback use the pack at 32/20 px, missing text shows “no
  glyph”, and the reference is gone. Flash fell from 1,741,705 B to 1,273,425 B
  before the cache work (~468 KB).
- **RAD-04 — Test with a low-level account.** The first lessons are mostly
  radicals. **Pending:** needs a low-level account and hardware.

Dependency: the radical images are generated locally with the user's own
token; the repository must not contain WaniKani art.

## 2. Persistent offline cache (v1 blocker)

Home, the user and a review batch now survive a reboot without network. The
snapshot (summary + assignments + user) is in LittleFS; the subjects referenced
by the batch are on SD. A cached session can be started and completed offline,
completed items are pruned from the cache and kept in the outbox, and a cache
older than 24 h is marked stale. The real-account online fetch → Wi-Fi off →
review → reconnect → 201 path is verified; the remaining check is a deliberate
reboot with Wi-Fi unavailable.

- **CACHE-01 — Persist assignments.** Store the due assignment list and
  critical state in LittleFS, as in the design. **Done:** the fetched batch and
  its SRS state live in `/state/cache.bin` (version, token fingerprint, CRC32,
  `.tmp` + `.bak`).
- **CACHE-02 — Persist subjects.** Store the subjects needed for the cached
  sessions on SD; keep the payload separate from assignments. **Done:**
  `/kanikey/cache/subjects.bin`; loading tolerates missing subjects and drops
  the affected assignments.
- **CACHE-03 — Restore at boot.** Load Home counts and a review batch at boot
  when Wi-Fi is unavailable, without waiting for a failed request. **Done:**
  the cached Home is drawn first; Wi-Fi sync is skipped when no credentials are
  saved.
- **CACHE-04 — Offline state.** Show `Offline`, last-sync time and cache age;
  distinguish “no network” from “never synced”. **Done:** Home shows `Offline`,
  the cached sync time and age when the clock is trusted; older than 24 h is
  `Stale cache`; a missing cache keeps the `Not synced` boot state.
- **CACHE-05 — Invalidation.** Define cache version, token/account change,
  server `data_updated_at` and manual sync invalidation. **Done (2026-10-28):**
  versioned `KKCA`/`KKCS` files (currently v11), token fingerprint at load,
  summary `data_updated_at` stored, assignments keep theirs, 24 h stale state,
  Settings `Sync now` and `Clear cache`. Sign-out clears account state.
- **CACHE-06 — Keep the outbox coherent.** Flush/reconcile pending reviews
  against the cache and avoid resurrecting stale assignments. **Done:**
  cached assignments/subjects already in the outbox are pruned at boot, a fresh
  summary drops assignments that are no longer due, `startReview` filters the
  outbox, and a successful/reconciled send also prunes the cache.
- **CACHE-07 — End-to-end test.** Online fetch → reboot with Wi-Fi off →
  complete a review → reconnect → outbox 201. **Done (2026-11-19):** with the
  real account, online fetch → Wi-Fi off → review → outbox pending →
  reconnect → 201 is verified, and a deliberate reboot with the network
  unavailable restores the cached Home and its 20-assignment batch and starts
  a cached review. The review → outbox → 201 path was verified in the earlier
  run; the offline reboot was the last gap.
- **CACHE-08 — Save before sleep.** Persist the cache/outbox before dim,
  blank or deep sleep (coordinate with TNG-05). **Done:** the snapshot is saved
  after sync, batch fetch, review completion and lesson start, and again before
  deep sleep; the outbox already writes before each request.

## 3. Complete lesson flow (M4)

The list, card and `PUT /start` work. The local quiz is optional practice
before `PUT /start`, as decided; the flow is code-complete except for a real
radical-only lesson on a low-level account.

- **LESSON-01 — Lesson list.** Available lessons with character, type, level
  and main meaning; sorted by level and `lesson_position`. **Done
  (2026-10-28):** scrolling list with prompt, type/level and meaning.
- **LESSON-02 — Selection/picker.** Let the user choose from the available
  batch instead of always taking the first five. **Done:** Up/Down selects and
  Enter opens the card; no `/start` call until the card is confirmed.
- **LESSON-03 — Detail card.** Components, meaning/reading, mnemonic pages,
  hints and the component meanings. **Done (2026-10-28):** component subjects are
  fetched read-only, and their meanings plus the hints are appended to the
  paged card.
- **LESSON-04 — Local quiz.** Meaning/reading quiz, incorrect-answer feedback
  and completion state, if required before starting. **Done (2026-10-28):**
  `Q` on a lesson card opens an optional typed quiz; wrong answers are counted
  and retried, warnings retry the same part, and a completion screen returns to
  the card. The quiz never calls `/start`; only Enter on the card does.
  Hardware-verified on 2026-10-28 (meaning, reading and completion).
- **LESSON-05 — Batch size and progress.** Respect `lessons_batch_size` and
  show progress through the batch. **Done (2026-10-28):** the local setting is
  clamped to WaniKani `lessons_batch_size`; the list and card show position.
- **LESSON-06 — No lessons state.** Show the next unlock or the next review
  time when there are no lessons. **Done (2026-10-28):** a local `No lessons
  now` screen shows the next review time from `/summary`; Enter/Esc returns Home.
- **LESSON-07 — Radical-only lessons.** Already included in code; verify on
  hardware with RAD-01/RAD-02. **Pending:** the configured level-8 account has
  no radical-only lesson in its current batch; a low-level account is needed.
- **LESSON-08 — Ordering.** Respect level and `lesson_position`; do not rely
  on the summary order. **Done (2026-11-06):** the list is built from up to 60
  summary candidates; assignments and subject levels are fetched in bounded
  pages, then `kk::sortLessonCandidates()` orders the whole pool by level,
  `lesson_position` and id before trimming to the configured batch size.
  Hardware check on a fresh level is still pending with RAD-04.

## 4. WaniKani data and preferences

These are the parts of WaniKani that change the study experience. Study
materials, review order, the vacation notice, the SRS indicator, per-subject
review statistics, level progress, vocabulary details and related subjects are
live; review history, audio and the remaining preferences still need work.

- **WK-01 — `GET /study_materials`.** Fetch meaning synonyms, reading notes and
  meaning notes; use synonyms in the answer checker and show notes in detail.
  **Done (2026-10-28):** review and lesson batches fetch the materials by
  `subject_ids`, merge them into the cached subject, accept synonyms as
  meanings, and show the notes on the lesson card and review feedback. A token
  without the study-materials permission still reviews/lessons without notes.
  Hardware-verified on 2026-10-28: 4 materials (synonyms) applied to a review
  batch. The account has no meaning/reading notes, so note display is
  native-tested only.
- **WK-02 — `lessons_batch_size`.** `User` already parses it; use it for
  `fetchLessonBatch` and progress. **Done:** the lesson batch is clamped to it.
- **WK-03 — `reviews_presentation_order`.** Apply `shuffled` /
  `lower_levels_first` instead of always shuffling. **Done (2026-10-28):**
  `User` parses the preference, the cache keeps it, and the review queue
  stable-sorts by subject level when it is `lower_levels_first` (the shuffle
  stays inside each level). The local `Order` setting remains meaning/reading
  order. Hardware-verified `shuffled`; `lower_levels_first` is native-tested
  because this account's WaniKani preference is `shuffled`.
- **WK-04 — SRS indicator.** Show the SRS stage after completing an item and on
  the Home/detail screens where useful. **Done (2026-10-28):** the review header
  shows `SRS N`, and `POST /reviews` is parsed for the new stage; the session
  summary shows `SRS 3 -> 4` (or the stage alone). The response parser is
  native-tested; the header is hardware-verified with a synthetic cached review.
- **WK-05 — Vacation notice.** Read `current_vacation_started_at` and show a
  non-blocking notice. **Done (2026-10-28):** `User` parses and caches the
  timestamp (cache v5), and Home shows `Vacation mode` when there is no more
  urgent sync/outbox/offline status.
- **WK-06 — `GET /review_statistics`.** Per-subject accuracy and mistake
  history; feed the session summary and detail screen. **Done (2026-10-28):**
  the review batch fetches statistics by `subject_ids`, merges them into the
  cached subject, shows `SRS N NN%` in the review header and
  `History N reviews, NN%` in the summary. The account has no statistics
  permission issue; hardware-verified with a synthetic cached review.
- **WK-07 — `GET /level_progressions`.** Level progress, current level and
  level-up time. **Done (2026-10-28; UI-05 update 2026-10-29):** sync fetches
  `/level_progressions`, caches the current level's timestamps and shows
  `L8 42% since 03 Feb` in Home's low-priority status line, with a session
  `Level up! L8 -> L9` notice when the level increases. The real request
  returned 200 on hardware.
- **WK-08 — Item detail data.** Mnemonics, hints, components, user notes and
  synonyms for the review detail screen. **Done (2026-10-28):** review subjects
  are parsed with details, component subjects are fetched in small pages and
  kept with the batch, and the data is cached. The detail screen is UI-04 and
  is done.
- **WK-09 — Vocabulary detail fields.** Parse `parts_of_speech`,
  `context_sentences` (stored, up to 4) and `pronunciation_audios` (phase 2).
  **Done except audio (2026-10-28):** parts of speech and context sentences are
  parsed/cached and shown on the vocabulary detail page (6/6). Audio remains
  EXT-06.
- **WK-10 — Related subjects.** Parse `visually_similar_subject_ids`,
  `amalgamation_subject_ids` and the component graph; use it in lesson
  components and a future constellation view. **Done (2026-10-28):** the IDs are
  parsed/cached and the detail card adds a related page with counts and cached
  meanings. The component graph is already used by lesson components.
- **WK-11 — Review history and stats.** The API's `/reviews` endpoint is
  deprecated and empty, so history is device-local. **Done (2026-11-06):**
  completed reviews append a 16-byte record to `/state/journal.bin` (block CRC,
  ring of 4,000, account-scoped); `T` on Home opens a four-page Stats screen
  with the next-24 h forecast, the SRS distribution, level progress and a
  seven-day history with today's item accuracy, current streak and best streak.
  Home's `Next review` line shows the next bucket count (`+12 at 18:00` /
  `18:00 (+12)`). A full heatmap remains an optional EXT-05 extension.

## 5. Outbox and API robustness (code and docs done; 2026-11-19 pass)

This section is code- and docs-complete. CRC/`.bak`, ambiguous-request
reconciliation, offline end-to-end, Home pending/attention counts, the
user-facing rejected/stuck path with attempt/age detail, the 50-attempt cap, the
shared 429 gate, the shared 401 auth gate and parse retries all landed. The
2026-11-19 hardware pass verified the final OUTBOX screen (API-01/05), the 401
gate (API-03) and the parse-retry path (API-04). API-02 (429) and the 403 half
of API-03 remain native-tested only.

- **API-01 — Dismiss `Rejected` items.** User-facing action with confirmation
  and a clear reason. **Done (2026-10-28):** the HTTP code is stored in the two
  reserved bytes at record offset 22 (still 24 B, still file version 1, old
  files load with 0). Home shows `O: outbox` when attention is needed; the
  OUTBOX screen lists rejected items with their reason, scrolls, and confirms a
  discard of exactly that sequence. Pending/ambiguous items and the cache are
  untouched. Dev `J` seeds a 422, a 409 and a stuck item. Native-tested and
  **hardware-verified (2026-11-19):** the screen listed all three with
  `HTTP 422`, `HTTP 409` and `gave up`; the confirmation showed the attempts
  and age; the discard removed only the selected seq.
- **API-02 — 429 backoff.** Wait for `RateLimit-Reset` and back off instead of
  retrying immediately. **Done (2026-10-29):** `RateLimit-Reset` and
  `RateLimit-Remaining` are parsed case-insensitively. A 429 arms one shared
  monotonic gate that answers every GET/POST/PUT with a synthetic local 429
  until the deadline, so there is no immediate retry. The wait is the reset
  delta capped at 120 s, with a 60 s fallback when the header is absent/past or
  the clock is untrusted. Boot, Home and the sync error line show the remaining
  seconds; an outbox POST 429 stays `Pending`. Native-tested (5 cases). The
  countdown refreshes each second, Level/SRS 429s remain row warnings, batch
  loops stop on 429 and ambiguous reconciliation is not downgraded. A real 429
  was not provoked on hardware on 2026-11-19; the gate remains native-tested
  only.
- **API-03 — 401/403 auth state.** Surface “token invalid/expired” and
  “missing permissions”; do not retry forever. **Done (2026-10-29):** a real 401
  arms one shared gate; GET/POST/PUT then return a synthetic local 401 without
  TLS until setup starts, the token is saved/validated/cleared, or the user
  signs out. A 403 stays endpoint-specific and is not gated. Outbox 401/403 items
  stay `Pending` and keep a deferred auth issue across a successful `/user` or
  `/summary`; sync end reapplies it so Home/Boot show the generic
  `Auth: token rejected` / `Auth: missing permissions` until the outbox drains.
  The `Reviews:` / `Lessons:` / `Lesson:` strings are transient loading/sync/
  debug lines, not the Home status. Review/lesson batches carry the HTTP code and
  stop on auth; an optional materials/statistics/component 401 also stops the
  session. A failed lesson `PUT /start` returns Home with the auth state. A reconciliation blocked by the 401 gate stays `Ambiguous`, and
  Level/SRS 401s remain warnings while completing the sync. A 4xx is never
  retried by the JSON/body helpers. Native tests cannot cover the TLS client.
  **401 hardware-verified (2026-11-19):** a real `/user` 401 armed the gate and
  a following request logged `blocked by auth (401)` without a TLS call. The
  403 path remains native-tested; it needs a read-only token.
- **API-04 — Retry parse failures.** Retry truncated or parse-failed responses,
  not just transport errors. At the time, the subject DOM path failed the batch
  on a short body. **Done (2026-10-29):** `wkGetJsonRetry` parses each 200 body
  immediately and retries a truncated/unparsable 200, `httpCode == 0` or 5xx
  once (two attempts); 4xx and 429 are not retried by the helper, and 429 still
  uses the shared gate. Review/lesson assignments, subject and component pages,
  `/user` and `/summary` (streaming parser) use it, as do the optional
  level/SRS/study-material/statistics requests. The error strings (e.g.
  `subjects JSON`) survive both attempts, and a structurally valid empty
  `/summary` is no longer treated as a parse failure. **Hardware-exercised
  (2026-11-19):** a real `/assignments` body was truncated at 32 KB, the log
  showed the retry and the second parse failure; that finding led to the
  practice-pool stage split (see `docs/hardware.md`).
- **API-05 — Attempts and age.** Cap attempts and show attempt/age information
  for stuck items. **Done (2026-10-29):** `OutboxState::Stuck` (`4`) and a
  50-attempt cap; a would-be retry becomes stuck instead of sending. Pending
  excludes stuck, and attention counts rejected + stuck for Home, Summary and
  the sync row. The OUTBOX screen lists both, shows `gave up`, attempts and age
  (`just now`/hours/days, or `age unknown` with a zero/untrusted timestamp) and
  dismisses only the selected seq. Flush skips stuck without blocking later
  items; dev `J` adds a stuck seed. Native-tested and **hardware-verified
  (2026-11-19):** `gave up`, attempts/age and the single-seq discard were
  checked on the device.
- **API-06 — Manual sync and retry.** A clear action from Home/Settings and the
  error screens. **Done (2026-10-29):** Home shows
  `Enter: reviews  L: lessons  S: sync`; pressing `s` or `S` starts the same
  `startSync()` and UI-02 sync screen as Settings › `Sync now`. The existing
  prerequisite check and error screens are unchanged; the error screens already
  retry, so no duplicate action was added. `Esc` still opens Settings; the UX
  text documents that the hint bar omits it.
- **API-07 — Keep docs in sync.** Update `api-sync.md` error table and screens
  with the implementation. **Done (2026-10-29):** audited `README.md`,
  `ROADMAP.md`, `docs/api-sync.md`, `docs/ux.md`, `docs/design.md`,
  `docs/setup.md`, `docs/hardware.md` and `docs/decisions.md` against the
  current firmware: sync steps, cache v11, outbox states/attempts/age, 401/403
  and 429 gates, parse retries, `/summary` empty behavior, Home/Sync/Outbox/
  Summary/Lesson wording, CI and remaining hardware checks. A follow-up pass
  fixed the 13 contradictions reported by the tester (cache E2E status, lesson
  ordering, API-01 hardware row, single Home status line, pack-gated sync,
  implemented settings, current storage format, session-only stats, POST
  fields, timeout split, first-sync scope, reopenable portal and test-coverage
  overclaims). No firmware code or tests changed.

## 6. Setup, settings and pause (M5)

First boot no longer needs a serial cable. The device starts a WPA2 setup
network, saves the home Wi-Fi, joins it, then serves the station page where the
token is validated with `GET /user`, the browser sets the clock/time zone and
the pack can be checked again. Pause, the core device settings, review/lesson
batch sizes, order and clear auth/rate-limit/offline screens are also on the
device. SETUP-09 makes the phone optional for Wi-Fi changes by adding a
scan/password flow on the Cardputer itself.

- **SETUP-01 — First-boot setup portal.** Wi-Fi scan/password, token entry
  (masked, never logged), timezone and visual-pack check. Port the applicable
  parts of Tangocho's portal (TNG-01). **Done (2026-10-28):** WPA2 AP
  `KaniKey-XXXX`, two-phase AP → station page, Wi-Fi suggestions, token form,
  browser time/zone and pack check. The token and password are never logged or
  shown back.
- **SETUP-02 — Token validation.** Validate the token with `/user` before
  storing it; show scopes/permission errors clearly. **Done:** the station page
  validates with `GET /user`; 401/403/TLS errors are shown and the token is
  only saved on success. Setup makes no lesson-start calls; the first sync may
  flush the existing outbox.
- **SETUP-03 — Time and pack check.** Timezone selection, SNTP result and
  “copy glyphs.kgp / check again” state. **Done:** the browser posts its clock
  and POSIX time-zone rule; the pack status and “Check pack again” button are
  on both pages.
- **SETUP-04 — Pause menu.** Continue / End session / Settings instead of the
  current direct `Esc` exit. **Done (2026-10-28):** `Esc` in review opens the
  three-row pause screen; Continue preserves the session, End session queues
  the current item and leaves, Settings opens the menu.
- **SETUP-05 — Settings.** Brightness, dim/blank/sleep timings, timezone,
  review/lesson preferences and About/version. **Done (2026-10-28):** brightness, timings, review batch size/order, lesson
  batch size, time zone (via the setup portal), Setup portal shortcut, Sync now,
  About, clear cache and sign-out are implemented. Sign-out clears the token,
  cache and outbox after a confirmation and keeps the Wi-Fi credentials.
- **SETUP-06 — Low battery.** Show a low-battery state and confirm before
  starting a long session. **Done (2026-10-28):** below 20% on battery, Enter/L
  opens a confirmation before reviews or lessons; USB power skips it.
- **SETUP-07 — Manual sync and errors.** Sync now, retry, and clear
  offline/auth/rate-limit screens. **Done (2026-10-28):** Sync now is in
  Settings; 401/403, 429, 5xx/other and network failures have distinct boot/Home
  states; Enter retries or opens setup, and a successful sync clears them.
- **SETUP-08 — About.** Firmware version, pack status, cache status, heap and
  last sync. **Done (2026-10-28):** version, build date, pack, cached batch,
  last sync/offline, Wi-Fi/IP, token presence, time zone and heap.
- **SETUP-09 — On-device Wi-Fi setup (v1.1).** Choose the 2.4 GHz network and
  type its password on the Cardputer, without a phone. A scan list shows SSID,
  signal and security, with a manual SSID for hidden networks; the password
  field is masked, supports Backspace and a show/hide toggle, and only saves
  the credentials after a successful join. On failure the password stays
  editable and the reason is shown (`not found`, `wrong password`, `timeout`,
  `unsupported`); `Esc` cancels. Reachable from Settings › Wi-Fi and from the
  boot “Wi-Fi not configured” state; the phone portal stays as the fallback and
  is still where the token is entered in this task. Reuse
  `DevWifi::scan/connect` and the existing text-entry path; never log or show
  the password while masked. A phone-free first boot also needs on-device
  token entry — a separate follow-up if wanted.

## 7. UI, stats and sync (M5)

- **UI-01 — Home SRS distribution bar.** Apprentice/Guru/Master/Enlightened/
  Burned counts and visual bars. **Done (2026-10-28):** sync asks for five
  `/assignments` buckets (`srs_stages=1,2,3,4`, `5,6`, `7`, `8`, `9`) with
  `page_after_id=999999999&hidden=false` and reads only `total_count`; the
  counts are stored in `KKCA` v10. Home draws the segmented 224 px bar at
  y=100 with 3 px segments and A/G/M/E/B labels; the sync clock moved to the
  header. A missing distribution shows `SRS: sync for counts`, and an offline
  boot keeps the cached bar. A successful live review moves one count between
  buckets; the next sync is authoritative. Native-tested and hardware-verified
  (2026-10-28): counts A 145 / G 143 / M 81 / E 147 / B 586; each count request
  returned ~350 B.
- **UI-02 — Sync screen.** Progress, cancel and a clear offline/error state.
  **Done (2026-10-28):** a dedicated six-row screen shows Wi-Fi, Outbox,
  Account, Summary, Level and SRS with values and `ok` / active / `!` / `X` /
  `--` states, a 0/6 progress bar and `Esc: cancel`. The rows update between
  the blocking requests; a cancel keeps completed data and returns to Home (or
  boot `Sync cancelled`). Failures stay on screen with the error line and
  `Enter: retry` or `Enter: setup` for 401/403; `Esc` goes back. Level and SRS
  failures are warnings and do not block the sync. Hardware-verified on
  2026-10-28 with a forced Summary failure; normal sync returned to Home.
- **UI-03 — Session summary.** Accuracy, time, completed items, mistakes and
  SRS stage changes, not only counts. **Done (2026-10-28):** three columns for
  reviews, accuracy and time; a per-session upward-stage count comparing each
  successful `POST /reviews` response with the stage tracked in the pending
  assignment; mistakes, outbox, and a last-item line whose stage comes from the
  last successful POST but whose history comes from the last completed item.
  These can differ while that item's POST is still pending.
  Accuracy and duration are in native-tested `wk/SessionStats.h`; the session
  timer is a non-persistent `millis()` value latched when the summary opens.
- **UI-04 — Item detail screen.** Mnemonics, components, notes, synonyms and
  readings; open from review feedback. **Done (2026-10-28):** `Tab` on feedback
  opens a 5-page card; `Space` pages, `Tab`/`Esc` returns. Verified on hardware
  with a synthetic cached review.
- **UI-05 — Level progress.** Current level, progress to next level and
  level-up feedback (with WK-07). **Done (2026-10-29):** the Level step asks for
  the current level's kanji total
  (`/subjects?levels=<level>&types=kanji&hidden=false&page_after_id=999999999`) and passed
  kanji (`/assignments?levels=<level>&subject_types=kanji&srs_stages=5,6,7,8,9&hidden=false&page_after_id=999999999`),
  reading only `total_count`. Home shows `L8 42% since 03 Feb` (rounded, 0 when
  the total is unknown) and keeps `L8 since 03 Feb` when the counts fail; both
  counts and the timestamps are cached in `KKCA` v11. A sync that raises the
  level shows `Level up! L8 -> L9` in green after auth/outbox/offline and before
  vacation for the rest of the session; it is not shown on the first sync
  without a previous level and is cleared on sign-out, a new token or a level
  decrease.
  Native-tested (61 tests); both firmware builds pass. Hardware-verified
  (2026-10-29): Home showed `L8 3% since 03 Feb` from the real count requests;
  a real level-up transition is still pending.
- **UI-06 — Lesson layout fixes.** Page counter, hint bar spacing and the
  `drawLoading` Japanese subtitle that can be blank. **Done (2026-10-29):**
  the header keeps `2/5` and adds `p1/3` to its right with an 8 px gap, well
  clear of `LESSON`; the old y=117 page line is gone. A 2 px item progress bar
  at y=14 fills for completed items and leaves a 4 px gap before the card. The
  y=125 hint bar holds `Space p  Q quiz` (90 px) left and
  `Enter start  Esc list` (126 px) right, leaving a 12 px gap in 228 px.
  `startCurrentLesson()` passes the English main meaning to `drawLoading`.
  Mockups and docs updated; 61 native tests and both firmware builds pass.
  Hardware-verified (2026-10-29): a live lesson card showed `1/5 p1/2`, the
  2 px bar and the full hint row without clipping; no `/start` was sent.
- **UI-07 — Missing pack/glyph states.** The prompt state exists; finish the
  full startup/setup wording and actions. **Done (2026-10-29):** boot shows
  `Pack missing` with the `/kanikey/glyphs.kgp` path and `Enter: check again`;
  the setup portal shows the pack status and a re-check action; a missing
  prompt glyph shows `no glyph` / `no pack` / `check SD`; About reports the
  pack status.
- **UI-08 — Palette and screenshots.** Revisit the RGB332 palette after real
  use; keep live screenshots with user content out of the repository.

## 8. Tangocho modules to port

The Cardputer layer from Tangocho is already paid for. This section lists what
still applies and where it lands; the `.tgc` pack, multiple-choice quiz, local
SRS and deck catalog remain intentionally dropped. See
[docs/reuse-tangocho.md](docs/reuse-tangocho.md) for the module matrix.

- **TNG-01 — `WifiPortal`.** Adapt the Wi-Fi portal for first-boot setup and
  token entry (SETUP-01). **Done (2026-10-28):** reduced to KaniKey setup in
  `platform/SetupPortal`; no deck catalog or progress-restore routes.
- **TNG-02 — `Ota`.** Port manifest download, SHA-256 verification, update and
  rollback (TECH-06). **Done (2026-10-29):** `platform/Ota` + `ota/Manifest`,
  Settings › Firmware, About badge, bootloader rollback and the release
  workflow; see `docs/ota.md`.
- **TNG-03 — UI primitives.** Port/adapt `Widgets`, `ListView`, `HintBar`,
  `TextLayout`, `UiFont` and `Icons` before adding more screens; the current UI
  is hand-drawn.
- **TNG-04 — Storage layer.** Adapt LittleFS/SD paths and atomic writes for the
  assignment/subject cache (CACHE-01/CACHE-02). **Done:** `platform/CacheStore`
  with `.tmp`/`.bak` and CRC32.
- **TNG-05 — Power and sleep.** Save the cache and outbox before dim, blank or
  deep sleep; restore on wake (CACHE-08). **Done:** cache saves after sync,
  batch, review/lesson changes and before deep sleep; the outbox already saves
  before each request.
- **TNG-06 — Serial remote.** Extend the screenshot/remote workflow for text
  input and the new screens.
- **TNG-07 — Clock/time helpers.** Adapt ISO 8601, timezone and SNTP helpers
  for SETUP-03 and WK-05.
- **TNG-08 — Upstream discipline.** Keep Tangocho as an upstream reference and
  cherry-pick HAL/keyboard/display fixes when needed.

## 9. Technical debt, CI and release

- **TECH-01 — Streaming subject parser. Done (2026-10-29).** `wk/JsonStream`
  (SAX, 32-level cap, escapes/surrogate pairs) feeds `AssignmentSink` and
  `SubjectSink`; review and lesson `/v2/assignments`, their `/v2/subjects` pages
  (5 review subjects, 3 lesson subjects, 3 review components, 10 lesson
  components) and `fetchSubjectsByIds` no longer build a page DOM. Page sizes,
  fetch order, the static 32 KB body buffer and the two-attempt/300 ms retry
  policy are unchanged. Expected memory win: no DOM nodes or whole-page string
  copies; peak heap should fall by the per-page DOM size (largest on detail
  pages). Hardware-verified on 2026-10-29: review and lesson pages parsed
  without `bad_alloc`; the largest free block stayed >=35 KB (see
  `docs/hardware.md`). 72 native tests pass; release build 1,412,341 B flash /
  94,704 B static RAM, dev build 1,419,953 B / 94,728 B.
- **TECH-02 — Refactor `main.cpp`. Done (2026-10-29).** `main.cpp` is now only
  the Arduino entry points (`setup()`/`loop()` forwarding to `kk::app`). The
  Home, Sync, Boot, Loading, About, Outbox, Settings, setup portal, modal,
  Review, Lesson and dev screens are separate `app/*Screen` views drawn from
  explicit view data: `HomeView`, `SyncView`, `BootView`, `LoadingView`,
  `AboutView`, `OutboxView`, `SettingsView`, `SetupView`, `PauseView`/
  `LowBatteryView`, `ReviewView`/`ReviewDetailView`/`ReviewSummaryView`,
  `LessonListView`/`LessonEmptyView`/`LessonCardView`/`LessonQuizView` and
  (dev only) `DebugView`/`RadicalTestView`. `app/App.cpp` holds the application
  and session state, the screen adapters and router, the key handlers, the
  cache/sync/outbox/portal services, the serial console and the asset server.
  `ui/Theme.h` holds the common palette, `ui/Text` holds `drawWrapped` and
  `ui/Widgets` holds `drawPill`, `trimFrontToWidth`, `drawSubjectPrompt`,
  `drawMissingGlyph` and `drawJapaneseFit`; the three glyph helpers take an
  explicit `GlyphPack`. 72 native tests pass; release build 1,421,589 B flash /
  94,704 B static RAM, dev build 1,429,505 B / 94,728 B. The extracted Review,
  Lesson and dev frames were re-checked on hardware on 2026-10-29; only the
  lesson-list capture is blocked by the pre-existing canvas recreation limit
  (see `docs/hardware.md`).
- **TECH-03 — Dev hooks. Done (2026-10-29).** `KANIKEY_DEV` now covers the M0
  screen, the Tab shortcut, `gDirectReading`/`gLessonQuizDirectReading`, the
  serial token/Wi-Fi/TLS/probe commands, the remote key helpers, the synthetic
  outbox/cache seeds and the OTA test commands (`U`/`F`/`I`/`V`/`Y`). The
  release serial console keeps only `h`, `i`, `p`, `S`, `up` and `r`. Both
  images compile; the dev image exercised the lesson fetch with the 24 KiB loop
  stack and no overflow.
- **TECH-04 — PlatformIO CI. Done (2026-10-29).** The `firmware` workflow
  (`.github/workflows/firmware.yml`) runs `pio test -e native`, the release
  manifest tests and the `cardputer-adv` release build; PlatformIO and pip
  caches are keyed by `firmware/platformio.ini`.
- **TECH-05 — Host tests. Done (2026-10-29).** The retry predicate moved to
  native-buildable `wk/HttpRetry` (`httpRetryable`) and is table-tested across
  200/4xx/3xx/2xx and 0/5xx; the outbox capacity edge is covered too. 77 native
  tests pass. Pack/RLE reads, HTTP transport and outbox persistence stay
  platform-only.
- **TECH-06 — Release. Done (2026-10-29).** MIT `LICENSE`, consolidated
  `THIRD_PARTY_NOTICES.md`, `Version.h`/`KANIKEY_ENV`, a tag-driven
  `.github/workflows/release.yml`, `tools/release/firmware_manifest.py` and
  the OTA path. Settings › Firmware and About show the running version/slot and
  a cached offer; the manifest check runs daily after sync and on demand. The
  install streams to the spare slot, checks size + SHA-256, activates and marks
  valid after 10 s; a failed first boot rolls back and is not offered again.
  Hardware-verified on 2026-10-29 with a local test server: v0.1.0 → v0.1.2/
  v0.1.3 in the other slot, plus a rollback when the image was reset before its
  10 s window (see `docs/hardware.md`). API/branding review is in
  `docs/ota.md` and `THIRD_PARTY_NOTICES.md`.
- **TECH-07 — 16-bit canvas.** Evaluate only if a later change frees enough
  contiguous memory; the 8-bit path is currently the stable one.
- **TECH-08 — Release hardening. Done (2026-10-29).** The cached review batch
  is lazy: Home no longer keeps 20 subjects alive, the batch is evicted after a
  review and before a lesson fetch, and the snapshot preserves its assignment
  list. The lesson-fetch minimum free heap rose from ~3.5 KB to ~56 KB, the loop
  task stack is 24 KiB (dev high-water ~17.9 KB free) and the USB RX ring is
  4 KiB (uploader chunk 2 KiB). The canvas recreated successfully in the lesson
  fetch run; both release and dev images pass. See `docs/hardware.md`.

## 10. Kakehashi-inspired extras that fit (after v1)

These are the product-ideas part of [docs/reuse-tangocho.md](docs/reuse-tangocho.md)
§6. They are worth doing once the WaniKani core is solid; they are practice and
progress features, not a second SRS.

- **EXT-01 — Configurable batch sizes.** Review and lesson batch size in
  Settings. **Done:** review 10/20/30 and lesson 3/5/10, clamped to WaniKani's
  `lessons_batch_size`.
- **EXT-02 — Question order.** Meaning-first / reading-first and custom
  ordering. **Done for the pair:** Settings › `Order` toggles meaning-first /
  reading-first; custom ordering is still open.
- **EXT-03 — Critical items / leeches.** **Done (2026-11-06):** a full sync
  detects account-wide statistics below the configured `Critical below`
  threshold (60/70/75/80/90%, default 75) with at least `Critical min` answers
  (3/5/10, default 5), filters hidden/burned assignments and stores the list
  for offline use. Settings › `Critical first` puts due critical items at the
  front of the review queue. Extra study › `Critical items` shows the
  worst-first list, opens the detail card and can start a local practice
  session.
- **EXT-04 — Extra study modes.** **Done (2026-11-06):** `P` opens Extra
  study with Recent mistakes, Recent lessons, Critical items and Current level.
  Practice is local: no outbox, no POST/PUT, no SRS or due-count changes; each
  answer is journaled with `practice=true` and the summary says
  `Nothing was sent to WaniKani`. A persistent apprentice pool
  (`/state/practice.bin`) is refreshed on sync for offline lessons/current
  level; S3/S4/S5 windows and size are in Settings.
- **EXT-05 — Advanced stats.** **Done except the heatmap (2026-11-06):**
  forecast, SRS distribution, level progress, seven-day history, today's item
  accuracy and current/best streak are live in the Stats screen; a full
  calendar heatmap is the remaining optional view.
- **EXT-06 — Audio.** **Partial (2026-11-06):** `Subject` parses up to four
  `pronunciation_audios` clips (URL/content type/voice), the ES8311 is enabled
  and serial `n` plays a test tone. `AudioCache` downloads clips to
  `/kanikey/audio/` with a 50 MB LRU index; serial `a` caches the current batch.
  Decoder, playback and automatic prefetch wait for the AUD-01 hardware
  measurements and a decoder decision.
- **EXT-07 — Stroke order.** KanjiVG rendering and attribution (CC BY-SA).
- **EXT-08 — JLPT / Joyo / frequency / constellation.** Data-heavy views for
  progress and component relationships; only if the core is stable.
- **EXT-09 — Reminders.** A device-local “reviews ready” reminder; no push
  notifications in v1.

Out of scope for KaniKey: anime clips, NHK reader, Spotify lyrics, camera OCR,
MyAnimeList sync, home-screen widgets, Apple Watch, community/forum, push
notifications and a local SRS.

## Round 2 (specified 2026-11-01)

The round-2 plan is in [docs/spec/README.md](docs/spec/README.md). Its task
tables are the authoritative status; this roadmap keeps the product direction
and the 2026-11-19 hardware-pass results. The remaining v1 extras map to spec
work as follows; EXT-01 and EXT-02 stay done, and EXT-07/EXT-08 are out of
round 2.

| Roadmap item | Spec task | Scope | Spec status |
|---|---|---|---|
| EXT-03 | STUDY-01, STUDY-02 | Critical items and leeches | done (2026-11-06) |
| EXT-04 | STUDY-03, STUDY-04, STUDY-05 | Extra study and the offline practice pool | done (2026-11-06) |
| EXT-05 + WK-11 | STATS-01…STATS-04 | Forecast, history, streak and level progress | done (2026-11-06) |
| EXT-06 | AUD-01…AUD-03 | Vocabulary audio | partial (AUD-01/02); AUD-03 todo |
| EXT-09 | REM-01…REM-03 | Device-local reminders and sounds | todo |
| Complete lesson flow | LES-01, LES-02, LES-03 | Browse → batch quiz → confirmed batch start | review (2026-11-19; hardware pending) |
| LESSON-08 | LES-04 | Lesson ordering | partial: default order done; L2 choices open |

The other spec groups are tracked in `docs/spec`: ARCH (controllers, settings
registry, local stores, screen state), FIX (01-fixes), SPEED (submission/sync),
CFG (sectioned settings), REV (review options), LES (batch flow), STUDY, STATS,
REM/AUD, ONB (pack and setup) and DOC (this documentation pass). ARCH-01 merged
on 2026-11-19 and ARCH-02 is in review with 224 native tests; the current flat
Settings menu stays until CFG-01/CFG-02. No round-2 task is claimed as done here
beyond the spec status above.

## 11. Bluetooth keyboard (after v1)

The built-in 56-key keyboard stays the primary input and the only key source
that can wake the device from deep sleep. An optional Bluetooth LE HID keyboard
adds a full-size input path for long sessions or accessibility. The ESP32-S3
has Bluetooth LE only (no Classic), so Classic-only keyboards are out of scope;
Wi-Fi and BLE share the 2.4 GHz radio, and the no-PSRAM heap is the main risk.
BT-01 is a go/no-go spike before any library or stack is merged.

| ID | Title | Priority | Size | Depends on | Status |
|---|---|---|---|---|---|
| BT-01 | Spike: BLE HID host on the S3 | P3 | M | none | todo |
| BT-02 | Source-neutral key input | P3 | M | BT-01 | todo |
| BT-03 | Pairing, settings and status | P3 | M | BT-02 | todo |
| BT-04 | Coexistence, power and hardware pass | P3 | M | BT-03 | todo |

- **BT-01 — Spike: BLE HID host on the S3.** Answer five questions with
  throwaway probes: (a) which host stack fits the Arduino build (ESP-IDF
  `esp_hidh` with the BLE transport vs a NimBLE-Arduino HOGP host), with flash,
  static RAM and largest free block before/after; (b) whether standard BLE HID
  keyboards pair and type (Just Works and passkey) on at least three models,
  and which ones are Classic-only; (c) reconnect time after boot and after
  deep sleep (BLE is off in deep sleep, so the built-in keyboard is the only
  wake source); (d) key latency with Wi-Fi idle and during a TLS request; (e)
  battery cost with the keyboard connected. No library is merged during the
  spike; the numbers and a go/no-go go to `docs/hardware.md`.
- **BT-02 — Source-neutral key input.** Add a second key source without
  rewriting the screens: a BLE HID report becomes a `KeyEvent` with a
  character and a modifier state, while the TCA8418 path keeps its x/y grid.
  Map US-layout HID usages (letters, digits, punctuation, Space) and the keys
  the UI uses (Enter, Esc, Tab, Backspace, arrows, Shift); unsupported keys
  are ignored. The built-in keyboard always works, and the screen handlers see
  one ordered queue. The pure HID-to-key mapping is native-tested.
- **BT-03 — Pairing, settings and status.** Settings gains `Bluetooth
  keyboard`: Off (default) / On, `Scan`, a short device list, `Pair`,
  `Forget`, and a status line (`off`, `pairing`, `connected`, `no keyboard`).
  Pairing starts only from Settings, never during a review or lesson, and
  requires a confirmation on the built-in keyboard; a 6-digit passkey is shown
  when the keyboard asks for one. Bonds live in NVS and survive reboot. The
  token stays masked; the BLE path cannot open the setup portal by itself.
- **BT-04 — Coexistence, power and hardware pass.** Keep the review loop
  responsive with BLE on: measure key latency while Wi-Fi syncs and during a
  TLS request, reconnect after wake, and battery cost. The feature ships only
  if the spike and this pass show no TLS regression, no heap exhaustion and
  acceptable latency; otherwise it is dropped with the measured numbers.
  Hardware checks: three BLE keyboards, Shift/punctuation, arrows, Backspace,
  Esc, Tab, reconnect, forget/re-pair, pairing timeout, low battery and
  offline use. Docs: `docs/ux.md`, `docs/hardware.md`, `docs/decisions.md`,
  `README.md` and this roadmap.

Out of scope for the first pass: Classic Bluetooth, USB host, multiple
simultaneous keyboards, macros/media keys, non-US layouts and typing while the
device is in deep sleep.

## 12. M5Stack PaperDIY port (after v1)

This is a separate target, not a v1 promise: the Cardputer ADV stays the
primary device. The PaperDIY shares the ESP32-S3 and the reusable `wk/` core
(sync, cache, outbox, lessons, stats), but its 4.7" 960×540 16-gray e-paper,
two user buttons and lack of keyboard/touch make it a different product. The
first target is a read-only companion; full reviews wait for BT-01…BT-04.

| ID | Title | Priority | Size | Depends on | Status |
|---|---|---|---|---|---|
| PD-01 | Board HAL and build target | P3 | M | none | todo |
| PD-02 | E-paper rendering and refresh | P3 | L | PD-01 | todo |
| PD-03 | Two-button UI and read-only scope | P3 | M | PD-02 | todo |
| PD-04 | Power, sleep and sync cadence | P3 | M | PD-03 | todo |
| PD-05 | Hardware pass and docs | P3 | M | PD-04 | todo |

- **PD-01 — Board HAL and build target.** Add `TGC_BOARD_PAPERDIY`,
  `board/BoardPaperDiy.cpp` and a PlatformIO `cardputer-adv`-style env. Cover
  the M5PM1 power management (battery/USB/charging, e-paper power and boost),
  the microSD slot, the two user buttons and deep sleep. Update M5Unified and
  M5GFX to versions with PaperDIY support (≥ 0.2.26 / ≥ 0.2.33) and re-verify
  the Cardputer build. The 8 MB octal PSRAM relaxes the canvas/framebuffer
  budget, but the TLS contiguous-block rule still applies.
- **PD-02 — E-paper rendering and refresh.** The panel is 960×540, exactly 4×
  the 240×135 canvas: start with the existing screens rendered at 240×135 and
  scaled 4× into a high-contrast grayscale theme (the panel has no backlight),
  then move to a native 960×540 layout and larger pack glyphs. Use
  `epd_text`/`epd_fastest` for updates and `epd_quality` on screen transitions;
  batch redraws, avoid a full refresh per key, and schedule a full refresh
  every N partial updates to clear ghosting. Measure refresh time and ghosting;
  the 4× path is a bring-up layout, not the final one.
- **PD-03 — Two-button UI and read-only scope.** With no keyboard or touch, the
  two user buttons navigate (short/long press for Next/Prev, Confirm and
  Back); the hint bar names buttons instead of keys. The first release is a
  read-only companion: Home, lessons list/card, item detail, stats, sync and
  cache/offline. Reviews and the lesson quiz need text entry and stay on the
  Cardputer until a keyboard exists; full reviews depend on BT-01…BT-04 (BLE)
  or a wired keyboard on the FPC expansion (I2S/SPI; no I2C is exposed,
  verify). Today `hasKeyboard()` is only logged, so the app needs a real
  no-keyboard UI path.
- **PD-04 — Power, sleep and sync cadence.** The 140 mAh battery and M5PM1 put
  this in the “desk companion that sleeps between updates” class, not an
  always-on terminal: wake on timer or button, sync, refresh only when the
  data changed, then sleep. Measure sleep current, sync energy and refresh
  energy; keep Wi-Fi/BLE off in deep sleep. USB power is the default for long
  sessions.
- **PD-05 — Hardware pass and docs.** Boot, SD/pack, Wi-Fi/TLS, sync,
  cache/offline, e-paper ghosting and partial refresh, button navigation,
  battery/sleep and OTA. Update `docs/hardware.md` (new measured section),
  `docs/ux.md`, `README.md` and `docs/decisions.md`.

Out of scope for the first pass: touch, backlight, audio, multi-board
auto-detect, the native 960×540 redesign and full reviews without an external
keyboard.

## Definition of done for v1

- Review and lesson Japanese renders entirely from the SD pack; no CJK font in
  flash.
- A session can be fetched, cached, rebooted, completed offline and synced.
- Lessons can be listed, understood (components/quiz) and started.
- WaniKani preferences and user synonyms are respected.
- Rejected, rate-limited and invalid-auth reviews have a visible, recoverable
  path.
- Wi-Fi, token and time can be configured on the device itself.
- The item detail and session summary show enough to learn from a mistake.
- CI builds the firmware, runs the native tests, checks the release manifest
  and scans secrets.
- A tagged release publishes an app image and an OTA manifest; the device can
  install it, mark it valid and roll back a bad first boot.
- The source is MIT-licensed and the third-party notices are complete.
- No WaniKani content is distributed in the repository.

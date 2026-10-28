# KaniKey roadmap

Status snapshot: **2026-10-28**. This is the working backlog after the offline
cache and the `efontJA_16` removal landed, not the original product
plan. Product scope and UX live in [docs/design.md](docs/design.md),
[docs/ux.md](docs/ux.md) and [docs/api-sync.md](docs/api-sync.md).

The review loop works on hardware. The main v1 gaps are the complete lesson
flow, WaniKani preferences/item detail and API robustness. The persistent
offline cache restores Home, the user and a cached review batch after a reboot
without network; first boot now uses an on-device setup portal, and Settings
covers display/power/study preferences plus sync, cache and sign-out.

## Where we are

Working on hardware:

- Home from `/user` + `/summary`, with due reviews, lessons, next review and
  outbox status.
- 20-item review session with typed meaning/reading, romaji-to-kana, answer
  checking, feedback and requeue. The session follows WaniKani
  `reviews_presentation_order` (`shuffled` or `lower_levels_first`); the local
  `Order` setting still selects meaning-first or reading-first.
- `POST /v2/reviews` with a persistent LittleFS outbox. The real-account path
  online fetch → Wi-Fi off → review → reconnect → 201 is verified; the cached
  Home/batch also restore at boot. A deliberate offline boot still needs a
  network-off reboot.
- Lessons: a list/picker with character, type, level and main meaning, sorted
  by level and `lesson_position`; the card shows meaning/reading, paged
  mnemonic, component meanings and hints, and `PUT /assignments/{id}/start`
  returns 200. The list, card and quiz are hardware-verified; tests so far use
  only GETs and no live lesson was started by this work.
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
- SD visual pack: the card now has the token pack with **7,269 glyphs + 498
  radical bitmaps** (3,698,476 B), uploaded over USB on 2026-10-27. Wi-Fi
  asset upload also works.
- Radical prompts render from the pack's 96 px bitmaps; radical-only lessons
  and reviews are no longer filtered out. `efontJA_16` is gone: missing glyphs
  show an explicit state instead. A real radical review prompt (radical 鳥) was
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
  Settings; `Esc` from Home opens Settings. Brightness, dim/blank/sleep
  timings, Setup portal, Sync now and About are live; values persist in NVS.
  About shows version, build, pack, cache, last sync, Wi-Fi, token state, zone
  and heap.
- User-facing boot states for missing SD/pack, token or Wi-Fi and sync
  failures, with `Enter` retry. The M0 debug screen is dev-build only.
- 8-bit RGB332 canvas (32.4 KB), static 32 KB HTTP body buffer, dim/blank/deep
  sleep, screenshot capture.
- 44 native tests pass. GitHub Actions runs gitleaks on `main`.

Current build: **1,373,009 B / 1,835,008 B flash (74.8%)**, **94,184 B /
327,680 B static RAM (28.7%)**. The dev image is 1,378,153 B. Removing
`efontJA_16` freed ~468 KB and leaves ~462 KB of flash headroom.

## Priority summary

| Priority | Workstream | Why | Status | Start with |
|---:|---|---|---|---|
| 1 | Radicals and `efontJA_16` removal | Early levels and lessons are incomplete; frees ~468 KB flash | Done except real-prompt hardware check | RAD-02, RAD-04 |
| 2 | Persistent offline cache | The outbox survives reboot; cached Home/batch now do too | Core landed; real-account E2E pending | CACHE-05, CACHE-07 |
| 3 | Complete lesson flow | List/picker, card, components, quiz, empty state and start exist; ordering/hardware remain | Partial | LESSON-08, RAD-04 |
| 4 | WaniKani data and preferences | Synonyms, notes, review order, vacation and SRS indicator are live; item detail, stats and preferences are missing | Partial | WK-06, WK-07 |
| 5 | Outbox and API robustness | Rejected items, 429 and invalid auth have no user path | Partial | API-01, API-02 |
| 6 | Setup, settings and pause | Setup, pause, settings, low battery and error screens work | Done | — |
| 7 | UI, stats and sync | Missing SRS bar, sync progress, accuracy and item detail | Partial | UI-01, UI-02 |
| 8 | Tangocho modules | Reuse the portal, OTA, widgets, storage and sleep plumbing | Portal ported; OTA/widgets pending | TNG-02, TNG-03 |
| 9 | Technical debt, CI and release | Maintainability and shipping | Ongoing | TECH-01, TECH-04 |
| 10 | Kakehashi-inspired extras | Valuable after v1; not all fit a keyboard device | Not started | EXT-01, EXT-03 |

## Execution order

Radicals are closed except the real-account hardware check. The software work
continues with the remaining cache invalidation/E2E items, then the complete
lesson flow.

| Order | Phase | Tasks | Outcome |
|---:|---|---|---|
| 0 | Close radicals (parallel) | RAD-02, RAD-04 | Real radical prompts on a configured account |
| 1 | Offline cache | CACHE-05 → CACHE-07, TNG-04, TNG-05 | Review and Home work after reboot without network |
| 2 | Complete lessons | LESSON-01 → LESSON-08, WK-02, TNG-03 | List, understand, quiz and start lessons |
| 3 | Setup and device UX | TNG-02 | OTA release path (setup/settings complete) |
| 4 | WaniKani data and detail | WK-06 → WK-11 (WK-01/03/04/05 done) | Item detail and progress |
| 5 | API robustness | API-01 → API-07 | Recoverable rejected, rate-limited and auth errors |
| 6 | UI, stats and sync | UI-01 → UI-08 | SRS bar, sync screen, accuracy and polish |
| 7 | Technical debt and release | TECH-01 → TECH-07 | CI, refactor, version, OTA and license |
| 8 | Kakehashi-inspired extras | EXT-01 → EXT-09 | Practice modes, critical items, audio and stats |

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
  missing state and a real radical review prompt (radical 鳥) are verified with
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
older than 24 h is marked stale. The remaining work is the real-account E2E
validation.

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
  versioned `KKCA`/`KKCS` files (currently v5), token fingerprint at load,
  summary `data_updated_at` stored, assignments keep theirs, 24 h stale state,
  Settings `Sync now` and `Clear cache`. Sign-out clears account state.
- **CACHE-06 — Keep the outbox coherent.** Flush/reconcile pending reviews
  against the cache and avoid resurrecting stale assignments. **Done:**
  cached assignments/subjects already in the outbox are pruned at boot, a fresh
  summary drops assignments that are no longer due, `startReview` filters the
  outbox, and a successful/reconciled send also prunes the cache.
- **CACHE-07 — End-to-end test.** Online fetch → reboot with Wi-Fi off →
  complete a review → reconnect → outbox 201. **Done except the offline boot
  (2026-10-28):** with the real account, online fetch → Wi-Fi off → review →
  outbox pending → reconnect → 201 is verified, and the cached Home/batch
  restore at boot. A reboot with Wi-Fi actually unavailable is still untested.
- **CACHE-08 — Save before sleep.** Persist the cache/outbox before dim,
  blank or deep sleep (coordinate with TNG-05). **Done:** the snapshot is saved
  after sync, batch fetch, review completion and lesson start, and again before
  deep sleep; the outbox already writes before each request.

## 3. Complete lesson flow (M4)

The card and `PUT /start` work, but this is not yet a full lesson flow. The
local quiz decision still needs to be made; the recommendation is to keep it
optional practice before `PUT /start` unless real use shows otherwise.

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
  on the summary order. **Partial:** candidates are sorted before trimming; the
  summary still supplies the first 10 candidates.

## 4. WaniKani data and preferences

These are the parts of WaniKani that change the study experience. Study
materials, review order, the vacation notice and the SRS indicator are live;
item detail, stats and the remaining preferences still need work.

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
  history; feed the session summary and detail screen.
- **WK-07 — `GET /level_progressions`.** Level progress, current level and
  level-up time.
- **WK-08 — Item detail data.** Mnemonics, hints, components, user notes and
  synonyms for the review detail screen.
- **WK-09 — Vocabulary detail fields.** Parse `parts_of_speech`,
  `context_sentences` (phase 2) and `pronunciation_audios` (phase 2).
- **WK-10 — Related subjects.** Parse `visually_similar_subject_ids`,
  `amalgamation_subject_ids` and the component graph; use it in lesson
  components and a future constellation view.
- **WK-11 — Review history.** `GET /reviews` for heatmap, streak and forecast;
  this is a later stats input, not a v1 blocker.

## 5. Outbox and API robustness (finish M3)

Already done: CRC/`.bak`, ambiguous-request reconciliation, offline end-to-end,
Home pending/attention counts. Remaining:

- **API-01 — Dismiss `Rejected` items.** User-facing action with confirmation
  and a clear reason; today only the dev `z` command clears the outbox.
- **API-02 — 429 backoff.** Wait for `RateLimit-Reset` and back off instead of
  retrying immediately. A 429 currently becomes `Pending` with no delay.
- **API-03 — 401/403 auth state.** Surface “token invalid/expired” and
  “missing permissions”; do not retry forever.
- **API-04 — Retry parse failures.** Retry truncated or parse-failed responses,
  not just transport errors. The subject DOM path still fails the batch on a
  short body.
- **API-05 — Attempts and age.** Cap attempts and show attempt/age information
  for stuck items.
- **API-06 — Manual sync and retry.** A clear action from Home/Settings and the
  error screens. **Partial:** Settings › Sync now works; Home/error-screen
  actions are pending.
- **API-07 — Keep docs in sync.** Update `api-sync.md` error table and screens
  with the implementation.

## 6. Setup, settings and pause (M5)

First boot no longer needs a serial cable. The device starts a WPA2 setup
network, saves the home Wi-Fi, joins it, then serves the station page where the
token is validated with `GET /user`, the browser sets the clock/time zone and
the pack can be checked again. Pause and the core device settings are also on
the device; account/study preferences and clear error actions remain.

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

## 7. UI, stats and sync (M5)

- **UI-01 — Home SRS distribution bar.** Apprentice/Guru/Master/Enlightened/
  Burned counts and visual bars.
- **UI-02 — Sync screen.** Progress, cancel and a clear offline/error state.
- **UI-03 — Session summary.** Accuracy, time, completed items, mistakes and
  SRS stage changes, not only counts.
- **UI-04 — Item detail screen.** Mnemonics, components, notes, synonyms and
  readings; open from review feedback.
- **UI-05 — Level progress.** Current level, progress to next level and
  level-up feedback (with WK-07).
- **UI-06 — Lesson layout fixes.** Page counter, hint bar spacing and the
  `drawLoading` Japanese subtitle that can be blank.
- **UI-07 — Missing pack/glyph states.** The prompt state exists; finish the
  full startup/setup wording and actions.
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
  rollback (TECH-06).
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

- **TECH-01 — Streaming subject parser.** Assignments/subjects still build a
  JSON DOM; a streaming parser would reduce fragmentation, permit larger
  batches and make a 16-bit canvas realistic.
- **TECH-02 — Refactor `main.cpp`.** It is ~2,000 lines with many globals.
  Split screens, session state and app services before adding more flows.
- **TECH-03 — Dev hooks.** `KANIKEY_DEV` now covers the M0 screen and Tab
  shortcut; put `gDirectReading`, synthetic outbox commands and other serial
  helpers behind the same flag.
- **TECH-04 — PlatformIO CI.** Add `pio run -e cardputer-adv` and
  `pio test -e native` to GitHub Actions alongside gitleaks.
- **TECH-05 — Host tests.** Add pack/RLE reader tests and more outbox/HTTP edge
  cases.
- **TECH-06 — Release.** Decide the license, finalize third-party notices,
  version the firmware, integrate OTA/About and review WaniKani API/branding
  compliance.
- **TECH-07 — 16-bit canvas.** Evaluate only if TECH-01 frees enough contiguous
  memory; the 8-bit path is currently the stable one.

## 10. Kakehashi-inspired extras that fit (after v1)

These are the product-ideas part of [docs/reuse-tangocho.md](docs/reuse-tangocho.md)
§6. They are worth doing once the WaniKani core is solid; they are practice and
progress features, not a second SRS.

- **EXT-01 — Configurable batch sizes.** Review and lesson batch size in
  Settings; today they are fixed at 20 and 5.
- **EXT-02 — Question order.** Meaning-first / reading-first and custom
  ordering; today it is always meaning then reading.
- **EXT-03 — Critical items / leeches.** Prioritize items with repeated
  mistakes and show a small “critical items” list.
- **EXT-04 — Extra study modes.** Recent lessons, random tests, vocabulary
  reading practice and custom reviews; no local SRS writes.
- **EXT-05 — Advanced stats.** Review forecast, heatmap, streak and 7-day
  history (needs WK-11).
- **EXT-06 — Audio.** Vocabulary pronunciation through the ES8311 and
  `pronunciation_audios`.
- **EXT-07 — Stroke order.** KanjiVG rendering and attribution (CC BY-SA).
- **EXT-08 — JLPT / Joyo / frequency / constellation.** Data-heavy views for
  progress and component relationships; only if the core is stable.
- **EXT-09 — Reminders.** A device-local “reviews ready” reminder; no push
  notifications in v1.

Out of scope for KaniKey: anime clips, NHK reader, Spotify lyrics, camera OCR,
MyAnimeList sync, home-screen widgets, Apple Watch, community/forum, push
notifications and a local SRS.

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
- CI builds the firmware, runs the native tests and scans secrets.
- No WaniKani content is distributed in the repository.

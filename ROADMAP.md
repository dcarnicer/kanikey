# KaniKey roadmap

Status snapshot: **2026-10-29**. This is the working backlog after the offline
cache and the `efontJA_16` removal landed, not the original product
plan. Product scope and UX live in [docs/design.md](docs/design.md),
[docs/ux.md](docs/ux.md) and [docs/api-sync.md](docs/api-sync.md).

The review loop works on hardware. The main v1 gaps are the last lesson
hardware checks and the lesson-ordering limitation, a deliberate offline boot,
hardware verification of the 429/auth/parse-retry paths and the final OUTBOX
screen (they are code/native/host-inspected only), OTA and the license/release.
The persistent
offline cache restores Home, the user and a
cached review batch after a reboot without network; first boot uses an
on-device setup portal, and Settings covers display/power/study preferences
plus sync, cache and sign-out.

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
- 429 handling: `RateLimit-Reset` / `RateLimit-Remaining` are parsed and one
  shared gate blocks every request until the reset; Boot, Home and the sync
  error line show the remaining seconds. This is native-tested; hardware
  verification is pending.
- 401/403 handling and parse-failure retries are implemented and native-tested;
  the shared auth gate and the retrying GET helpers have not had a hardware
  pass. Rejected/stuck outbox rows, their attempt/age detail and dismissal are
  native-tested; the earlier outbox hardware runs covered the send/reconcile
  path, not the final OUTBOX screen.
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
- 72 native tests and the `cardputer-adv` build run in GitHub Actions alongside
  gitleaks on pushes to `main` and on pull requests.

Current build: **1,417,949 B / 1,835,008 B flash (77.3%)**, **94,704 B /
327,680 B static RAM (28.9%)**. The dev image is 1,425,549 B. Removing
`efontJA_16` freed ~468 KB and leaves ~420 KB of flash headroom.

## Priority summary

| Priority | Workstream | Why | Status | Start with |
|---:|---|---|---|---|
| 1 | Radicals and `efontJA_16` removal | Early levels and lessons are incomplete; frees ~468 KB flash | Done except a radical-only lesson | RAD-04 |
| 2 | Persistent offline cache | The outbox survives reboot; cached Home/batch now do too | Core and real-account E2E done; deliberate offline boot pending | CACHE-07 |
| 3 | Complete lesson flow | List/picker, card, components, quiz, empty state and start exist; ordering/hardware remain | Partial | LESSON-08, RAD-04 |
| 4 | WaniKani data and preferences | Synonyms, notes, review order, vacation, SRS, review stats, level progress, vocabulary details and related subjects are live; review history and audio remain | Partial | WK-11, EXT-06 |
| 5 | Outbox and API robustness | Rejected/stuck items, the 50-attempt cap, 429 backoff, parse retries, auth gate and Home/Settings manual sync are in | Code and docs done; hardware check pending for the final OUTBOX screen (API-01) and API-02…05 | — |
| 6 | Setup, settings and pause | Setup, pause, settings, low battery and error screens work | Done | — |
| 7 | UI, stats and sync | Accuracy, the SRS bar, sync screen, item detail and level progress are live | Partial | UI-07 |
| 8 | Tangocho modules | Reuse the portal, OTA, widgets, storage and sleep plumbing | Portal ported; OTA/widgets pending | TNG-02, TNG-03 |
| 9 | Technical debt, CI and release | Maintainability and shipping | Ongoing | TECH-01, TECH-02 |
| 10 | Kakehashi-inspired extras | Valuable after v1; not all fit a keyboard device | Partially started (EXT-01/02 done) | EXT-03, EXT-04 |

## Execution order

Radicals and the offline cache are code-complete except for hardware checks.
The lesson flow is implemented except LESSON-08 (only the first 10 summary
candidates are sorted) and hardware checks. The next work is LESSON-08, then
OTA/release, with the remaining UI and API hardware verification alongside.

| Order | Phase | Tasks | Outcome |
|---:|---|---|---|
| 0 | Close radicals (parallel) | RAD-04 (RAD-02 done) | Real radical prompts and a radical-only lesson |
| 1 | Offline cache | CACHE-05 → CACHE-07, TNG-04, TNG-05 | Review and Home work after reboot without network (offline boot still untested) |
| 2 | Complete lessons | LESSON-01 → LESSON-08, WK-02, TNG-03 | List, understand, quiz and start lessons (ordering partial) |
| 3 | Setup and device UX | TNG-02 | OTA release path (setup/settings complete) |
| 4 | WaniKani data and detail | WK-11 (WK-01/03/04/05/06/07/08/09/10 done) | Review history and progress |
| 5 | API robustness | API-07 (API-01/02/03/04/05/06 done) | Done; hardware verification of API-01's final OUTBOX screen and API-02…05 still pending |
| 6 | UI, stats and sync | UI-07 → UI-08 (UI-01/02/03/04/05/06 done) | Polish |
| 7 | Technical debt and release | TECH-01/TECH-04 done → TECH-07 | Refactor, version, OTA and license |
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
  complete a review → reconnect → outbox 201. **Done except the offline boot
  (2026-10-28):** with the real account, online fetch → Wi-Fi off → review →
  outbox pending → reconnect → 201 is verified, and the cached Home/batch
  restore at boot. A reboot with Wi-Fi actually unavailable is still untested.
- **CACHE-08 — Save before sleep.** Persist the cache/outbox before dim,
  blank or deep sleep (coordinate with TNG-05). **Done:** the snapshot is saved
  after sync, batch fetch, review completion and lesson start, and again before
  deep sleep; the outbox already writes before each request.

## 3. Complete lesson flow (M4)

The list, card and `PUT /start` work. The local quiz is optional practice
before `PUT /start`, as decided; the flow is closed except a radical-only lesson
and the last ordering check.

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
- **WK-11 — Review history.** `GET /reviews` for heatmap, streak and forecast;
  this is a later stats input, not a v1 blocker.

## 5. Outbox and API robustness (code and docs done)

This section is code- and docs-complete. CRC/`.bak`, ambiguous-request
reconciliation, offline end-to-end, Home pending/attention counts, the
user-facing rejected/stuck path with attempt/age detail, the 50-attempt cap, the
shared 429 gate, the shared 401 auth gate and parse retries all landed. The
remaining work is hardware verification of the final OUTBOX screen
(attempts/age/discard, API-01) and API-02…05; those paths are native or
host-inspected only.

- **API-01 — Dismiss `Rejected` items.** User-facing action with confirmation
  and a clear reason. **Done (2026-10-28):** the HTTP code is stored in the two
  reserved bytes at record offset 22 (still 24 B, still file version 1, old
  files load with 0). Home shows `O: outbox` when attention is needed; the
  OUTBOX screen lists rejected items with their reason, scrolls, and confirms a
  discard of exactly that sequence. Pending/ambiguous items and the cache are
  untouched. Dev `J` seeds a 422 and a 409 item. Native-tested; the final
  OUTBOX screen still needs a hardware pass.
- **API-02 — 429 backoff.** Wait for `RateLimit-Reset` and back off instead of
  retrying immediately. **Done (2026-10-29):** `RateLimit-Reset` and
  `RateLimit-Remaining` are parsed case-insensitively. A 429 arms one shared
  monotonic gate that answers every GET/POST/PUT with a synthetic local 429
  until the deadline, so there is no immediate retry. The wait is the reset
  delta capped at 120 s, with a 60 s fallback when the header is absent/past or
  the clock is untrusted. Boot, Home and the sync error line show the remaining
  seconds; an outbox POST 429 stays `Pending`. Native-tested (5 cases). The
  countdown refreshes each second, Level/SRS 429s remain row warnings, batch
  loops stop on 429 and ambiguous reconciliation is not downgraded.
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
  retried by the JSON/body helpers. Native tests cannot cover the TLS client;
  hardware verification is pending.
- **API-04 — Retry parse failures.** Retry truncated or parse-failed responses,
  not just transport errors. At the time, the subject DOM path failed the batch
  on a short body. **Done (2026-10-29):** `wkGetJsonRetry` parses each 200 body
  immediately and retries a truncated/unparsable 200, `httpCode == 0` or 5xx
  once (two attempts); 4xx and 429 are not retried by the helper, and 429 still
  uses the shared gate. Review/lesson assignments, subject and component pages,
  `/user` and `/summary` (streaming parser) use it, as do the optional
  level/SRS/study-material/statistics requests. The error strings (e.g.
  `subjects JSON`) survive both attempts, and a structurally valid empty
  `/summary` is no longer treated as a parse failure.
- **API-05 — Attempts and age.** Cap attempts and show attempt/age information
  for stuck items. **Done (2026-10-29):** `OutboxState::Stuck` (`4`) and a
  50-attempt cap; a would-be retry becomes stuck instead of sending. Pending
  excludes stuck, and attention counts rejected + stuck for Home, Summary and
  the sync row. The OUTBOX screen lists both, shows `gave up`, attempts and age
  (`just now`/hours/days, or `age unknown` with a zero/untrusted timestamp) and
  dismisses only the selected seq. Flush skips stuck without blocking later
  items; dev `J` adds a stuck seed. Native-tested.
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
device.

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
- **TECH-02 — Refactor `main.cpp`.** **Partial (2026-10-29).** The Home,
  Sync, Boot, Loading, About, Outbox, Settings, setup portal and modal screens
  and the shared palette/text helpers are extracted: `app/HomeScreen` takes an
  explicit `HomeView` and draws the whole Home frame, `app/SyncScreen` takes a
  `SyncView` (six `SyncRowView`s, completion count, failure issue) and draws
  the whole Sync frame, `app/BootScreen` takes a `BootView` and keeps the state
  text in `main.cpp`, `app/LoadingScreen` takes a `LoadingView`, `app/AboutScreen`
  takes an `AboutView` with explicit `AboutLine`s, `app/OutboxScreen` takes an
  `OutboxView` (`OutboxRowView`, `OutboxReasonKind`) and draws the whole Outbox
  frame including the empty state, 7-row scrolling list and confirmation,
  `app/SettingsScreen` takes a `SettingsView` (`SettingsRowView`) and draws the
  whole Settings frame including the 7 visible rows of 12, selection highlight,
  scrollbar and note line, `app/SetupScreen` takes a `SetupView` and draws the
  whole setup portal frame including the AP and station layouts, battery state,
  228 px truncation and token/pack status, and `app/ModalScreens` takes
  `PauseView` and `LowBatteryView` (or no view for sign-out) and draws the
  Pause, low battery and sign-out frames; `main.cpp` keeps their key handlers,
  state and actions. `ui/Theme.h` holds the common colors and `ui/Text` holds
  `drawWrapped`. Review, Lesson, debug, the remaining drawing helpers, session
  state and services still live in `main.cpp`. 72 native tests pass; release
  build 1,417,949 B flash / 94,704 B static RAM, dev build 1,425,549 B /
  94,728 B.
- **TECH-03 — Dev hooks.** `KANIKEY_DEV` now covers the M0 screen and Tab
  shortcut; put `gDirectReading`, synthetic outbox commands and other serial
  helpers behind the same flag.
- **TECH-04 — PlatformIO CI. Done (2026-10-29).** The `firmware` workflow
  (`.github/workflows/firmware.yml`) runs `pio test -e native` and the
  `cardputer-adv` release build; PlatformIO and pip caches are keyed by
  `firmware/platformio.ini`.
- **TECH-05 — Host tests. Done (2026-10-29).** The retry predicate moved to
  native-buildable `wk/HttpRetry` (`httpRetryable`) and is table-tested across
  200/4xx/3xx/2xx and 0/5xx; the outbox capacity edge is covered too. 63 native
  tests pass; release build 1,403,689 B flash and 94,704 B static RAM. Pack/RLE
  reads, HTTP transport and outbox persistence stay platform-only.
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
  Settings. **Done:** review 10/20/30 and lesson 3/5/10, clamped to WaniKani's
  `lessons_batch_size`.
- **EXT-02 — Question order.** Meaning-first / reading-first and custom
  ordering. **Done for the pair:** Settings › `Order` toggles meaning-first /
  reading-first; custom ordering is still open.
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

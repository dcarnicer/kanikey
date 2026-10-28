# Decisions

Short log of design decisions. Status: **accepted** (2026-10-25). Format:
date, decision, why and what it implies.

---

## 2026-10-25 — Japanese glyphs come from the SD, not flash

**Decision:** review and lesson Japanese text is drawn from
`/kanikey/glyphs.kgp`, a `KKGP` v1 pack with 4bpp RLE glyphs at 64/32/20 px.
The pack is generated locally from the WaniKani API or Tangocho's WaniKani
kanji cache and is never committed.

**Why:** the app slot is ~1.75 MB and `efontJA_16` costs ~468 KB; the card has
30 GB. A 64 px glyph is also much better than a scaled 16 px font.

**Implications:** the SD card is required for Japanese in v1. Missing glyphs
show an explicit state instead of falling back to a font; the `efontJA_16`
reference was removed on 2026-10-28. The full tokenless pack is 2.9 MB for
6,621 glyphs; the token pack on the card has 7,269 glyphs and 498 radical
bitmaps.

---

## 2026-10-25 — 8-bit canvas, static response buffer

**Decision:** the canvas is 8-bit RGB332 (32,400 B), and `WkClient` reads
response bodies into a 32 KiB static buffer. During a review fetch the canvas
is freed only for each TLS request and restored before the body is parsed.

**Why:** a 16-bit canvas is 64,800 B. After 5–20 subject batches the largest
free block falls to 28–63 KB, so the 16-bit canvas cannot be reallocated; the
8-bit canvas always can. Keeping bodies out of the heap stops them from
splitting the canvas block.

**Implications:** the UI is 8-bit color; the theme colors still read correctly,
and `tools/shot.py` decodes RGB332. Static RAM is ~28% (32 KiB body, 8 KiB
upload buffer, 16 KiB USB RX). A streaming subject parser is still the right
long-term fix for fragmentation.

---

## 2026-10-25 — Wi-Fi asset upload, USB as fallback

**Decision:** a one-shot HTTP server on port 8080 (`S`) accepts a `PUT` under
`/kanikey/`; `tools/device.py push` runs `curl -T`. The USB `up` command writes
8 KiB chunks and waits for an ACK per chunk.

**Why:** moving a 2.9 MB pack over the USB serial link dropped bytes and was
slow; TCP over Wi-Fi completed the same file in seconds. USB remains useful for
small files and when Wi-Fi is unavailable.

**Implications:** the server is a development tool, not a production feature:
it listens only when explicitly started, accepts only `/kanikey/` paths and is
unauthenticated on the LAN.

---

## 2026-10-25 — KaniKey is a companion, not a substitute

**Decision:** KaniKey syncs with WaniKani and shows reviews/lessons. The
SRS, assignments and progress stay in WaniKani.

**Why:** running two schedulers creates divergence and duplicates work. The
value is in the device and in the writing, not in reimplementing WaniKani.

**Implications:** there is no FSRS for reviews; intervals are not invented; the API is
the source of truth.

---

## 2026-10-25 — Written answer, not a multiple-choice test

**Decision:** the user types the meaning and reading. There are no four options in
the main flow.

**Why:** it is the heart of WaniKani and the reason to use a keyboard. A multiple-choice
test would turn KaniKey into a worse Tangocho.

**Implications:** romaji→kana IME, answer checker, text editing and keyboard in two
modes. A "test mode" may come later as accessibility.

---

## 2026-10-25 — Offline by default, with an outbox

**Decision:** you can review without a network; completed reviews are saved in a
persistent outbox and sent when the connection returns.

**Why:** that is the appeal of a pocket device. Kakehashi shows it can be done
without blocking the session.

**Implications:** write before advancing, reconcile ambiguous submissions and show
pending status honestly. The API has no idempotency; you have to be careful
with timeouts.

---

## 2026-10-25 — Cardputer ADV only in v1

**Decision:** the firmware is optimized and tested only for the Cardputer ADV.

**Why:** the keyboard is the product; other boards change the layout and the
tests. Tangocho already maintains several.

**Implications:** the HAL for other boards can be kept in the fork, but support
is not promised. The layout is 240×135 landscape.

---

## 2026-10-25 — Pruned fork of Tangocho

**Decision:** KaniKey starts as a fork of Tangocho at v0.7.0, keeps HAL,
render, base UI, Wi-Fi, SD, OTA and tests, and drops packs/quiz/SRS/catalog.

**Why:** the hardware is already solved and tested; rewriting it is the expensive
and boring part.

**Implications:** keep the MIT notice, an `upstream` remote and an aggressive prune.
If both projects grow, extract a `cardputer-kit` later. The first milestone uses
a source copy of `board/`, `ui/KeyInput.h` and `platform/RootCerts.h` rather than
a full-history fork; that is an implementation detail, not a change of direction.

---

## 2026-10-25 — Visual pack on the microSD

**Decision:** kanji/vocabulary glyphs and radicals are generated on the
computer with `kanikey-glyphs` and stored in `/kanikey/glyphs.kgp` on the SD.

**Why:** a CJK font does not fit in flash; rasterizing on the device wastes
RAM and is more code. The pack gives quality and keeps the firmware simple.

**Implications:** the SD is required to review. The pack is generated with the user's
token and is not redistributed. If a glyph is missing, the item is blocked with a warning.

---

## 2026-10-25 — M1 uses efontJA_16 as a temporary glyph source

**Decision:** the first review screen renders kanji/vocabulary with M5GFX's
`efontJA_16` (16 px, scaled 2–3×) instead of waiting for the visual pack.

**Why:** the Home screen is live and the next visible milestone is a real review.
The font is already in M5GFX, covers ~10,800 Japanese glyphs and adds ~468 KB of
flash; the app still fits its 1.75 MiB slot at 88.6%.

**Implications:** this is a bring-up fallback, not a reversal of the visual-pack
decision. **Superseded on 2026-10-28:** the reference is removed and missing
glyphs show an explicit state.

**Decision:** the MVP focuses on reviews. Lessons come later.

**Why:** reviews already require API, cache, IME, answer checker, outbox and
screens. Adding lessons and mnemonics at the same time multiplies the scope.

**Implications:** the home screen can show the lesson counter from the start,
but the lessons screen is postponed.

---

## 2026-10-25 — English-only UI

**Decision:** interface texts, documentation, code and comments are in English.
There is no localization in v1. Meanings/readings are WaniKani's (English).

**Why:** the project is meant to be published and WaniKani content is English;
one language keeps the 240×135 UI and the string budget simple.

**Implications:** `UiStrings` still centralizes every on-device string, but only
the English table is built. Localization can be added later if wanted, but it is
not planned.

---

## 2026-10-25 — Streaming summary, DOM for small responses

**Decision:** `/summary` is parsed by a small streaming parser that keeps bucket
counts and at most 20 subject IDs per bucket. `/user` and the assignment/subject
pages still use the small JSON DOM.

**Why:** the summary's 568 subject IDs made the DOM large enough to throw
`std::bad_alloc` during boot on the Cardputer, especially after the canvas and
lesson code grew the heap pressure.

**Implications:** summary parsing no longer allocates a tree; the bounded IDs
are enough for a 20-item review/lesson batch. Subject pages remain the next
place where a truly streaming parser is needed if batches grow.

---

## 2026-10-25 — Idle dim, blank, then deep sleep

**Decision:** the panel dims after 30 s, blanks after 60 s and the device
deep-sleeps after 2 min when idle. Deep sleep is skipped while a review is
active and on external power; the outbox is already saved to LittleFS.

**Why:** a pocket device must not drain its battery on the Home screen, but a
review session must not disappear just because the user is thinking.

**Implications:** the first key on a blank panel only wakes it. On wake from
deep sleep the device boots and syncs again; the outbox retries pending reviews.

---

## 2026-10-25 — Strict checking with warnings

**Decision:** WaniKani's exact answers are accepted (including user
synonyms and whitelist). Valid non-primary readings, incorrect okurigana
and the `n` error show a warning and allow retrying, without counting
as a failure automatically.

**Why:** it is faithful to WaniKani and avoids punishing valid knowledge for a
keystroke. A "strict" setting can count the warning as a failure.

**Implications:** the answer checker returns a result with a type (`Precise`,
`Imprecise`, `Warning`, `Incorrect`) and the UI decides.

---

## 2026-10-25 — No backend of our own

**Decision:** the Cardputer calls `api.wanikani.com` directly with the user's
token. There is no KaniKey server.

**Why:** less infrastructure, less privacy to give up and less latency.
The API allows CORS and personal tokens.

**Implications:** TLS, rate limit and streaming JSON on the device. The setup
portal is local and temporary.

---

## 2026-10-25 — WPA2 and temporary setup portal

**Decision:** the configuration portal is served on its own WPA2 network, with a
random key on screen, and it shuts down when done. It is not served on the home
network.

**Why:** the token must not travel over an open network. A permanent portal is not
needed because the device does the syncing.

**Implications:** the user configures from the phone once; after that everything is done
with the keyboard. The portal reopens only when requested.

---

## 2026-10-25 — No WaniKani content in the repository

**Decision:** meanings, readings, mnemonics, radical images,
audio and context sentences are not committed. The test fixtures are minimal,
hand-written and attributed.

**Why:** WaniKani's terms and respect for Tofugu's work. The visual
pack is generated with the user's token.

**Implications:** the repo can be published without WaniKani material; setup
includes generating/copying the pack.

---

## 2026-10-25 — Sessions of 20 by default

**Decision:** a review session takes 20 items by default; "all" is an
option.

**Why:** on a small device, a 200-item session is a trap. 20 is
a manageable unit and can be repeated.

**Implications:** the summary offers "review more"; the queue is frozen per session.

---

## 2026-10-25 — Working name KaniKey

**Decision:** the project is called KaniKey until there is a better one.

**Why:** *kani* + *key*, short, evokes keyboard and kanji, and does not include
"WaniKani" in the product name (brand rule).

**Implications:** the name can change before publishing; the documents and the
namespace can keep using `kanikey`.

---

## 2026-10-25 — MIT license (proposed)

**Decision:** the code would be published under MIT, like Tangocho and Kakehashi.

**Why:** compatibility with the fork and with the copied ideas; it is the usual
thing in this kind of project.

**Implications:** keep the Tangocho/Kakehashi notices and the font
licenses. WaniKani content is not licensed with the code.

---

## 2026-10-25 — Audio and stroke order out of v1

**Decision:** they are not implemented in the MVP.

**Why:** they add audio decoding, memory, speaker and extra data. Written
review already justifies the device.

**Implications:** the design leaves room for them (item card, settings, visual
pack) but does not promise them.

---

## 2026-10-28 — efontJA_16 removed; missing glyphs are explicit

**Decision:** no CJK font is linked. Japanese text comes only from the visual
pack; feedback and readings try 32/20 px and a missing glyph shows `no glyph`.

**Why:** the radical bitmaps and the token pack cover the prompts, and the font
cost ~468 KB of a 1.75 MiB app slot. Flash fell from 1,741,705 B to 1,273,425 B
before the offline cache work.

**Implications:** a missing or incomplete pack is visible instead of silently
falling back. The real review/lesson prompts still need a configured account on
hardware.

---

## 2026-10-28 — Offline cache split between LittleFS and SD

**Decision:** persist a versioned snapshot (`/state/cache.bin`: summary, user
and batch assignments) in LittleFS, and the batch subjects
(`/kanikey/cache/subjects.bin`) on SD. Both use a token fingerprint, CRC32,
`.tmp` + rename and a `.bak` copy.

**Why:** Home and the next session must survive a reboot without network; the
subject payload is larger and belongs on the card, while the small mutable
snapshot fits LittleFS. The token fingerprint discards another account's data.

**Implications:** boot restores Home and a cached batch before any Wi-Fi
request; completed items are pruned from the cache and stay in the outbox.
`KKCA`/`KKCS` v5 add the study-material notes, the review presentation order
and the vacation timestamp, store the summary `data_updated_at`, mark caches
older than 24 h as stale, and Settings exposes `Sync now`/`Clear cache`. The
real online fetch → offline → reconnect E2E (CACHE-07) still needs a deliberate
live run.

---

## 2026-10-28 — Study materials are merged into the subject

**Decision:** review and lesson batches fetch `/study_materials` by
`subject_ids` and merge the user's synonyms and notes into the `Subject`; the
notes are part of the cached subject. Hidden materials are skipped, and a
missing study-materials permission does not fail the batch.

**Why:** the answer checker already understands synonyms, but it had no source
for them. Merging keeps one object for the checker, the lesson card, the review
feedback and the offline cache instead of a second lookup table.

**Implications:** `KKCS` v5 carries `meaning_note`/`reading_note`; older caches
are discarded. Notes appear on the lesson card and after a correct review
answer. The full item-detail screen (UI-04/WK-08) can reuse the same fields.

---

## 2026-10-28 — Review order follows WaniKani

**Decision:** `User` parses and caches `reviews_presentation_order`;
`ReviewQueue` shuffles the frozen batch and stable-sorts by subject level when
it is `lower_levels_first`. The local `Order` setting stays meaning-first /
reading-first.

**Why:** the account preference should apply without adding another device
toggle, and the local setting already means something else. Sorting after the
shuffle keeps variety inside a level.

**Implications:** `KKCA` v5 stores the order; older caches are discarded.
Offline sessions keep the last known order.

---

## 2026-10-28 — Vacation notice is a Home status line

**Decision:** `User` parses and caches `current_vacation_started_at`; Home shows
`Vacation mode` in the status line only when no sync/outbox/offline message is
more urgent.

**Why:** vacation mode changes what the user expects from reviews and lessons,
but it must not block the session or hide a real sync problem.

**Implications:** the timestamp is part of `KKCA` v5, so the notice works
offline. The account used for hardware tests is not on vacation, so the notice
is covered by native parsing/cache tests only.

---

## 2026-10-28 — SRS stage comes from the review response

**Decision:** the review header shows the assignment's current SRS stage, and
the session summary shows the stage parsed from
`resources_updated.assignment.data.srs_stage` in the `POST /reviews` response.
The previous stage is shown when the same session queued the review.

**Why:** the stage is the most useful immediate feedback after a review, and
the response already carries it. Parsing it avoids a second assignment request.

**Implications:** the outbox format does not change; a response that cannot be
parsed is still treated as sent and only skips the stage display. The parser is
native-tested. The header was verified on hardware with a synthetic cached
review; the summary line needs a successful live POST.

---

## 2026-10-28 — Lesson quiz is optional and local-only

**Decision:** `Q` on a lesson card opens a typed meaning/reading quiz with
retries, a mistake count and a completion screen. The quiz uses `AnswerChecker`
but no network or `/start`; Enter on the card remains the only way to start the
lesson.

**Why:** practice should help without turning the card into a gate, and a local
quiz must not change the WaniKani lesson flow.

**Implications:** `LessonQuiz` is pure C++ and native-tested. The completion
screen says the lesson was not started; the user returns to the card and
decides.

---

## 2026-10-28 — Two-phase setup portal, no lesson writes

**Decision:** first boot starts a WPA2 `KaniKey-XXXX` AP. The phone saves the
home Wi-Fi there; the device joins it and serves the second page at its station
IP. The token is validated with `GET /user` on that page, the browser sends the
clock and POSIX time-zone rule, and the pack can be re-checked. Setup ends with
**Start KaniKey** and the first sync.

**Why:** the AP-only page cannot reach WaniKani to validate the token. The
station page gives real validation without keeping the AP open, and the browser
is a reliable clock/zone source on a device without an RTC.

**Implications:** the token and Wi-Fi password are never logged or shown back.
Setup only reads `/user`; it never calls `PUT /assignments/{id}/start`, so it
cannot move lessons. The normal first sync afterwards may flush reviews already
waiting in the outbox. Tangocho's
deck catalog, upload, OTA and progress-restore routes are deliberately dropped.

---

## 2026-10-28 — Pause and settings are local-only

**Decision:** `Esc` in a review opens Continue / End session / Settings; `Esc`
from Home opens Settings. Brightness, dim/blank/sleep timings, review batch/order, lesson batch,
Setup portal, Sync now, About, Clear cache and Sign out are on the device;
settings persist in NVS.

**Why:** the setup portal removed the serial cable, so the same session should
also expose the device controls without one. Pausing instead of ending avoids
losing a session on a stray `Esc`, and a sync can be requested deliberately.

**Implications:** changing settings never calls WaniKani. `Sync now` may flush
the existing outbox and is a user action. Account/study preferences, clear-cache
and the auth/rate-limit screens are still open (SETUP-05/07).

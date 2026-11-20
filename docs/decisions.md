# Decisions

Short log of design decisions. Status: **accepted** (2026-10-25; round-2
entries through 2026-11-20). Format: date, decision, why and what it implies.
Cache versions named in dated entries are the version at that date; the current
files are `KKCA` v12 and `KKCS` v11.

Round-2 directions `docs/spec/README.md` §1 map to entries here: D1/D6 are the
2026-11-01 entries below; D2 is “Lesson batch flow (LES-01/02/03)”, D3 the two
settings entries, D4 the 2026-11-06 study/stats/audio entry and D5 the
2026-10-25 English entry.

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
**Superseded in part on 2026-11-19 (`ONB-01`):** the font pack is public and
account-independent; only the radical pack is token-generated and personal.
The card still holds both packs.

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

---

## 2026-10-25 — MVP focuses on reviews; lessons later

**Decision:** the MVP focuses on reviews. Lessons come later.

**Why:** reviews already require API, cache, IME, answer checker, outbox and
screens. Adding lessons and mnemonics at the same time multiplies the scope.

**Implications:** the home screen can show the lesson counter from the start,
but the lessons screen was postponed. **Superseded (2026-10-28):** the lesson
list, card, components and optional quiz are implemented and lessons are part
of v1.

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
**Superseded in part on 2026-11-01 (`REV-02`):** every reading with
`accepted_answer=true` is now correct (see “Readings follow WaniKani's accepted
flag”), and a listed but non-accepted reading is a warning. A close meaning
follows R6: WaniKani accepts it by default, while Strict warns without naming
the answer (see the 2026-11-19 REV-02 entry).

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
**Superseded in part on 2026-10-28:** the portal is two-phase. The AP page saves
Wi-Fi, then the station page is served on the home network behind a 6-digit
page code. On 2026-11-19 (`SETUP-09`) Settings › Account › Wi-Fi added the
on-device scan; the portal remains the token/time/pack path.

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

## 2026-10-25 — MIT license (accepted 2026-10-29)

**Decision:** the code is published under MIT, like Tangocho and Kakehashi.
The repository ships `LICENSE` and `THIRD_PARTY_NOTICES.md`.

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
`KKCA`/`KKCS` v9 (the current version at the time; v10 adds the Home SRS
counts) add the study-material notes, the review presentation order, the
vacation timestamp, the per-subject review statistics, the level progression,
vocabulary details and related-subject IDs, store the summary
`data_updated_at`, mark caches older than 24 h as stale, and Settings exposes
`Sync now`/`Clear cache`. **Update:** the real-account online fetch → Wi-Fi off
→ review → reconnect → 201 path is verified; a deliberate offline boot remains.

---

## 2026-10-28 — Study materials are merged into the subject

**Decision:** review and lesson batches fetch `/study_materials` by
`subject_ids` and merge the user's synonyms and notes into the `Subject`; the
notes are part of the cached subject. Hidden materials are skipped, and a
missing study-materials permission does not fail the batch.

**Why:** the answer checker already understands synonyms, but it had no source
for them. Merging keeps one object for the checker, the lesson card, the review
feedback and the offline cache instead of a second lookup table.

**Implications:** `KKCS` v9 carries `meaning_note`/`reading_note`; older caches
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

**Implications:** `KKCA` v9 stores the order; older caches are discarded.
Offline sessions keep the last known order.
**Superseded in part on 2026-11-19 (`REV-08`):** R2 `Order` adds Random, level,
SRS and oldest-available orders; `Account preference` (the default) keeps the
`reviews_presentation_order` behaviour described here. R4 `First question`
remains a separate meaning/reading/random setting.

---

## 2026-10-28 — Vacation notice is a Home status line

**Decision:** `User` parses and caches `current_vacation_started_at`; Home shows
`Vacation mode` in the status line only when no sync/outbox/offline message is
more urgent.

**Why:** vacation mode changes what the user expects from reviews and lessons,
but it must not block the session or hide a real sync problem.

**Implications:** the timestamp is part of `KKCA` v9, so the notice works
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

## 2026-10-28 — Review statistics are merged into the subject

**Decision:** review batches fetch `/review_statistics` by `subject_ids` and
merge the per-subject counts, streaks and `percentage_correct` into the cached
`Subject`. The review header shows the lifetime accuracy; the summary shows the
last item's review count and accuracy.

**Why:** the statistics are small, already keyed by subject, and belong with the
rest of the subject data used by the review screen and the future detail screen.

**Implications:** `KKCS` v9 carries the statistics; older caches are discarded.
A token without the review-statistics permission still reviews, without the
accuracy display. The parser is native-tested; the header and summary were
hardware-verified with the synthetic cached review.

---

## 2026-10-28 — Level progress is cached with the Home snapshot

**Decision:** sync fetches `/level_progressions` once, keeps the entry for the
current level and stores its timestamps in `KKCA`. Home shows the unlock date in
the low-priority status line when no sync/outbox/offline/vacation message is
more urgent.

**Why:** the level is already on Home; the unlock date gives context without a
new screen, and caching keeps it available offline.

**Implications:** `KKCA` v9 stores the progression; older caches are discarded.
A token without the level-progressions permission keeps the previous cached
value. The parser is native-tested; the real request and the Home line were
verified on hardware on 2026-10-28.

---

## 2026-10-28 — Review detail data is cached with the batch

**Decision:** review batches parse mnemonics/hints and fetch component subjects
in pages of 3, keeping them in the same subject list as the review items.
`Tab` on feedback opens a 5-page detail card (meanings/readings/synonyms,
meaning mnemonic, reading mnemonic, notes, components). The subject cache is
written from the vector directly, without copying every Subject.

**Why:** the detail card needs the same data online and offline; components are
small but a 10-subject page DOM is too large after the review subjects are
loaded. Copying the 40+ subjects into a second `SubjectCache` added another
large allocation and could abort during the cache save.

**Implications:** components are part of `KKCS` and count against the raised
128-subject limit. The detail card is read-only and never calls `/start`. The
data path is native-tested; the card was hardware-verified with a synthetic
cached review. A real component fetch reached 24/24 before the no-copy cache
save was added.

---

## 2026-10-28 — Vocabulary details are cached with the subject

**Decision:** parse `parts_of_speech` and up to four `context_sentences` when
subjects are loaded with details, cache them with the subject and show them on
a sixth detail page for vocabulary. `pronunciation_audios` stays phase 2.

**Why:** the fields are small, already part of the subject payload, and make the
detail card useful for vocabulary without another request.

**Implications:** `KKCS` v9 carries the fields; older caches are discarded. The
vocabulary page shows the parts of speech and up to two English context
sentences; the Japanese sentence is stored for a future audio/context view.

---

## 2026-10-28 — Related-subject IDs are cached

**Decision:** parse up to 32 `visually_similar_subject_ids` and 32
`amalgamation_subject_ids` per subject and cache them. The detail card adds a
related page with counts and the meanings of any related subjects already in
the batch.

**Why:** the IDs are small, already part of the subject payload and are the
input for the future constellation view. Showing counts and cached meanings
makes them visible without another fetch.

**Implications:** `KKCS` v9 carries the IDs; older caches are discarded. The
page is read-only and does not fetch missing related subjects; the constellation
view will do that later.

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
**Superseded on 2026-11-19 (`LES-01/02/03`):** lessons now browse the whole
batch, run a batch quiz (L3 `Required` by default) and start the batch only
after one confirmation. The `Q` single-item quiz remains optional local
practice.

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
the existing outbox and is a user action. Review/lesson preferences,
clear-cache, sign-out and the auth/rate-limit screens are implemented
(SETUP-05/07).
**Superseded on 2026-11-19 (`CFG-01/02/03`):** Settings is the eight-section,
48-row registry menu with `System › Reset settings`; the flat row list above is
the v1 state. SETUP-09 later added `Account › Wi-Fi`.

---

## 2026-10-28 — Home SRS distribution uses five `total_count` requests

**Decision:** sync requests `/assignments` five times with
`page_after_id=999999999&hidden=false` and one `srs_stages` bucket per request
(`1,2,3,4`, `5,6`, `7`, `8`, `9`). The cursor is past the last assignment, so
`data` is empty and only the collection's top-level `total_count` is parsed.
The five counts are cached in `KKCA` v10 and Home draws a 224 px segmented bar
with A/G/M/E/B labels.

**Why:** `/summary` has no stage breakdown and downloading all assignments to
count them would need multiple 500-item pages and a large JSON DOM. Five tiny
requests fit the existing client and keep the distribution available offline.

**Implications:** a sync adds five short requests (~350 B each) after the user,
summary and level requests; they are skipped when the sync already failed. A
partial failure keeps the previous cached counts, and a cache without counts
shows `SRS: sync for counts`. A successful live review moves one count between
buckets; the next sync replaces the whole distribution. The review queue still
comes from `/summary` and the per-batch `/assignments` request; the bar is
display-only.

---

## 2026-10-28 — Sync screen cancels at step boundaries

**Decision:** a sync remains synchronous on the main task but draws a dedicated
six-row screen (`Wi-Fi`, `Outbox`, `Account`, `Summary`, `Level`, `SRS`) before
and after each blocking request. `Esc` is polled between steps and cancels
cooperatively; failures stay on screen with retry/setup instead of disappearing.

**Why:** TLS requests need large contiguous heap blocks and are not safe to run
in a second task with the current client. A step screen gives useful progress
without a state-machine rewrite, and cooperative cancel avoids aborting a
request midway.

**Implications:** a cancel pressed during a request takes effect when that
request returns (SRS can take up to five requests). What already arrived stays
in the cache and the device returns Home, or to `Sync cancelled` when there is
no cache. An Account or Summary failure stops later steps; Level and SRS
failures are warnings and the sync completes. **Superseded in part on
2026-11-19 (`SPEED-02/03`):** the automatic boot/wake sync is now a background
job; `Sync now` keeps this modal screen, now seven rows with `Critical`.

---

## 2026-10-29 — Rejected and stuck reviews wait for the user

**Decision:** an outbox review refused by WaniKani (422/409/other non-auth 4xx)
is kept as `Rejected`; a review that has used 50 send attempts becomes `Stuck`.
Neither is resent automatically. Home shows `N need attention`; `O: outbox`
opens the list, and a confirmed discard removes only the selected sequence.
The row and confirmation show the HTTP code or `gave up`, the attempt count and
the age.

**Why:** the API has no idempotency key, so resending a refused or
possibly-delivered review can duplicate it. A rejection or give-up must remain
visible instead of disappearing, and the user needs enough context to decide.

**Implications:** the 24-byte record keeps its layout; bytes 22–23 now hold
`http_code` (old files load `0`), and `Stuck` uses state value `4`. Pending
excludes attention items, and attention counts rejected + stuck for Home,
Summary and the sync row. Native-tested; the final OUTBOX screen still needs a
hardware pass.

---

## 2026-10-29 — Shared 429/401 gates; 403 stays endpoint-specific

**Decision:** any 429 arms one shared monotonic gate using `RateLimit-Reset`
(clamped to 120 s, 60 s fallback), and every request returns a local 429 until
the deadline. Any real 401 arms one shared session gate; every request returns
a local 401 until setup starts, a token is saved/validated/cleared, or the user
signs out. A 403 is reported only for the endpoint that returned it. The JSON
and body helpers retry a truncated/unparsable 200, `httpCode == 0` or 5xx once;
other 4xx and 429 are not retried.

**Why:** retrying a rate limit immediately makes it worse, and continuing to
send a revoked token cannot succeed. A missing scope may affect only one
resource, so it must not block reviews or lessons that still work.

**Implications:** the outbox POST keeps a 401/403 item `Pending` and stores a
deferred auth issue that survives a later successful `/user` or `/summary`
until the outbox drains. A reconciliation blocked by either gate stays
`Ambiguous`. The gates and retry helpers are native/host-inspected only;
hardware verification is pending.

---

## 2026-10-29 — OTA: GitHub manifest, two slots, rollback

**Decision:** a tagged GitHub release publishes the app-only
`kanikey-cardputer-adv.bin` and a tab-separated `kanikey-firmware.txt` manifest
with the version, size and SHA-256. The device checks
`releases/latest/download/kanikey-firmware.txt` after a successful sync at most
once a day and from Settings › Firmware, streams the image into the spare
1.75 MiB slot, verifies size and SHA-256, and switches the boot slot only after
`Update.end()` accepts the image. The new image is marked valid after 10 s of
the loop; a failure before that rolls back to the previous slot, and the failed
version is not offered again.

**Why:** the partition table already had two OTA slots; the security properties
match the existing HTTPS root CAs and `Update`, and the rollback keeps a bad
image from bricking a pocket device. A daily check adds one small request and
makes releases discoverable without a phone.

**Implications:** release builds require HTTPS and ignore the serial manifest
override; only a `KANIKEY_DEV` build may use a local `http://` manifest for
testing. The first OTA-capable image is flashed over USB. Settings, the outbox
and the cache survive an update. `tools/release/firmware_manifest.py` checks
the tag against `Version.h` and the image against its slot.

---

## 2026-10-29 — Lazy review batch and dev-only serial hooks

**Decision:** Home loads the cached summary, user and counters at boot but not
the review subjects; `startReview()` loads the batch from disk on demand, and
the batch is evicted after a review and before a lesson fetch. The loop task
stack is 24 KiB, the USB RX ring is 4 KiB and the USB uploader sends 2 KiB
chunks. Only the development image exposes the serial token/Wi-Fi/probe
commands, synthetic outbox/cache seeds, the remote key helpers and the
`gDirectReading` answer bypass. Release keeps `h`, `i`, `p`, `S`, `up` and `r`.

**Why:** keeping 20 review subjects alive during a lesson fetch left the TLS
handshake with as little as 3.5 KB free and blocked the canvas in some runs;
the dev image also overflowed the 8 KiB loop stack. The lazy batch raised the
lesson-fetch minimum to ~56 KiB and the 24 KiB stack leaves ~17 KiB of
headroom. Test hooks that can inject answers or synthetic data do not belong in
a release binary.

**Implications:** starting a review after a lesson or a reboot reads the batch
from the card (no network round trip); the snapshot's assignment list is
preserved when the subjects are evicted. `h` now prints the loop stack
high-water mark. The trade-off is one extra disk read per review session.

---

## 2026-11-01 — Readings follow WaniKani's accepted flag

**Decision:** any reading with `accepted_answer=true` is a correct answer for a
kanji, primary or not. A reading that exists on the subject but is not accepted
is a warning, not a mistake; it names the type WaniKani wants (`on'yomi`,
`kun'yomi` or `nanori`) when the accepted readings share one type, otherwise it
says "That reading is not accepted for this item". Vocabulary and kana
vocabulary are unchanged.

**Why:** WaniKani shakes without a penalty for a listed but non-accepted reading
and marks every accepted reading correct. The previous primary-vs-valid warning
in strict mode punished answers WaniKani accepts, and a non-accepted reading
counted as wrong.

**Implications:** `strict` now only affects meaning typos; the reading branch no
longer has a primary-reading warning. A rejected reading is still a warning, so
it is not counted and not sent to WaniKani.

---

## 2026-11-01 — Typo tolerance is measured on the expected answer

**Decision:** a close meaning is judged with the length of the expected answer,
not the typed input: up to 3 characters allow no typo, up to 5 allow one, up to 8
allow two and longer answers allow three. This is KaniKey's existing curve;
WaniKani does not publish its rule, so the final policy belongs to REV-02.

**Why:** using the input length let a long wrong answer become "close" to a short
answer: 一 + "none" matched "one" and 手 + "handle" matched "hand".

**Implications:** short answers accept only exact spelling, while a one-character
typo in a long answer still shows the warning in strict mode. The boundary is
covered by native tests; it must be re-checked against WaniKani when REV-02
decides the policy.

---

## 2026-11-01 — A warning hides the answer and keeps the typed text

**Decision:** a `Warning` (typo, reading-as-meaning, n' hint, rejected reading)
shows only the `CHECK` pill and the message, never the typed or answer row.
`Tab` is ignored, the hint bar reads `Enter: edit  Esc: pause` (reviews) or
`Enter: edit  Esc: card` (lesson quiz), and `Enter` returns to the same question
with the meaning string and IME raw buffer intact. The strict typo message is
`Close. Check the spelling` and never names the candidate answer.

**Why:** the previous warning screen drew the correct answer and cleared the
input, so typing the reading in the meaning field revealed the meaning and gave
a free retry.

**Implications:** a warning is still not counted and not sent to WaniKani; only
the retry behaviour changes. The lesson quiz keeps its input for the same
reason. `tests` cover the message text and that existing warning cases still
return `Warning`.

---

## 2026-11-01 — The UI keeps the built-in fonts; docs follow the device (D1)

**Decision:** Latin UI text stays on M5GFX `Font0`/`Font1` (the 6×8 GLCD
font), with `Font2` for titles and big values and `Font4` for large titles;
Japanese prompts and readings come only from the SD pack at 64/32/20 px.
`docs/screens` and the README show the device's RGB332 colours and these
fonts, not a Noto UI font.

**Why:** a rasterized UI font costs flash and RAM without improving a 240×135
screen, and the mockups had drifted from the panel.

**Implications:** no UI font file ships; `tools/mockups/render.py` uses the
bitmap fonts and `Theme.h` colours, and its Noto Sans JP copy is only the
Japanese pack stand-in. DOC-01/DOC-02 own the images and the text.

---

## 2026-11-01 — Round-2 tasks are delegated and reviewed (D6)

**Decision:** each round-2 task runs in its own git worktree, is implemented by
a subagent against its spec file and is reviewed before merge. A reviewer
checks the task's acceptance boxes, runs the native tests and both device
builds, and does the listed hardware checks.

**Why:** the tasks overlap in the app controllers and the shared components;
one branch per task group avoids conflicts, and a single review gate catches
memory, flash and wire-format regressions.

**Implications:** `review` means implemented and waiting for the reviewer;
`done` also needs the hardware checks. The workflow and the brief template are
in [`docs/spec/README.md`](spec/README.md) §7.

---

## 2026-11-06 — Review history lives on the device

**Decision:** KaniKey never calls `GET /v2/reviews` (deprecated; it returns an
empty `data` array). Every completed review appends a 16-byte record to
`/state/journal.bin` on LittleFS. Records sit in blocks of 32 with a `{seq,
CRC32}` trailer; 125 blocks form a 4,000-record ring. The header stores the
account key, next slot, count and its own CRC. A torn write costs at most the
active block, and a different account key recreates the file. Sign-out and
token clear delete it; Settings › Clear cache does not, because history is not
cache. The Stats History page aggregates the last seven local days, today's
item accuracy (reviews without a wrong part), current streak and best streak;
the best-streak scan keeps at most 400 distinct days in RAM.

**Why:** the API no longer provides history, and the outbox is not a history
store: it only holds unsent reviews and is deleted on sign-out.

**Implications:** a review with an untrusted clock (`completedAt == 0`) is kept
but cannot be placed on a day. The journal serves STATS now and is the shared
input for STUDY (critical items/recent mistakes) and REM later.

The subject catalog is `/kanikey/cache/catalog/` on the SD card: one `KKSB`
file per subject (payload byte-identical to the KKCS subject payload), plus a
`KKCT` meta file with the account key, count and bytes. It is account-scoped,
caps at 2,000 subjects or 8 MB, and evicts by smallest `lastUsed` (ties by
subject id). Sign-out clears it; Clear cache does not need to because it is not
the Home cache. The SD mount now allows four file handles so the pack, a
catalog file and a directory scan can coexist.

Critical (leech) items use WaniKani's own `percentage_correct`, never a local
recalculation: the full sync asks `/review_statistics` for percentages strictly
below the `Critical below` setting (default 75%) and at least `Critical min`
answers (default 5), joins the active assignments, drops hidden and burned
items, and stores the worst-first list in
`/state/critical.bin`. `Settings › Critical first` is Off by default; when on,
the review queue stable-partitions due critical subjects to the front before
applying the batch limit. STUDY-03/04/05 can read the same list for practice
and the visible critical-items screen.

Vocabulary audio is split into a spike, a cache and playback. The parser keeps
`pronunciation_audios` as transient URL/content-type/voice clips, but they are
not written to KKCS. The cache stores clips as
`/kanikey/audio/<subject>-<voice>.<mp3|ogg>` with a `KKAU` index, a 50 MB cap
and LRU eviction; serial `a` fills it, and clear cache/sign-out delete it. The
ES8311 is enabled so the hardware run can measure the speaker path and play the
serial `n` test tone. Playback and automatic prefetch stay blocked until clip
sizes and a decoder's flash/RAM fit are measured on the S3.

Extra study is local practice, never a WaniKani session: it reuses the review
queue and UI, has no outbox item, never calls `POST /reviews` or `PUT /start`,
does not change due counts, SRS or review statistics, and journals every
completion with `practice=true`. Recent mistakes uses the journal, Critical
items uses `/state/critical.bin`, and Recent lessons/Current level use
`/state/practice.bin`, a 256-assignment apprentice pool refreshed on sync.
Subjects come from the SD catalog; missing ones are fetched online and stored
there, otherwise they are skipped and counted in `N items skipped`. Settings
S3/S4/S5 control the two windows and the session size (Apprentice/24 h, 24 h,
10 by default).

---

## 2026-11-19 — App.cpp is split into controllers over one AppState

**Decision:** `app/App.cpp` keeps only `setup()`, `loop()`, the `draw()` and
`onKey()` routers and the Home/Boot adapters. The cache, sync, outbox, review,
lesson, stats, study, settings, setup, power and dev-console flows live in
their own controllers, and `app/AppState.{h,cpp}` owns the single `AppState`
instance returned by `state()`. The fields keep their old names so the move is
mechanical and reviewable. Controller headers declare only the entry points
another translation unit calls; the rest stays inside the `.cpp`.

**Why:** every wave-2 task needed `App.cpp`, so parallel agents conflicted on
every hunk. The split gives each task its own file without changing components,
wire formats, key bindings or UI.

**Implications:** cross-controller calls are explicit through the headers
(Review↔Outbox↔Cache, Sync→Outbox/Cache/Study, Settings→Setup/Sync). Small
shared subject-text helpers (`subjectMainMeaning`, `correctAnswer`) moved to
the pure `wk/SubjectText.h`. `main.cpp` and `App.h` are unchanged.

---

## 2026-11-19 — Settings registry and generic NVS access (ARCH-02)

**Decision:** `wk/SettingsRegistry.cpp` is the single table of setting IDs,
NVS keys, defaults, choices and help text; `wk/SettingsModel` provides the
pure lookup/cycle/validate/value-label functions and a fixed-capacity
`SettingsCache`. `platform/DeviceSettings` loads the keyed rows once at boot
through `settingsGetInt`/`settingsSetInt`; the seven existing getters/setters
become wrappers with unchanged NVS keys and defaults. The six STUDY-era keys
kept direct getters until CFG-02 added them to the registry.

**Why:** CFG-01/CFG-02 added about 25 rows; without one table, each setting
means changes in three places and nothing can be tested without hardware. The
only visible change is that a stored value outside a choice list falls back to
its default, as ARCH-02 specifies.

**Implications:** `settingsAction()`/`drawSettings()` were untouched, so the
flat 19-row menu stayed until CFG-01. A no-op change performs no NVS write. The
dev-only `Z <seconds>` idle-sleep hook bypasses the choice list with
`settingsSetSleepSecondsDev`; normal callers stay validated. CFG-02 owned the
remaining rows and the settings section UX and landed on 2026-11-19.

---

## 2026-11-19 — Lesson batch flow (LES-01/02/03)

**Decision:** lessons follow D2: browse every selected card, take a batch quiz
(L3 `Required` by default, `Skip` optional), then start the whole batch with
one confirmation. `Enter` on a card only pages/advances; it never sends
`PUT /assignments/{id}/start`. The start loop treats 2xx and 422 as started,
stops on any other code, keeps the unstarted items and offers retry. The `Q`
single-item quiz remains optional and local. Started subjects leave the
summary and the snapshot immediately.

**Why:** the old per-card `Enter` started a lesson irreversibly while paging,
and only the last card could be quizzed. The batch flow matches WaniKani and
makes the start deliberate.

**Implications:** this supersedes the 2026-10-28 “Lesson quiz is optional and
local-only” decision for the batch gate, not for the `Q` practice. The batch
quiz and the start never write review/SRS state; only the confirmed start loop
writes, one `PUT` per item with a `lessonStarted` journal record.

---

## 2026-11-19 — Sectioned settings and the registry of record (CFG-01/02/03)

**Decision:** Settings is an eight-section menu backed by one 48-row registry
(39 keyed settings and 9 actions) with the IDs, labels, choices, defaults, NVS
keys and help text from `docs/spec/03-settings.md`. `Enter`/`Right` cycle
forward, `Left` back, `Esc` pops one level; destructive rows (including the
on-device Wi-Fi action) are locked during a review; `System › Reset settings`
removes only registry keys (the legacy `order` included) and keeps the token,
Wi-Fi, time zone, cache and outbox.

**Why:** the flat 19-row list could not hold the round-2 settings, and every
row had its label/value duplicated in the controller and the screen. One
registry makes the values testable natively and lets CFG-02 add settings
without touching the UI.

**Implications:** R4 migrates `order` to `rv.first`; the six STUDY keys, L3,
Y1–Y3, P5/P6 and M1–M7 are registry-backed. CFG-04's `?` overlay landed on
2026-11-19. A stored value outside a choice list falls back to the default.

---

## 2026-11-19 — Feedback lists every accepted answer and a useful hint (REV-01/03)

**Decision:** review and lesson-quiz feedback show every accepted meaning and
reading, with user synonyms faint and a two-line fit, instead of one answer.
A wrong answer shows the first sentence of the part's hint, else the user note;
Warning keeps the checker message; the generic `Incorrect meaning`/`Incorrect
reading` line is gone.

**Why:** telling a learner that 作文 means only `Composition` when `Writing`
and `Essay` are also accepted is misleading, and the old wrong-answer line
spent the only free line repeating the NOT QUITE pill.

**Implications:** the answer token helpers are pure and native-tested; the
lesson quiz uses the same layout. REV-02 and the other review options landed in
their own 2026-11-19 entries; the hardware feedback checks remain.

---

## 2026-11-19 — Quick, background sync and review prefetch (SPEED-02/03/04)

**Decision:** boot and wake choose `Skip`, `Quick` or `Full` from the cached
sync times (Y1/Y2); the automatic sync is a background job that performs at
most one request per loop pass and keeps Home visible with `sync n/m` in the
header. Manual `Sync now` stays modal (seven rows) and always runs `Full`. A
full automatic sync prefetches the next review batch when reviews are due, the
cached batch is shorter than R1 and the battery is not low.

**Why:** every wake paid a 10-request, 20–25 s modal sync before Home appeared,
and `Enter` then paid another 8–10 requests for the batch. A quick plan and a
prefetched batch remove both waits.

**Implications:** cache v12 adds `lastFullAt`, `lastQuickAt` and `lastUserAt`;
v11 still loads with those times set to `savedAt`. Requests stay sequential.
`Enter` during an automatic sync starts a cached review at the next boundary,
`Esc` opens Settings, and the job pauses while another screen owns the display.
Y1–Y3 and R1 are registry-backed. Hardware checks remain partial (see
[`docs/hardware.md`](hardware.md), 2026-11-19).

---

## 2026-11-19 — TLS keep-alive with an idle-age fix (SPEED-05)

**Decision:** keep one `WiFiClientSecure` over HTTP/1.1 + `Connection:
keep-alive` for a sync step, batch/lesson fetch or outbox flush, with chunked or
`Content-Length` bodies decoded in place and one fresh-connection retry. The
canvas stays released until `wkSessionEnd()`; non-session callers keep HTTP/1.0.

**Why:** the first merge stalled at `Outbox` after `[sync] step Time -> Outbox`
with no `[http]` line. The root cause was not the HTTP session: `App.cpp`
captures `now` before a sync step refreshes `gLastInputAt`, so `now - last`
underflowed to ~49 days, the panel blanked, and the modal sync then waited for
a key. A serial byte woke it. Clamping the idle age (and waking a modal sync if
the panel is already off) removes the stall.

**Implications:** `wk/IdleTimer.h` is pure and tested; dim/blank/sleep all use
it. A reused connection failure retries once, but a partially written POST is
not retried and follows the ambiguous/outbox path. The hardware run completed
35 reused requests in one session with `[sync] complete`; the TLS spike,
timing/heap targets, parsed chunked `/summary`, idle-drop retry and REV-08
option B still need their checks.

---

## 2026-11-19 — Home recomputes from the cache every minute (SPEED-07)

**Decision:** while Home owns the screen it ticks once a minute, recomputes the
due count and `Next review` from the cached `/summary` buckets and redraws,
which also refreshes the battery. With an untrusted clock, `effectiveNow()`
advances the last cache save with uptime and re-anchors when a sync writes a
newer `savedAt`.

**Why:** a review that became due stayed invisible until the next sync, and the
battery reading only changed when a key forced a redraw.

**Implications:** no network request and no new cache field; the tick is
skipped while Settings, Pause, Stats, Outbox, the portal, a review, a lesson or
the manual sync screen is open. Drift on a long uptime is possible and SNTP
corrects it at the next sync; the 60 s tick and the offline-clock line still
need the hardware run (see [`docs/hardware.md`](hardware.md), 2026-11-19).

---

## 2026-11-19 — Auto-advance, back-to-back and the minimal header (REV-06/09/12)

**Decision:** three review options are live behind registry rows. R8
`Auto-advance on correct` is Off by default and offers 0.5 s, 1 s or 2 s;
typing a printable key during CORRECT feedback advances immediately and feeds
the key into the next field, while wrong/warning feedback, the detail card,
pause, Settings and the blank screen never advance. R5 `Back-to-back` is On by
default; Off interleaves correct and wrong answers, and a single-item queue
still asks the other part. R10 `Header info` `Minimal` hides only the
SRS+accuracy block.

**Why:** the three options were already specified in the review wave, and
their defaults keep the existing Enter-only, back-to-back and SRS-header
behaviour for users who change nothing.

**Implications:** the rows persist in NVS and apply without a reboot. Hardware
checks are pending. R6 `Typos`, R7, R9, R12 and R2/R3 landed in their own
2026-11-19 entries, so the review wave is implemented; its hardware checks
remain.

---

## 2026-11-19 — On-device Wi-Fi setup (SETUP-09)

**Decision:** Settings › Account › Wi-Fi opens a 2.4 GHz scan with a manual
SSID row for hidden networks and a masked password field; credentials are
written only after `WL_CONNECTED`, and a failed join keeps the password
editable while the saved network stays untouched. The boot “Wi-Fi needed”
state offers the on-device flow on Enter and the phone portal on `P`; the
portal remains the token and time-zone path.

**Why:** changing networks should not require a phone or a serial cable, while
the portal must stay as the fallback and as the only first-boot token entry.

**Implications:** the registry grew to 48 rows (Account now has three). 5 GHz
entries are filtered in both the scan and the pure builder; the join runs
before saving, so a typo cannot replace a working network. The
join/save/failure, hidden-SSID, boot-flow, review-lock and power-guard checks
remain on hardware.

---

## 2026-11-19 — Lesson ordering, workload guard and topic pages (LES-04/05/06)

**Decision:** L2 `ls.order` chooses WaniKani (default), type groups
(`Radicals>Kanji>Vocab`) or interleaved types over the existing 60-candidate
pool. L4/L5/L6 are evaluated locally before any fetch or the low-battery
prompt; the guard lists each exceeded reason, `Enter` continues and `Esc`
returns Home. A lesson card is split by topic (`Meaning`, `Reading`,
`Components and notes`, `Context`) and long topics continue on extra pages with
the title repeated.

**Why:** the batch flow needs a predictable order, and a user with too many
apprentice items or due reviews should see that before spending Wi-Fi and a
lesson slot; one dense card also hid the topic structure.

**Implications:** the lessons started-today count comes from the ARCH-03
journal and is local to this device. The guard returns without a fetch, so the
cache and lesson pool are unchanged. The multi-reason guard, L5/L6, the
60-candidate ordering check and a radical/vocabulary page set remain on
hardware.

---

## 2026-11-19 — Typo policy: WaniKani or Strict (REV-02)

**Decision:** R6 `rv.typos` selects the default WaniKani policy (a close meaning
within tolerance of at least four characters is CORRECT, with
`Close: <answer>` and no mistake) or Strict (CHECK, retry the same part, answer
hidden). Readings keep their current strictness, and reviews plus both lesson
quizzes read the same row.

**Why:** WaniKani accepts a small typo as correct, and the old default marked
it as a warning and asked for a retry; the strict policy stays available for
users who want it.

**Implications:** this supersedes the 2026-10-25 “Strict checking with
warnings” default for meanings only. The received answer is not sent
differently; the queue sees Correct or Warning, so the accuracy bookkeeping and
auto-advance follow the policy. Hardware checks under both policies remain.

---

## 2026-11-19 — Font and radical packs are separate (ONB-01)

**Decision:** pack v2 uses the reserved header word as `kind` (0
combined/legacy, 1 font, 2 radical) with the same body and CRC. The card
carries `/kanikey/glyphs.kgp` (font, no WaniKani content, buildable without a
token) and `/kanikey/radicals.kgp` (personal, token-generated); v1 combined
packs still open for both roles. A missing radical pack affects only radical
prompts (`no glyph`), while a missing font keeps the existing `Pack missing`
boot state.

**Why:** the combined file made the whole pack personal, so every user needed a
token and Python before the device worked. Separating the font allows a public,
verified download, while the radical bitmaps remain personal.

**Implications:** two open files and two caches instead of one (+1,148 B static
RAM, +1,936 B flash measured). `build.py --split DIR` writes both and verifies
v2 kind/table consistency; the public font build is 9.36 MB. Both-pack boot,
missing-radical boot, v1 fallback and both upload paths remain hardware checks;
ONB-02 provides the portal download/upload (2026-11-19).

---

## 2026-11-19 — Reviews are submitted by policy, not on every Enter (SPEED-01)

**Decision:** R12 `Submit reviews` selects At session end (default), Every 5 or
Immediately. A completed item is written to the local outbox first; the
summary (or the every-5 checkpoint) runs a bounded send loop over the records
queued by this session, one request at a time. `Esc` stops the loop and leaves
the rest Pending for the next sync. A bounded `StageTracker` keeps each
record's pre-review stage, replacing the old last-pair globals.

**Why:** with the old one-POST-per-completion behaviour every Enter after a
review paid the network wait; the default now lets the user finish the batch
and sends at the summary, where progress and `Esc: stop` are visible.

**Implications:** the outbox is the source of truth and the same reconciliation,
auth/rate gates and offline handling apply. Immediately keeps today's
behaviour; Every 5 sends the session's batch after the 5th/10th completion and
flushes the tail at the summary. The summary shows `Sending n/m`, a bar, stage
moves and the outbox count. Each queued record carries its journal handle in
the tracker, so every POST patches its own `srsAfter`, not only the last one.
The hardware summary/Esc/offline/Every-5/Immediately and heap checks remain.

---

## 2026-11-19 — Screens are base screens plus a modal stack (ARCH-04)

**Decision:** one pure `wk/ScreenModel` owns the base screen and a four-deep
modal stack. Pause, Settings, About, sign-out, the Outbox discard confirmation,
the low-battery prompt and the firmware-install confirmation are modals; lesson
start and the batch quiz stay sub-states of the lesson screen. `draw()` and
`onKey()` dispatch to the top modal first, and `screenCanSleep()` is the only
sleep predicate. Stats, Extra study, Critical items, on-device Wi-Fi and
notices remain separate screens or overlays that block sleep.

**Why:** about twenty booleans allowed invalid combinations such as
`gAboutActive && gPauseActive`, and the draw, key and sleep orderings had
drifted apart. A stack makes the reachable states explicit and gives `Esc` one
meaning: pop one level.

**Implications:** Pause → Settings → About returns About → Settings → Pause
without `gSettingsOrigin`, and no `gShowHome`/`gReviewActive`/… flag remains.
The model is pure and native-tested. The return chain passed on 2026-11-19;
sleep/dim, firmware install, low-battery and debug routes remain hardware
checks.

---

## 2026-11-19 — One list, hint and text primitive set (TNG-03)

**Decision:** the Tangocho-derived `ui/ListView` window math and row frame,
`ui/HintBar` preferred-first width fallback, `ui/TextLayout` wrapping/ellipsis/
UTF-8 trimming and the 1 bpp `ui/Icons` tables are the shared primitives. Home
hints, Settings, the Wi-Fi scan, Outbox, the lesson list and the Study/critical
windows use them; lesson, critical and study rows keep their custom 20 px
painters. `UiFont` is not ported and the icons are not adopted in screens yet.

**Why:** every list carried its own scroll-window and clipping code, and a
wide hint could collide with the right side. One tested rule keeps the 240×135
screens consistent and makes a new list cheaper to add.

**Implications:** the window math and fallbacks are native-tested, and a
migrated list keeps its own fonts and row content, so the layout is unchanged.
The remaining scroll/window hardware cases and icon adoption are follow-ups;
`UiFont` stays out because the UI keeps the built-in fonts (D1).

---

## 2026-11-19 — The public font pack ships from the release workflow (ONB-01/02)

**Decision:** the release workflow builds the v2 kind-1 font pack from a fixed
kana + JIS X 0208 set with pinned Noto Sans JP and Pillow, verifies it,
publishes it as `glyphs.kgp` beside the app image and adds a
`pack font <name> <bytes> <sha256>` row to the OTA manifest. The setup station
page downloads that record through `glyphs.kgp.tmp`, checks exact size and
SHA-256, validates the font kind and reopens it; a personal `radicals.kgp`
arrives through the local upload form (or a manual card copy). `Check pack
again` is the explicit full-CRC path.

**Why:** the split pack made the font safe to publish, but most users should
not need Python, a token and a computer to install it. The radical bitmaps
remain WaniKani-derived and must stay personal.

**Implications:** a tagged release must build and attach the pack; the firmware
update path does not require a `pack` record, but the setup page uses the
`font` one. The download checks HTTPS, size, SHA-256 and kind; the upload checks
size, free space and the v2 kind-2 structure but not the CRC. Empty-card
download, cut Wi-Fi, valid and truncated upload, `Esc` and heap checks remain
on hardware (see [`docs/hardware.md`](hardware.md), 2026-11-19).

---

## 2026-11-19 — Light sleep before deep sleep (SPEED-06)

**Decision:** after the blank delay, with P5 `Light sleep` On and on battery,
the device enters `board::lightSleep()` instead of staying awake until the deep
sleep deadline. The keyboard INT, G0 and an optional timer wake it; a key
restores the same screen, a timer wake falls through to the P4 deep-sleep
check. P6 `Sleep during review` Never keeps review screens awake as before;
After 10 min ends the session with `endReview(false)`, saves the cache and
deep-sleeps. The dev `Z` command enters a 10 s light sleep for the spike.

**Why:** the blank panel left the CPU, Wi-Fi and RAM running; a pause could not
resume because deep sleep was the only low-power state and always cold-boots.

**Implications:** `SleepPolicy` is pure and native-tested; `wokeFromSleep()`
stays EXT1-only and `wokeFromTimer()` is separate. `powerWakeCheck()` now runs
`powerTick()` while blank, so the P4/P6 deadlines still fire with P5 Off. The
timer path woke on hardware; current draw, key/G0 latency, first-key, Wi-Fi
rejoin, panel recovery, P4/P6 deadlines and USB checks remain.

---

## 2026-11-20 — BT-01: BLE HID host is a conditional go on NimBLE

**Decision:** BT-02 may start with the source-neutral key seam and the pure
HID mapping/diff tests. The BLE transport is a conditional go on
NimBLE-Arduino (pinned 2.5.2; 1.4.3 also compiles), not on the core's
Bluedroid `esp_hidh`. Bluedroid is a complete HOGP host but a compiled probe
added ~553 KB flash / ~20.9 KB static RAM, so it does not fit the current
1.75 MiB slot and its 15.6 KB task-stack floor makes the TLS heap gate
unlikely. NimBLE added ~201 KB / ~6.4 KB in the same probe; HOGP is our code.
No library or partition table is merged, and no hardware pair/typing, heap or
current measurement was taken.

**Why:** the no-PSRAM heap is the deciding constraint, and the current image
leaves 170,787 B in the slot (not the 278,019 B in the 2026-11-19 brief).
Both need a larger slot (A 2.5 MB; B at least 2.25 MB, 2.5 MB recommended);
NimBLE leaves far more flash and static-RAM margin, and it lets the probe
start pairing at a point `esp_hidh` does not expose.

**Implications:** the seam can ship even if BT is dropped (PaperDIY buttons
use it). The BLE transport ships only after three keyboards pair and type,
Just Works and one passkey/numeric flow pass, reconnect/latency and 1,000-key
tests pass, and BLE-on-TLS keeps at least 8 KB free / 4 KB largest. Details
and the probe commands are in `docs/spec/10-bluetooth-paperdiy.md` §1.7.

---

## 2026-11-20 — AUD-03: minimp3, MP3 only, on-demand first

**Decision:** AUD-03 is a conditional go on a vendored minimp3
(`MINIMP3_ONLY_MP3`, CC0) with the 16,236 B decode scratch made static, and
`M5.Speaker.playRaw()` for PCM output. Only `audio/mpeg` clips play; an
OGG/Vorbis entry stays cached but is treated as missing. The first cut is `R`
on demand and stops on navigation; M6 autoplay waits for the hardware run.

**Why:** a desktop cross-compile probe on the pinned Arduino-ESP32 2.0.17 /
IDF 4.4.7 toolchain added 16,384 B flash and 22,904 B static RAM for minimp3;
Helix added 32,488 B flash and 23,816 B heap. Helix is RPSL/RCSL, not a clean
fit for an MIT app, and the pinned core has no MP3/OGG decoder; the newer
3.3.12 package's precompiled Helix is not selected by platform 6.12.0.
minimp3's stock 17,344 B decode stack does not fit the 24 KiB loop stack; the
static scratch patch drops it to 1,104 B. The API delivers MP3 and OGG for the
same clip; the official example is 8,689 B MP3 / 8,535 B OGG, 44.1 kHz mono,
0.52 s.

**Implications:** MP3 only; the cache must prefer `audio/mpeg`, and an
OGG-only clip shows `no audio` and makes no request. About 30 KB RAM
(decoder, scratch, two PCM buffers, file buffer) must be measured against the
no-PSRAM heap; the latest slot figure is 170,787 B free, so the 16.4 KB
decoder fits. Go/no-go gates: at least 2× real time decode, at least 8 KB free
and 4 KB largest after playback plus TLS, no underrun or key lag, first sound
under 100 ms. Helix is the fallback only if that decode gate fails and the
license is accepted. No decoder or playback code is merged.

---

## 2026-11-20 — REM-02: device-local reminder wake, no push

**Decision:** the reminder is the device's own timer. Before the idle deep
sleep the device computes the next wake from the cached summary: the first
review bucket after now (only when M1 is on) and the next M3 time, each moved
past quiet hours (M2) and capped at 24 h. A timer wake starts no Wi-Fi and no
TLS. It shows the banner `N reviews ready` for 30 s when the cached due count
meets M1 (or any due review when only M3 is on) and the moment is outside quiet
hours; a key in that window continues the normal boot, and otherwise the chip
arms the next wake and sleeps again. The Home banner replaces the
next-review row, not the header.

**Why:** the server cannot push to a device with no Wi-Fi at rest, and an
extra request on each wake would spend the battery the reminder is meant to
save. The cache already holds the buckets, so a wake needs only the local
clock.

**Clock across deep sleep:** RTC memory keeps the UTC time at the moment the
timer is armed and `rtcMicros()` at that moment; a timer wake rebuilds the time
as their sum with the elapsed RTC time. `time()` is never read before SNTP, and
a device without a trusted clock arms no timer. The local offset comes from the
POSIX TZ rule through `localtime_r`. DST is handled in the pure module: a 02:30
daily time in the spring gap normalises to 03:30, and in the autumn overlap it
takes the first occurrence. A DST change during sleep can move one wake by up
to an hour, and the next recompute corrects it.

**Implications:**
- Both M1 and M3 at their Off defaults arm nothing and read no clock, so a
  release device with defaults sleeps exactly as before.
- M1 Off does not disable the M3 time: it reminds when any review is due.
- The reminder wakes at the first future bucket, not at the first bucket that
  reaches M1. A high M1 can cost several timer wakes a day; the wakes are cheap
  because they do not join Wi-Fi.
- The reminder never fires on USB power: the idle deep sleep is battery only.
- The reminder re-arms without writing the cache, since nothing changed on a
  timer wake. A key during the banner runs the normal quick sync after boot.
- The fired time lives in one boot only. The next wake is always strictly later
  than the fired one, so a reminder cannot repeat at the same moment.
- Hardware numbers (wake cost, the 30 s expiry, the banner width) are still
  open; see `docs/hardware.md` when the run is done.

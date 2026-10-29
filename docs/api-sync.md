# KaniKey — API, cache and synchronization

> Design, 2026-10-25; implementation notes through 2026-10-29. This document is
> the contract between the firmware and the WaniKani API, and between the
> firmware and local storage.

---

## 1. Principles

1. **WaniKani is the source of truth.** KaniKey does not reschedule reviews or
   invent SRS stages. It only caches, displays and sends.
2. **The cache is explicit.** Every piece of data has a place, a lifetime and an
   invalidation rule. No such thing as “whatever is in RAM”.
3. **The outbox is sacred.** A completed review is written to disk before the
   screen advances. A power cut cannot lose it.
4. **A network failure is not a review failure.** It gets queued and reported.
5. **Never a full JSON in RAM.** The API returns pages of up to 1,000 subjects;
   they are read as a stream and written to disk.
6. **No WaniKani content in the repository.** Test fixtures are minimal,
   hand-written and attributed.

---

## 2. The v2 API in one page

| Concept | Value |
|---|---|
| Base | `https://api.wanikani.com/v2/` |
| Revision | `Wanikani-Revision: 20170710` header |
| Auth | `Authorization: Bearer <token>` |
| Format | JSON; collections with `data`, `pages`, `total_count` |
| Pagination | `page_after_id` / `page_before_id`; 500 per page (1,000 for subjects/reviews) |
| Filters | `updated_after`, `ids`, `levels`, `subject_ids`, `immediately_available_for_*` |
| Rate limit | 60 requests/minute; `RateLimit-*` headers |
| Conditional | `If-None-Match` / `If-Modified-Since`; 304 with no body |
| Errors | 401, 403, 404, 409, 422, 429, 5xx; body `{error, code}` |
| Subscription | `max_level_granted` (3 or 60); always respect it |
| Brand | The product is not called WaniKani; own UI; no official art |

### 2.1 Collections and resources

```
{
  "object": "collection",
  "url": "...",
  "pages": { "next_url": "...", "previous_url": null, "per_page": 500 },
  "total_count": 1600,
  "data_updated_at": "2017-11-29T19:37:03.571377Z",
  "data": [ ... ]
}
```

A resource is `{id, object, url, data_updated_at, data}`.

### 2.2 Dates

ISO 8601 in UTC, with microseconds and `Z`. KaniKey only needs seconds.
`available_at`, `started_at`, `passed_at`, `burned_at`, `created_at` may be
`null`.

---

## 3. Endpoints used

| Method | Endpoint | What for | Frequency |
|---|---|---|---|
| GET | `/user` | User, level, subscription, preferences | Every sync (boot or manual) |
| GET | `/summary` | Reviews/lessons per hour, `next_reviews_at` | Every sync (boot or manual) |
| GET | `/assignments` | Stages, `available_at`, assignment IDs | Per review/lesson batch; five SRS count requests, plus one of the two level count requests (passed kanji) per sync |
| GET | `/subjects` | Meanings, readings, components, mnemonics | When the batch IDs need data; plus one of the two level count requests (kanji total) per sync |
| GET | `/study_materials` | User synonyms and notes | Per review/lesson batch |
| GET | `/review_statistics` | Per-subject accuracy and streaks | Per review batch |
| GET | `/level_progressions` | Current level and level-up timestamps | Every sync |
| POST | `/reviews` | Send a completed review | Per item |
| PUT | `/assignments/{id}/start` | Start a lesson | Per lesson |

The Level step makes two collection count requests: `/subjects` for the level's
kanji total and `/assignments` for the kanji that passed (Guru or later).

There is no ETag/conditional-request path in the current client; `304` is listed
below for completeness only.

Optional for phase 2:

| Method | Endpoint | What for |
|---|---|---|
| GET | `/reviews` | History, streak, heatmap |

### 3.1 `/user`

Fields that are stored:

- `data.username`, `data.level`
- `data.subscription.active`, `type`, `max_level_granted`, `period_ends_at`
- `data.preferences.lessons_batch_size`
- `data.preferences.reviews_presentation_order` (`shuffled` /
  `lower_levels_first`)
- `data.preferences.reviews_display_srs_indicator`

`data.current_vacation_started_at` is shown as a notice if it is not `null`.

### 3.2 `/summary`

```
data.lessons[]: { available_at, subject_ids[] }
data.reviews[]: { available_at, subject_ids[] }
data.next_reviews_at
```

- It is a report, not a collection.
- The buckets are hourly. For the home screen, the `subject_ids` of buckets
  with `available_at <= now` are added up.
- The sum does not replace assignments for reviewing: it only gives the counter.
- The response is cached with the Home snapshot and replaced on every sync;
  after a session the local counter is adjusted. A snapshot older than 24 h is
  shown as stale rather than blocked.
- A structurally valid summary with empty `lessons`/`reviews` arrays and no
  `next_reviews_at` is an all-clear account, not a parse failure. The streaming
  parser requires the `data`, `lessons` and `reviews` keys to be present.

### 3.3 `/assignments`

Filters used by the current client:

| Filter | Use |
|---|---|
| `subject_ids=...` | Fetch the assignment for a review/lesson batch |
| `srs_stages=...` | Home distribution buckets |
| `levels`, `subject_types` | Passed kanji count for the current level |
| `hidden=false` | Exclude hidden from the distribution and level counts |
| `page_after_id=999999999` | Ask for an empty data page in the count requests |

`immediately_available_for_review`, `immediately_available_for_lessons` and
`updated_after` remain design filters for a future global sync; the current
queue comes from `/summary` and is resolved by `subject_ids`.

The Home distribution makes five small collection requests with
`srs_stages=1,2,3,4`, `5,6`, `7`, `8`, `9`, `hidden=false` and
`page_after_id=999999999`; the cursor is past the last assignment, so `data` is
empty and the response is ~350 bytes. It reads only the top-level
`total_count`. A partial failure keeps the previous cached counts instead of
replacing the bar with a mixed snapshot.

The Level step makes one more count request for the current level:
`/assignments?levels=<level>&subject_types=kanji&srs_stages=5,6,7,8,9&hidden=false&page_after_id=999999999`.
A passed kanji is Guru or later (stages 5-9). If this or the `/subjects` total
fails, both level counts stay 0 and the cached timestamps remain valid.

Fields:

```
id, subject_id, subject_type, srs_stage,
unlocked_at, started_at, passed_at, burned_at,
available_at, resurrected_at, hidden, data_updated_at
```

- `srs_stage` 0 = lesson; 1–8 = review; 9 = burned.
- `available_at` may be `null` (not scheduled).
- `hidden=true` is neither shown nor reviewed.
- `subject_type` may be `radical`, `kanji`, `vocabulary`, `kana_vocabulary`.
- For the queue: `srs_stage >= 1`, `available_at <= now`, `hidden=false`.
- For lessons: `srs_stage == 0`, `started_at == null`, `hidden=false`,
  `unlocked_at != null`.

### 3.4 `/subjects`

Filters: `ids=...` is used by the current batches; `levels=...`, `types=...`,
`hidden=false` and `page_after_id=999999999` are used for the current level's
kanji total. `updated_after=...` remains a design filter.

Common fields:

```
id, object (radical|kanji|vocabulary|kana_vocabulary),
data.level, data.characters (may be null for radicals),
data.meanings[] { meaning, primary, accepted_answer },
data.auxiliary_meanings[] { meaning, type: whitelist|blacklist },
data.lesson_position, data.meaning_mnemonic, data.reading_mnemonic,
data.meaning_hint, data.reading_hint,
data.component_subject_ids[], data.amalgamation_subject_ids[],
data.visually_similar_subject_ids[],
data.character_images[] (radicals only; SVG),
data.parts_of_speech[] (vocabulary),
data.context_sentences[] (vocabulary; up to 4 stored),
data.pronunciation_audios[] (vocabulary, phase 2),
data.readings[] { reading, primary, accepted_answer, type }
```

- Reading `type`: `onyomi`, `kunyomi`, `nanori` (kanji); in vocabulary it is
  usually missing.
- A radical's `characters` may be `null`: the image has to be drawn.
- Mnemonics carry `<radical>`, `<kanji>`, `<vocabulary>`, `<meaning>`,
  `<reading>` markup; it is stripped when displaying.
- `document_url`, `slug`, `created_at`, `hidden_at` and
  `spaced_repetition_system_id` are not stored in v1 (they are not needed).

**Implementation (2026-10-28).** Review batches parse the detail fields
(`meaning_mnemonic`, `reading_mnemonic`, hints, `parts_of_speech` and
`context_sentences`) and fetch the component subjects in pages of 3. A
10-subject component page is ~20 KB and its DOM can exceed the largest free
block after the review subjects are loaded. Components are kept in the same
subject list and cached, so the detail card works offline. Vocabulary context
sentences are stored (up to 4) and the English sentence is shown on the sixth
detail page; `pronunciation_audios` remains phase 2 (EXT-06). The related IDs
(`visually_similar_subject_ids`, `amalgamation_subject_ids`, up to 32 each) are
stored and the detail card adds a related page with counts and any meanings
already present in the batch.

The Level step requests `/subjects?levels=<level>&types=kanji&hidden=false&page_after_id=999999999`
and reads only the top-level `total_count` for the level's kanji denominator;
the cursor is past the last subject, so `data` is empty and the response is a
few hundred bytes.

### 3.5 `/study_materials`

Fields: `subject_id`, `meaning_note`, `reading_note`, `meaning_synonyms[]`,
`hidden`. The synonyms are used as correct answers. They are requested only for
subjects in the current batch, by `subject_ids`.

**Implementation (2026-10-28).** Review and lesson batches request the
materials for their own subject IDs in groups of 10, after the subjects are
parsed. Notes are whitespace-collapsed, hidden materials are skipped and the
synonyms are de-duplicated. Everything is merged into the `Subject`, so the
answer checker and the lesson/feedback cards use it and the subject cache keeps
it offline. A 403 on `/study_materials` is logged and the batch continues
without notes: a token without that permission can still review and study. A
401 is different: it arms the session-wide auth gate and the review/lesson
flow stops to ask for a new token.

### 3.6 `POST /reviews`

Body:

```json
{
  "review": {
    "assignment_id": 1422,
    "incorrect_meaning_answers": 1,
    "incorrect_reading_answers": 2,
    "created_at": "2026-10-25T08:42:13Z"
  }
}
```

- `incorrect_*` are required and may be 0.
- `created_at` is optional; if omitted, the server uses the request time.
- It must be in the past and later than `assignment.available_at`.
- The response carries `resources_updated.assignment` and
  `resources_updated.review_statistic`. The current client uses the assignment's
  SRS stage for display; `available_at` is parsed but not written back, and the
  statistic is not merged from this response (it comes from the next
  `/review_statistics` batch).
- There is no idempotency key. See §6.

**Implementation (2026-10-28).** The 2xx response is parsed for
`resources_updated.assignment.data.srs_stage` and `available_at`. Only the SRS
stage is displayed/applied; `available_at` is not written back to the cache.
The review header shows the assignment's stage before completion; the session
summary shows the stage returned by the POST (with the previous stage when the
same session queued it). A 201 with a body that cannot be parsed still counts as
sent; only the stage display is skipped.

### 3.7 `PUT /assignments/{id}/start`

Body:

```json
{ "assignment": { "started_at": "2026-10-25T08:42:13Z" } }
```

- `started_at` is optional; it defaults to now.
- It requires `srs_stage == 0`, `started_at == null`, `unlocked_at != null` and an
  allowed level.
- It returns the assignment with `srs_stage = 1` and `available_at`. The current
  client only checks the status; the refreshed data arrives with the next
  batch/sync.

### 3.8 `/review_statistics`

Fields used: `subject_id`, `meaning_correct`, `meaning_incorrect`,
`reading_correct`, `reading_incorrect`, `meaning_max_streak`,
`meaning_current_streak`, `reading_max_streak`, `reading_current_streak`,
`percentage_correct`, `hidden`.

**Implementation (2026-10-28).** Review batches request statistics by
`subject_ids` in groups of 10, after subjects and study materials. The fields
are merged into the cached `Subject`; the review header shows the subject's
lifetime accuracy and the session summary shows the last item's review count
and accuracy. A token without the review-statistics permission still reviews,
without the accuracy display.

### 3.9 `/level_progressions`

Fields used: `level`, `unlocked_at`, `started_at`, `passed_at`, `completed_at`,
`abandoned_at`.

**Implementation (2026-10-29).** Sync fetches the collection once and keeps the
entry for the current level (falling back to the highest level). It then makes
two tiny `total_count` requests for that level: `/subjects?levels=<level>&types=kanji&hidden=false&page_after_id=999999999`
for the kanji total, and
`/assignments?levels=<level>&subject_types=kanji&srs_stages=5,6,7,8,9&hidden=false&page_after_id=999999999`
for the kanji passed (Guru or later). If either count fails, both are stored as
0 and the timestamps remain valid; the counts never fail the Level step on
their own. The snapshot stores the timestamps and counts in `KKCA` v11. Home
shows the low-priority status line `L8 42% since 03 Feb` (rounded
`passed * 100 / total`, 0 when the total is unknown) when no
sync/outbox/offline/level-up message is more urgent; without counts it keeps
`L8 since 03 Feb`. The level-up notice is not shown without a previous level,
is cleared when a new token is saved or the parsed level decreases, and lasts
until sign-out. A token without the level-progressions permission keeps the
previous cached value.

---

## 4. Token and permissions

- Personal WaniKani token; never username/password.
- Required permissions (the WaniKani portal groups them by resource):
  - Read **assignments**.
  - Read **subjects**.
  - Read **study materials**.
  - Read **review statistics**.
  - Read **level progressions**.
  - Read **user** and **summary**.
  - Create **reviews**.
  - (Phase 2: read **reviews**.)
- It is stored in NVS; it is shown masked; it is deleted on logout.
- It is validated with `GET /user` before storing it.
- If the token lacks permissions, `403`; a missing-permissions state is shown
  and updating the token is offered. A 403 is per endpoint and does not stop
  requests that are still in scope (for example, study materials can fail while
  reviews work).
- If the token is revoked, `401`; returning to setup is offered. A 401 arms a
  session-wide gate: later requests are answered locally with 401 until setup
  starts, the token is saved/validated/cleared, or the user signs out.

---

## 5. Synchronization model

### 5.1 States

| State | What it means | What the UI shows |
|---|---|---|
| `cold` | No cache or no token | Setup / first sync |
| `warm` | Cache and token, network available | Normal home |
| `syncing` | Requests in progress | Progress |
| `offline` | No network, with cache | `Offline - 2h old` |
| `stale` | Old cache (more than 24 h) | `Stale cache - 2d old` |
| `auth_error` | 401 | `Auth: token rejected` (Home) / `Token rejected` (Boot) |
| `forbidden` | 403 | `Auth: missing permissions` / `Missing permissions` |
| `rate_limited` | 429 and the shared gate is active | `Rate limited; retry in 42s` |
| `server_error` | Persistent 5xx | `WaniKani error; retry` |

### 5.2 Cold start

```
1. Mount LittleFS and microSD
2. Load the outbox; load the cached Home/user/batch if the card/pack and
   account match
3. If the token or Wi-Fi is missing: use the cache if there is one, otherwise
   open the setup portal
4. Otherwise: run the sync below
```

- Boot draws the cached Home before Wi-Fi work, so a saved cache is usable
  offline.
- A missing or unreadable cache with a configured token goes to sync; an
  unconfigured device goes to setup. Neither is a hard error.

### 5.3 Sync (boot or manual)

```
1. Join Wi-Fi; wait up to 5 s for SNTP
2. Flush the outbox (see §6)
3. GET /user                     → user, level, max_level_granted
4. GET /summary                  → counters (streaming parser)
5. GET /level_progressions       → current level timestamps
   GET /subjects + /assignments  → level kanji total and passed counts
6. Five GET /assignments count requests for the SRS bar
7. The summary/level/SRS steps save the snapshot as they succeed
```

- Order matters: the outbox first, so a later `/summary` reflects the reviews
  that were sent.
- Account and Summary are blocking steps: an error stops the sync and leaves
  the sync screen on `Sync failed`. Level and SRS are non-critical; a failure
  is a warning (or a still-valid cached value) and keeps the previous data.
- If the outbox flush fails due to network, the sync continues with the
  remaining steps. No assignment list is fetched by the sync itself in any case.
- If the flush hits 422/409, that item is marked `rejected` with its HTTP code
  and kept for the Outbox screen; the flush continues with the next pending
  item. A 401 arms the session gate and a 403 is reported for the endpoint;
  both leave the item `Pending` (see §6).
- A 429 arms the shared rate-limit gate; later requests are answered locally
  until the reset and the sync reports the countdown.

**Not implemented yet:** the global `assignments`/`subjects` delta, ETag/304 and
`updated_after` warm sync. Review and lesson batches fetch their own
`/assignments`, `/subjects`, `/study_materials` and (reviews only)
`/review_statistics` by `subject_ids`; those records are merged into the cached
batch.

### 5.4 Review session

```
session_start:
  - if the batch is not cached: flush the outbox, then fetch it (not a full sync)
  - due = summary subject IDs with stage 1..8, available_at <= now, hidden=false
  - filter out those already in the outbox
  - sort according to reviews_presentation_order (shuffled by default; if it is
    lower_levels_first, respect it) and freeze the list
  - fetch assignments/subjects/materials/statistics for the batch
  - if a subject is missing: do not offer the item; if none remain, the
    session does not start

item_complete:
  - write outbox (assignment_id, counts, created_at, subject_id)
  - if there is network: try an immediate POST
      - ok: apply assignment, delete from outbox
      - network: leave pending
      - 429/5xx/timeout: leave pending or ambiguous (see §6)
  - if there is no network: leave pending
  - advance
```

- There is no background or 15-minute sync; boot and the manual `S`/Settings ›
  `Sync now` action run the sync in §5.3.
- The session list does not change if the server adds reviews in the meantime.
- Completed items are removed from the local queue even if the POST remains
  pending.
- At the end, the summary shows `N pending` or `N need attention` if an outbox
  remains.

**Implementation (2026-10-28).** `ReviewQueue` shuffles the frozen batch and
then stable-sorts by subject level when the cached `User` says
`lower_levels_first`; the shuffle stays inside each level. The local `Order`
setting is separate and only selects meaning-first or reading-first.

### 5.5 After syncing

- `summary` is refreshed; Home recalculates the due/lesson counters and the
  next review time from it.
- There is no separate “N new reviews” banner; the Home counters are the
  authoritative display.
- If an outbox item was rejected (422/409) or reached the attempt cap, it is
  marked `rejected`/`stuck` with its HTTP code or attempt count and kept; it is
  not deleted or reconciled automatically. Home shows `N need attention` and
  `O: outbox` opens the list to review or discard it.
- If the token expires, the outbox is kept and reauthentication is requested.
  An outbox 401/403 is retained as a deferred auth issue, so a later successful
  `/user` or `/summary` does not hide it; it clears when the outbox drains or
  the token is validated/changed.

---

## 6. Outbox and ambiguous sends

### 6.1 Record

```
offset  field              type
0       seq                u32   local monotonic
4       assignment_id      u32
8       subject_id         u32
12      incorrect_meaning  u16
14      incorrect_reading  u16
16      created_at         u32   UTC epoch; 0 = omit
20      state              u8    pending | sending | ambiguous | rejected | stuck
21      attempts           u8
22      http_code          u16   0 = unknown; rejection reason
        (record size 24 B)
```

- It is written to LittleFS with `temp + rename` and a `.bak` copy; the file
  header carries the CRC32 over the records.
- `http_code` uses the two reserved bytes at offset 22; the record and the file
  version do not change. Files written before this field load with `0`.
- `sending` is marked before the POST; if there is a power cut, the next flush
  treats it like `ambiguous` and reconciles it, not blindly as `pending`.
- `rejected` is kept until the user sees it; the Home status line shows
  `N need attention` and `O: outbox` opens the list. Discarding is confirmed
  and removes only the selected `seq`, never a pending, sending or ambiguous
  item.
- `attempts` counts send attempts, including offline and gate-blocked calls
  that never reached WaniKani; after 50 attempts an item that would be retried
  is marked `stuck` before the next POST. Stuck is not pending and is never
  sent again, but it is kept and dismissed exactly like `rejected`.
  `N need attention` counts rejected + stuck.
- `http_code` records 422/409 (or another rejection code) so the Outbox screen
  can explain why the review was refused. The detail view also shows the
  attempt count and the age from `created_at` (`just now`, `2h`, `3d`); a zero
  `created_at` or an untrusted clock shows `age unknown`.

### 6.2 Flush

1. Sort by `seq`.
2. Skip `rejected` and `stuck`; they are not failures and do not block later
   items.
3. For each `pending` (and each `sending`/`ambiguous`, reconciled first):
   - Before the POST, if `attempts >= 50`, mark `stuck`, save and continue with
     the next item without sending.
   - POST.
   - 2xx: use the returned SRS stage for display; delete.
   - 401: arm the session-wide auth gate and stop; later requests are answered
     locally with the stored 401. The item stays `Pending` and is remembered as
     a deferred auth issue.
   - 403: report missing permissions and stop this endpoint; the item stays
     `Pending` and is remembered as a deferred auth issue. Other endpoints are
     not gated because the missing scope may be local to one resource.
   - 422/409: mark `rejected` with the HTTP code; keep it for the Outbox
     screen.
   - 429: wait for `RateLimit-Reset`; leave pending. The shared gate makes the
     next attempt local until the reset (no immediate retry).
   - connect/DNS/TLS failure before the request is sent: leave pending and stop
     this flush.
   - timeout or `httpCode == 0` after the request was sent, or a 5xx: mark
     `ambiguous`; stop the flush (do not continue with the following ones to
     avoid creating a mess).
4. `ambiguous` ones are resolved before retrying:
   - `GET /assignments?ids={assignment_id}` (not the single-resource path).
   - If `data_updated_at` is later than our `created_at` **and** `available_at`
     is later than `created_at`, assume the review arrived; delete it and prune
     the cache.
   - Otherwise the current implementation sets it back to `pending` and lets
     the next flush retry it. This is a heuristic: the API has no idempotency
     key, so a review that arrived without changing `available_at` can be
     duplicated. The UI never calls this “sent”.
   - If the shared 429 or 401 gate is active during reconciliation, keep the
     item `ambiguous` and stop rather than retrying the POST.

### 6.3 Why not retry blindly

The API does not accept an idempotency key. A timeout after the server has
processed the review produces a duplicate if it is resent. Duplicating a review
alters the SRS stage and the user's statistics. The client reconciles with the
assignment before retrying; when the assignment cannot prove delivery, the
current heuristic retries, and that duplicate risk remains documented in §6.4.

A rejected or stuck review is never resent automatically. The user opens
Outbox from Home, sees the stored HTTP code or `gave up`, the attempt count and
the age, and either keeps the item or confirms a local discard. Dismissing only
removes the unsent outbox record; it does not change WaniKani and does not touch
the cache or assignments.

### 6.4 Limitations

- Reconciliation is heuristic: another device could have reviewed the same item
  in the same window. If the assignment does not prove delivery, the current
  path retries, so a duplicate remains possible.
- If the user reviews in KaniKey and on the web at the same time, there may be
  conflicts.
- The outbox is not synchronized between devices.

**Implementation (2026-10-29).** `attempts` is incremented before each POST and
saturates at 255. After 50 started attempts the next send is replaced by
`stuck` (state value 4, same 24 B record and file version). The flush skips
rejected/stuck items without stopping later pending ones; the Outbox screen
lists both with `HTTP <code>` or `gave up` and shows attempts/age in the
confirmation.

---

## 7. Cache

| Data | Where | Lifetime | Invalidation |
|---|---|---|---|
| Token, Wi-Fi, settings | NVS | Until logout | User action |
| `user` | LittleFS snapshot | Until next sync | Sync / token change |
| `summary` | LittleFS snapshot | Until next sync; stale after 24 h | Sync / after session |
| Assignments | LittleFS snapshot | Until next sync | Sync / review response |
| Subjects | microSD | Immutable by ID | `data_updated_at` if refreshed |
| Study materials | merged into Subjects (microSD) | Until subject refresh | New review/lesson batch |
| Review statistics | merged into Subjects (microSD) | Until subject refresh | New review batch |
| Visual pack | microSD | Until regenerated | Builder version / new characters |
| Session stats | RAM only | Current session | Session end / reboot |

### 7.1 Sizes

- A full assignment cache would be ~10,000 × 19 B ≈ 190 KB with the current
  inline record; the snapshot keeps only the batch (tens of records).
- Outbox: 128 × 24 B ≈ 3 KB plus the 16-byte header.
- Subjects: ~300 B on average + mnemonics; ~20 MB if all are cached; that is why
  they live on the SD card and are requested by ID.
- Visual pack: 3.7 MB for the token pack (2.9 MB tokenless); SD card.

### 7.2 Atomic writes

- Write `.tmp`, `fsync`/`close`, rename over the destination.
- Keep a `.bak` of assignments and outbox.
- On startup, if the main file fails CRC, use `.bak` and warn.
- Never delete the outbox before the POST is confirmed.

**Implementation (2026-10-29).** The Home/user/batch snapshot is
`/state/cache.bin` (`KKCA`, version 11); the batch subjects are
`/kanikey/cache/subjects.bin` (`KKCS`, version 11) on SD. Version 3 added the
study-material notes to each cached subject, version 4 added the review
presentation order, version 5 added the vacation timestamp, version 6 added the
per-subject review statistics, version 7 added the level progression, version 8
added vocabulary parts of speech/context sentences, version 9 added the
related-subject IDs, version 10 added the Home SRS distribution counts and
version 11 adds the level kanji total/passed counts;
older files are discarded. Both carry the save time, an
FNV-1a fingerprint of the API token, the payload length and CRC32.
The snapshot also stores the summary's `data_updated_at`; assignments keep
their own `data_updated_at` for outbox reconciliation. Loading a snapshot
written for another token or an older version discards both files. The cached
batch is started offline and completed items are removed from the snapshot;
the outbox is pruned from the cache on boot and after a successful send.
Settings › `Sync now` refreshes the snapshot, `Clear cache` invalidates it, and
a cache older than 24 h is shown as stale.

---

## 8. Streaming JSON parser

The API returns pages of up to 1,000 subjects; an ArduinoJson DOM does not fit.
Design:

```
HTTPClient / WiFiClientSecure
        │ bytes
        ▼
JsonStream (4 KB buffer)
        │ events: object_start, key, string, number, bool, null, array_start...
        ▼
SubjectSink / AssignmentSink / UserSink / SummarySink
        │ records
        ▼
store (LittleFS or SD)
```

- Own parser, no dependencies, with a context stack of maximum depth 8.
- Decodes `\uXXXX` escapes (including surrogate pairs) to UTF-8.
- Long strings (mnemonics) are written directly to the record; they are not
  copied to a global `std::string`.
- A subject object is built in a fixed ~8 KB structure; if a mnemonic exceeds the
  budget, it is stored truncated with a flag.
- If the parser fails, the page is discarded, it is retried once and
  `Bad response from WaniKani` is shown.
- Native tests with fixtures and fuzzing.

Rejected alternative: download the JSON to the SD card and parse it afterwards
with a DOM. It still needs a DOM and duplicates temporary storage.

---

## 9. Time

- The API uses UTC. The UI shows local time.
- The Cardputer has no battery-backed RTC; time is learned by:
  1. SNTP when connecting.
  2. `POST /time` from the setup portal (browser time).
  3. Last known time stored in NVS.
- `clock_trusted` is only true if the time was synchronized in this boot or if
  there is a stored time less than X hours old and there has been no power cut.
- Offline review:
  - If `clock_trusted`: `created_at` = completion time.
  - If not: `created_at = 0`, it is omitted; the server will use the send time.
- When sending a `created_at`, check that it is later than `available_at`; if
  not, omit it.
- Time zone: an offset/POSIX TZ is stored for display; it does not affect the API.

---

## 10. Errors, retries and rate limit

| Code | Meaning | Action |
|---|---|---|
| 200 | OK | Apply |
| 200, truncated / invalid JSON | Incomplete response | Retry once, then report the parse error |
| 304 | Not modified | Not used yet; the client sends no conditional request |
| 401 | Invalid token | Arm the session-wide auth gate; stop until token validation/save, setup start, sign-out or dev token clear; outbox items stay Pending |
| 403 | No permissions / subscription | Per-endpoint only; explain and keep the outbox item Pending; do not gate other requests |
| 404 | Resource does not exist | Discard, refresh |
| 409 | Conflict | Mark rejected; keep until the user discards in Outbox |
| 422 | Validation | Mark rejected, do not retry; keep until the user discards in Outbox |
| 50 attempts | Retry cap | Mark stuck before the next POST; not pending, keep until the user discards in Outbox |
| 429 | Rate limit | Wait for `RateLimit-Reset`; shared gate, no immediate retry |
| 5xx | Server | Retry once in the GET helpers; an outbox POST becomes `ambiguous` |
| DNS / TLS / connect failure before sending | Network | One retry in the GET helpers, then offline; outbox POST stays `pending` |
| timeout after sending / `httpCode == 0` | Response unknown | One retry in the GET helpers; outbox POST becomes `ambiguous` |

- Retry policy: the JSON/body helpers make two attempts with a 300 ms delay for
  a truncated/unparsable 200, `httpCode == 0` or 5xx. Other 4xx and 429 are not
  retried. The direct outbox POST/PUT and reconciliation GET are single-shot.
- 429: use `RateLimit-Reset` (epoch) if present; otherwise 60 s. A far-future
  reset is capped at 120 s; no request starts before the shared gate deadline.
- Budget: a normal sync < 30 s; a cold one < 2 min.
- No requests are made in parallel. One at a time.
- Wi-Fi is joined for setup/sync/serial work; review and lesson input do not
  require the radio.

**Implementation (2026-10-29).** `WkClient` stores a real 401 in one shared
session-wide auth gate. While it is armed, every GET/POST/PUT returns a
synthetic `connected=true` 401 without opening TLS, calling the request hooks or
touching the body buffer, so no endpoint retries the rejected token. The gate is
cleared when setup starts, a token is saved or validated (setup portal, serial
dev `k`), the dev token is cleared, or the user signs out; an ordinary 200 does
not clear it. A 403 is not gated: it is reported for the endpoint that returned
it and the rest of the batch can continue, so a token without the
study-materials scope can still review.

An outbox POST that gets 401/403 stays `Pending` and is also kept as a deferred
session auth issue. A later successful `/user` or `/summary` does not erase it;
the sync end reapplies it, so Home/Boot show the generic `Auth: token rejected` /
`Auth: missing permissions`. The `Reviews: token rejected (401)`, `Reviews:
missing permissions (403)`, `Lessons: ...` and `Lesson: ...` strings are
transient loading/sync/debug lines, not the Home status. The deferred issue
clears when the outbox has no unsent items and on token validation/save/clear or
sign-out; draining the outbox also clears an Auth/Forbidden Home state that came
from the outbox, without hiding a current probe/summary failure. Review and
lesson batches carry their HTTP code, so a 401/403 stops the batch and shows the
transient line; an optional materials/statistics/component 401 also stops the
session. A failed `PUT /start` returns Home with the auth state and the transient
`Lesson: ...` line. If reconciliation is blocked by the 401 gate, the outbox item
stays `Ambiguous` instead of being downgraded to `Pending`. A non-critical Level
or SRS 401 keeps the sync going as a row warning but still sets the auth state.

The same client parses `RateLimit-Reset` and
`RateLimit-Remaining` case-insensitively. Any 429 arms one shared monotonic gate
(`millis()` deadline + reset epoch + wait); every GET/POST/PUT while it is active
returns a synthetic 429 with `connected=true`, `rateLimitReset` and `retryAfter`,
without opening TLS, calling the request hooks or touching the body buffer.
`wkRateLimitWaitSeconds()` returns the remaining whole seconds, safely across the
`millis()` wrap. `wkGetJsonRetry` returns immediately on 429 and never retries a
4xx; its streaming counterpart `wkGetBodyRetry` for `/summary` follows the same
policy. They retry a truncated or unparsable 200, `httpCode == 0` or 5xx once,
with a short delay between attempts. Each 200 body is parsed before the next
request, so a short JSON body gets one more attempt; other 4xx and 429 are not
retried by the helpers, and a 429 still arms the shared gate. The wait is the
reset delta clamped to 120 s, or 60 s when the header is absent/past or the
clock is untrusted. The boot screen, Home status line and sync error line show
the countdown; an outbox POST 429 stays `Pending` and the next attempt is
satisfied locally by the gate. The visible countdown is redrawn once per second
while the gate is active and no blocking sync is running. A 429 on the
non-critical Level or SRS step is a row warning but still
sets the rate-limit state for Home/Boot. Study-material and review-statistic
batch loops stop at the first 429. If reconciling an ambiguous outbox item is
blocked by the 429 or 401 gate, the item stays `ambiguous`.

---

## 11. Storage format

### 11.1 Subject (current cache)

`KKCS` stores variable-length subject records: id, type, level and characters;
count-prefixed meanings/readings and synonym/whitelist/blacklist lists;
component/similar/amalgamation ID lists; mnemonic, hint and note strings; parts
of speech and context sentences; and the review-statistics counters. Every
string is `u16` length + UTF-8. There is no fixed-size subject record or shared
string pool.

The fixed `SubjectRecord` + string-pool layout below is a future design and is
not written by the current firmware.

```
SubjectRecord (future, not used)
  id, type, level, flags, lesson_position
  count fields and string-pool offsets
  data_updated_at
```

### 11.2 Assignment (current cache)

The snapshot stores each cached batch assignment inline as a 19-byte record:

```
Assignment (current, 19 B)
  id                u32
  subject_id        u32
  subject_type      u8
  srs_stage         u8
  available_at      u32  0 = null
  data_updated_at   u32
  flags             u8   hidden, pending_review
```

The snapshot is rewritten whenever the cache is saved (sync, batch fetch,
review/lesson change). The larger fixed 32-byte `AssignmentRecord` from the
original design is not used.

### 11.3 Outbox

See §6.1. The whole file is rewritten with `temp + rename`; `.bak` keeps the
previous version. It is not an append-only log.

### 11.4 Cache header

Both `KKCA` (LittleFS) and `KKCS` (SD) start with the same 24-byte header:

```
0   magic         4 B   "KKCA" or "KKCS"
4   version       u16   currently 11
6   reserved      u16   0
8   saved_at      u32   UTC epoch; 0 when the clock was not trusted
12  account_key   u32   FNV-1a of the API token
16  payload_len   u32
20  payload_crc   u32   CRC32 of the payload
24  payload...
```

There is no ETag/last-sync meta in the current files; the snapshot payload holds
the summary, user, level, SRS counts and batch assignments, and `KKCS` holds the
subjects.

### 11.5 Session statistics

The current build keeps session counters in RAM only (`SessionStats`): reviews,
correct/incorrect by type, accuracy and elapsed time for the summary screen.
They are discarded when the session ends and are not written to LittleFS.
Lifetime counters, initial/final-stage history and rebuild logic are future
work.

---

## 12. Fixtures and tests

Minimal fixtures, hand-written, with attribution:

- `user.json`: level, subscription, preferences.
- `summary.json`: review/lesson buckets.
- `assignments.json`: radical, kanji, vocab, kana; stage 0/1/9; hidden.
- `subjects.json`: radical with `characters: null`, kanji with on/kun/nanori,
  vocab with context/audio, kana vocab.
- `study_materials.json`: synonyms.
- `review_statistics.json`: correct/incorrect counts, streaks and percentage.
- `level_progressions.json`: current and previous level timestamps.
- `vocabulary_details.json`: parts of speech and context sentences.
- `related_subjects.json`: visually similar and amalgamation IDs.
- `review_ok.json`: `resources_updated.assignment`.
- `errors.json`: 401, 403, 422, 429.

Tests (current native coverage):

- Fixture parsing into the models; cache snapshot/subject round trips, CRC
  rejection and the level kanji percentage rounding.
- JSON values, escapes and bad input; summary buckets, IDs and a valid empty
  summary.
- Outbox model counts, HTTP code default, attempt cap and stuck add/remove.
- Rate limit: respects `Reset` (42 s, fallback, untrusted clock, 120 s cap).

Not covered natively (host/device checks or planned): HTTP/TLS requests, outbox
append/compaction/retries/reconciliation, outbox 429/timeout/422/cut-mid-write/
`.bak`, `next_url` pagination, filesystem atomic writes/version migration, and
`created_at`/untrusted-clock edge cases.

---

## 13. Open decisions

| # | Decision | Recommendation |
|---|---|---|
| 1 | Store all subjects or only the ones in the queue? | Only the ones in the queue; optional “download all” is not implemented |
| 2 | Mnemonics in the base cache? | Yes, for offline lessons; truncate if needed |
| 3 | `updated_after` or full sync? | Planned delta with `updated_after`; full on cold |
| 4 | Retry an ambiguous POST? | Reconcile first; the current heuristic retries when delivery cannot be proven |
| 5 | Send `created_at` offline? | Only if the clock is reliable; otherwise omit |
| 6 | Offline lessons? | Lessons are v1; `start` still requires network |
| 7 | User synonyms? | Yes, `study_materials`; shown in the detail view |
| 8 | Store audio/context? | Context sentences are stored (up to 4); pronunciation audio is phase 2 |

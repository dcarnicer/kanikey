# KaniKey — API, cache and synchronization

> Design, 2026-10-25. This document is the contract between the firmware and the
> WaniKani API, and between the firmware and local storage.

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
| GET | `/user` | User, level, subscription, preferences | Boot / 1 day |
| GET | `/summary` | Reviews/lessons per hour, `next_reviews_at` | 30–60 min |
| GET | `/assignments` | Stages, `available_at`, assignment IDs | Every sync |
| GET | `/subjects` | Meanings, readings, components, mnemonics | When IDs are missing |
| GET | `/study_materials` | User synonyms | Every sync |
| POST | `/reviews` | Send a completed review | Per item |
| PUT | `/assignments/{id}/start` | Start a lesson | Per lesson |

Optional for phase 2:

| Method | Endpoint | What for |
|---|---|---|
| GET | `/review_statistics` | Accuracy per subject |
| GET | `/reviews` | History, streak, heatmap |
| GET | `/level_progressions` | Level progress |

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
- It is cached for 30–60 min; after a session it is adjusted locally or refreshed.

### 3.3 `/assignments`

Filters that are used:

| Filter | Use |
|---|---|
| `immediately_available_for_review=true` | Review queue |
| `immediately_available_for_lessons=true` | Lessons |
| `updated_after=<ISO>` | Changes since the last sync |
| `hidden=false` | Exclude hidden |
| `subject_ids=...` | Refresh specific assignments |
| `levels=...` | Limit to allowed levels |

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

Filters: `ids=...`, `types=...`, `levels=...`, `updated_after=...`.

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
data.context_sentences[] (vocabulary, phase 2),
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

### 3.5 `/study_materials`

Fields: `subject_id`, `meaning_note`, `reading_note`, `meaning_synonyms[]`,
`hidden`. The synonyms are used as correct answers. They are requested only for
subjects in the cache, by `subject_ids` or `updated_after`.

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
  `resources_updated.review_statistic`; they are applied to the local state.
- There is no idempotency key. See §6.

### 3.7 `PUT /assignments/{id}/start`

Body:

```json
{ "assignment": { "started_at": "2026-10-25T08:42:13Z" } }
```

- `started_at` is optional; it defaults to now.
- It requires `srs_stage == 0`, `started_at == null`, `unlocked_at != null` and an
  allowed level.
- It returns the assignment with `srs_stage = 1` and `available_at`.

---

## 4. Token and permissions

- Personal WaniKani token; never username/password.
- Required permissions (the WaniKani portal groups them by resource):
  - Read **assignments**.
  - Read **subjects**.
  - Read **study materials**.
  - Read **user** and **summary**.
  - Create **reviews**.
  - (Phase 2: read **review statistics** and **reviews**.)
- It is stored in NVS; it is shown masked; it is deleted on logout.
- It is validated with `GET /user` before storing it.
- If the token lacks permissions, `403`; what is missing is shown and updating it
  is offered.
- If the token is revoked, `401`; returning to setup is offered.

---

## 5. Synchronization model

### 5.1 States

| State | What it means | What the UI shows |
|---|---|---|
| `cold` | No cache or no token | Setup / first sync |
| `warm` | Cache and token, network available | Normal home |
| `syncing` | Requests in progress | Progress |
| `offline` | No network, with cache | `Offline · N cached · M to sync` |
| `stale` | Old cache (more than 24 h) | `Last sync 1d ago` notice |
| `auth_error` | 401/403 | `Token rejected` / `Missing permissions` |
| `server_error` | Persistent 5xx | `WaniKani is not answering` |

### 5.2 Full sync (cold)

```
1. Ensure time (SNTP or portal)
2. GET /user                     → user, level, max_level_granted
3. GET /summary                  → counters
4. GET /assignments?hidden=false&levels=1..max_level_granted
                                 → all assignments (paginated)
5. Collect subject_ids of the relevant assignments
6. GET /subjects?ids=...         → in batches of 100
7. GET /study_materials          → synonyms
8. Save meta and ETags
```

- The initial cache may be large (up to ~10,000 assignments). It is stored in
  LittleFS; subjects only for the relevant IDs (reviews/lessons), not all of them.
- If the user wants a full offline cache, a “Download all subjects” setting
  walks all assignments and stores the subjects on the SD card.
- `GET /assignments?hidden=false` may be several pages; it is written as pages
  arrive.

### 5.3 Warm sync

```
1. If there is an outbox: flush (see §6)
2. GET /user (ETag)
3. GET /summary (TTL 30 min)
4. GET /assignments?updated_after=<last_sync>
5. GET /assignments?immediately_available_for_review=true
6. GET /assignments?immediately_available_for_lessons=true
7. For new IDs: GET /subjects?ids=...
8. GET /study_materials?updated_after=<last_sync>
9. Save meta
```

- Order matters: the outbox first, so that the assignments returned by the
  server already reflect the reviews that were sent.
- If the flush fails due to network, it switches to offline and no assignments
  are requested.
- If the flush fails with 422, the item is reconciled and it continues.
- `updated_after` may return changes to assignments that are not in the queue
  (because they were reviewed on another device); they are applied.

### 5.4 Review session

```
session_start:
  - if there is network and last_sync > 15 min: sync
  - due = assignments with stage 1..8, available_at <= now, hidden=false
  - filter out those already in the outbox
  - sort according to reviews_presentation_order (shuffled by default; if it is
    lower_levels_first, respect it) and freeze the list
  - for each subject_id without a subject: subjects sync (if there is network)
  - if a subject is missing: do not offer the item; show a notice

item_complete:
  - write outbox (assignment_id, counts, created_at, subject_id)
  - if there is network: try an immediate POST
      - ok: apply assignment, delete from outbox
      - 429/5xx/timeout: leave pending
  - if there is no network: leave pending
  - advance
```

- The session list does not change if the server adds reviews in the meantime.
- Completed items are removed from the local queue even if the POST remains
  pending.
- At the end, the summary shows `N waiting to sync` if an outbox remains.

### 5.5 After syncing

- `summary` is refreshed; local counters are recalculated.
- If the server returns new assignments, the home screen shows `N new reviews`.
- If an outbox item was rejected (422), it is deleted, the assignment is
  refreshed and `1 review could not be saved` is shown.
- If the token expires, the outbox is kept and reauthentication is requested.

---

## 6. Outbox and ambiguous sends

### 6.1 Record

```
seq              u32   local monotonic
assignment_id    u32
subject_id       u32
incorrect_meaning u16
incorrect_reading u16
created_at       u32   UTC epoch; 0 = omit
state            u8    pending | sending | ambiguous | rejected
attempts         u8
crc32            u32
```

- It is written to LittleFS with `temp + rename` and a `.bak` copy.
- `sending` is marked before the POST; if there is a power cut, on startup it is
  treated as `ambiguous`, not blindly as `pending`.
- `rejected` is kept until the user sees it; then it is deleted.

### 6.2 Flush

1. Sort by `seq`.
2. For each `pending`:
   - POST.
   - 200: apply `resources_updated.assignment`; delete.
   - 401/403: stop and request auth.
   - 422/409: mark `rejected`; reconcile.
   - 429: wait for `RateLimit-Reset`; retry.
   - 5xx/timeout: mark `ambiguous`; stop the flush (do not continue with the
     following ones to avoid creating a mess).
3. `ambiguous` ones are resolved before retrying:
   - `GET /assignments/{assignment_id}`.
   - If `data_updated_at` is later than our `created_at` and the SRS stage
     changed, assume the review arrived; delete.
   - If the assignment still has the same stage and `available_at` earlier than
     `created_at`, the POST did not arrive; set it back to `pending`.
   - If it cannot be decided, show `Needs attention` and do not duplicate.

### 6.3 Why not retry blindly

The API does not accept an idempotency key. A timeout after the server has
processed the review produces a duplicate if it is resent. Duplicating a review
alters the SRS stage and the user's statistics. We prefer to leave the item as
`ambiguous` and reconcile rather than risk it.

### 6.4 Limitations

- Reconciliation is heuristic: another device could have reviewed the same item
  in the same window.
- If the user reviews in KaniKey and on the web at the same time, there may be
  conflicts. The UI warns and prioritizes not duplicating.
- The outbox is not synchronized between devices.

---

## 7. Cache

| Data | Where | Lifetime | Invalidation |
|---|---|---|---|
| Token, Wi-Fi, settings | NVS | Until logout | User action |
| `user` | LittleFS | 1 day | Sync / 401 |
| `summary` | LittleFS | 30–60 min | Sync / after session |
| Assignments | LittleFS | Until sync | `updated_after`, review response |
| Subjects | microSD | Immutable by ID | `data_updated_at` if refreshed |
| Study materials | LittleFS | Until sync | `updated_after` |
| Visual pack | microSD | Until regenerated | Builder version / new characters |
| Local stats | LittleFS | Persistent | Manual reset |

### 7.1 Sizes

- Assignments: ~10,000 × 32 B ≈ 320 KB; fits in LittleFS.
- Outbox: 100 × 32 B ≈ 3.2 KB.
- Subjects: ~300 B on average + mnemonics; ~20 MB if all are cached; that is why
  they live on the SD card and are requested by ID.
- Visual pack: 7–9 MB; SD card.

### 7.2 Atomic writes

- Write `.tmp`, `fsync`/`close`, rename over the destination.
- Keep a `.bak` of assignments and outbox.
- On startup, if the main file fails CRC, use `.bak` and warn.
- Never delete the outbox before the POST is confirmed.

**Implementation (2026-10-28).** The Home/user/batch snapshot is
`/state/cache.bin` (`KKCA`, version 1); the batch subjects are
`/kanikey/cache/subjects.bin` (`KKCS`, version 1) on SD. Both carry the save
time, an FNV-1a fingerprint of the API token, the payload length and CRC32.
Loading a snapshot written for another token discards both files. The cached
batch is started offline and completed items are removed from the snapshot;
the outbox is pruned from the cache on boot. `data_updated_at` refresh and
manual-sync invalidation are still open.

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
| 304 | Not modified | Use cache |
| 401 | Invalid token | Stop, request token |
| 403 | No permissions / subscription | Stop, explain |
| 404 | Resource does not exist | Discard, refresh |
| 409 | Conflict | Reconcile |
| 422 | Validation | Reconcile, do not retry |
| 429 | Rate limit | Wait for `RateLimit-Reset` |
| 5xx | Server | Retry with backoff |
| timeout / DNS / TLS | Network | 2 retries, then offline |

- Backoff: 1 s, 2 s, 4 s, 8 s, with jitter; maximum 60 s.
- 429: use `RateLimit-Reset` (epoch) if present; otherwise 60 s.
- Budget: a normal sync < 30 s; a cold one < 2 min.
- No requests are made in parallel. One at a time.
- The radio is turned off when done.

---

## 11. Storage format

### 11.1 Subject

```
SubjectRecord (fixed)
  id                u32
  type              u8
  level             u8
  flags             u16   has_characters, hidden, has_mnemonic
  lesson_position   u16
  meaning_count     u8
  reading_count     u8
  component_count   u16
  similar_count     u16
  chars_off         u32   offset into the pool
  meanings_off      u32
  readings_off      u32
  auxiliary_off     u32
  components_off    u32
  similar_off       u32
  meaning_mnemonic_off u32
  reading_mnemonic_off u32
  data_updated_at   u32
```

String pool: length `u16` + UTF-8, aligned to 2 bytes. Lists are
`count` + `count` 8-byte references (`offset`, flags/type).

### 11.2 Assignment

```
AssignmentRecord (fixed, 32 B)
  assignment_id     u32
  subject_id        u32
  srs_stage         u8
  subject_type      u8
  flags             u8   hidden, started, passed, burned, pending_review
  available_at      u32  0 = null
  started_at        u32
  unlocked_at       u32
  data_updated_at   u32
```

They are stored sorted by `subject_id`; the file is rewritten on every sync
(320 KB is fast on LittleFS).

### 11.3 Outbox

See §6.1. Append-only file; it is compacted when emptied.

### 11.4 Meta

```
magic "KKM1", version u16
user_id, username, level, max_level_granted
last_user_sync, last_summary_sync, last_assignment_sync, last_outbox_flush
etag_user, etag_summary (hash or short string)
clock_trusted, last_known_utc
schema_version
```

### 11.5 Local statistics

Session and lifetime counters: reviews, correct, incorrect by type, time,
initial/final stage. They can be partially rebuilt; they are not the source of
truth.

---

## 12. Fixtures and tests

Minimal fixtures, hand-written, with attribution:

- `user.json`: level, subscription, preferences.
- `summary.json`: review/lesson buckets.
- `assignments.json`: radical, kanji, vocab, kana; stage 0/1/9; hidden.
- `subjects.json`: radical with `characters: null`, kanji with on/kun/nanori,
  vocab with context/audio, kana vocab.
- `study_materials.json`: synonyms.
- `review_ok.json`: `resources_updated.assignment`.
- `errors.json`: 401, 403, 422, 429.

Tests:

- Parsing each fixture into the model and back to the binary format.
- Chained pages with `next_url`.
- `updated_after` and ETag/304.
- Outbox: ok, 429, timeout, 422, cut mid-write, `.bak`.
- Reconciliation: assignment updated, not updated, ambiguous.
- Clock: valid `created_at`, earlier than `available_at`, unreliable.
- Rate limit: respects `Reset`.

---

## 13. Open decisions

| # | Decision | Recommendation |
|---|---|---|
| 1 | Store all subjects or only the ones in the queue? | Only the ones in the queue; optional “download all” |
| 2 | Mnemonics in the base cache? | Yes, for offline lessons; truncate if needed |
| 3 | `updated_after` or full sync? | Delta with `updated_after`; full on cold |
| 4 | Retry an ambiguous POST? | No; reconcile first |
| 5 | Send `created_at` offline? | Only if the clock is reliable; otherwise omit |
| 6 | Offline lessons? | Phase 2; v1 requires network for `start` |
| 7 | User synonyms? | Yes, `study_materials`; shown in the detail view |
| 8 | Store audio/context? | Phase 2; not in v1 |

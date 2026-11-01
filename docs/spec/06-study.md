# Critical items and extra study

STUDY covers decision D4: account-wide critical (leech) detection, critical-first review order and local extra-study modes. Practice sessions are local: they never call `POST /reviews` or `PUT /start`, never write the outbox and never change WaniKani SRS state. Today `/review_statistics` is fetched only for the review batch (`firmware/src/platform/WkReviewStatistics.cpp:24-70`) and merged into cached subjects; there is no account-wide store or practice path yet. All tasks are Wave 3 and assume ARCH-01's controller split, ARCH-02's settings registry and ARCH-03's SD catalog/journal; line numbers below are from the current tree.

| ID | Title | Priority | Size | Depends on | Wave | Status |
|---|---|---|---|---|---|---|
| STUDY-01 | Detect critical items | P2 | M | ARCH-02, ARCH-03 | 3 | todo |
| STUDY-02 | Prioritize critical items | P2 | S | STUDY-01 | 3 | todo |
| STUDY-03 | Extra study menu | P2 | L | ARCH-01, ARCH-02, ARCH-03, STUDY-01 | 3 | todo |
| STUDY-04 | Subjects for practice offline | P2 | M | ARCH-03, STUDY-03 | 3 | todo |
| STUDY-05 | Critical items list | P3 | S | STUDY-01, STUDY-03 | 3 | todo |

### STUDY-01 — Detect critical items

- **Priority:** P2
- **Size:** M
- **Depends on:** ARCH-02, ARCH-03
- **Touches:** `firmware/src/wk/CriticalItems.{h,cpp}` (new: `CriticalItem`, `isCritical`, `selectCriticalItems`); `firmware/src/wk/ReviewStatistics.{h,cpp}` (`ReviewStatistic` at `ReviewStatistics.h:13-25`, `parseReviewStatistics` at `ReviewStatistics.cpp:20-45`; add `id`, `totalAnswers()` and `parseReviewStatisticsStream`); `firmware/src/platform/WkReviewStatistics.{h,cpp}` (new `fetchCriticalStatistics`, reuse `idList` at `:13-20` and the request loop at `:29-56`); the ARCH-03 catalog store (names/version from `00-architecture.md`); `firmware/src/app/App.cpp` (`SyncRowIndex` `:166-173`, `resetSyncScreen` `:2610-2623`, `startSync` `:2691-2851`, new `runCriticalItems` beside `runSrsCounts` `:761-770`; post-ARCH-01: the sync controller); `firmware/test/test_wk/test_wk.cpp`
- **Settings:** S1 "Critical below" (default 75%, key `st.critpct`) and S2 "Critical minimum reviews" (default 5, key `st.critmin`) (`03-settings.md`)

**Problem / goal.** Critical items are not known account-wide. `fetchReviewStatistics` only queries `subject_ids` for the current review batch (`firmware/src/platform/WkReviewStatistics.cpp:31`) and `applyReviewStatistics` only sets `percentageCorrect` on those subjects (`firmware/src/wk/ReviewStatistics.cpp:47-70`). Nothing persists a list across sessions, so the app cannot prioritize leeches (STUDY-02) or offer the critical-items mode (STUDY-03/05). This replaces ROADMAP EXT-03's detection half.

**Behaviour.** A full sync adds a non-critical `Critical` row after `SRS`. Success shows `N critical items`; 401 shows the auth issue, while 403, 429, network or parse failure shows `unavailable` and keeps the previous cached list without failing the sync. The list is computed from `GET /v2/review_statistics?percentages_less_than=<S1>&hidden=false`, paginated, then joined to catalog assignments so hidden assignments and burned (`srs_stage == 9`) assignments are dropped; a statistic with fewer than S2 total answers is also dropped. The list survives reboot and is available offline; an empty list is valid and shows `0 critical items`. An older catalog is discarded at boot after the version bump and rebuilt by the next full sync.

**Design.**
- Use the `percentages_less_than` and `hidden` filters of `/v2/review_statistics` (verify both names and whether the bound is strict against the WaniKani API docs before coding); the local test is `percentage_correct < S1` either way.
- Add `uint32_t id` to `ReviewStatistic` for the page cursor and `totalAnswers()` = `meaningCorrect + meaningIncorrect + readingCorrect + readingIncorrect`; parse `id` in both DOM and streaming paths.
- Paginate with `page_after_id=<last complete resource id>` while `seen < total_count`; do not pass `pages.next_url` directly because the client expects a `/v2/` path (`firmware/src/platform/WkClient.h:32`).
- A 500-record page can exceed the 32 KB `gBodyBuffer` (`firmware/src/platform/WkClient.cpp:22`). Use a `JsonStreamParser` sink modeled on `AssignmentSink` (`firmware/src/wk/Assignment.cpp:12-138`), keep complete records and continue after the last complete id if truncated. If no complete record fits, warn and keep the old list.
- `selectCriticalItems(stats, assignments, maxPercent, minAnswers)` in `wk/CriticalItems.cpp` is pure: skip `stat.hidden`, `percentage >= maxPercent`, `totalAnswers < minAnswers`, and missing/`hidden`/stage-9 assignments. Sort by percentage ascending, then subject id.
- Persist `CriticalItem { uint32_t subjectId; uint8_t percentageCorrect; }` in the catalog; bump the catalog version and discard older files. Do not recompute the percentage locally.

**Tests.** Native: `test_critical_strictly_below_threshold`, `test_critical_minimum_answers`, `test_critical_excludes_hidden_and_burned`, `test_critical_missing_assignment_skipped`, `test_critical_sorted_worst_first`, `test_review_statistics_stream_keeps_complete_records`, `test_review_statistics_parses_id_and_total_answers`. Hardware: full sync on a real account with review-statistics permission; reboot with Wi-Fi off and inspect the catalog; a token without the permission keeps the previous list.

**Docs.** `docs/api-sync.md` §3.8 (filter, pagination, catalog storage); `docs/design.md` storage table; `docs/decisions.md` dated entry for the critical formula; `ROADMAP.md` EXT-03 row.

**Acceptance.**
- [ ] A full sync fetches, paginates and stores critical subject ids and percentages in the catalog.
- [ ] Hidden and burned assignments are excluded; `percentage < S1` and `total answers >= S2` have native tests at the defaults (75, 5).
- [ ] Missing permission, network error or 429 keeps the previous list and does not fail the sync.
- [ ] The list survives reboot for STUDY-05.
- [ ] `pio test -e native` and both device builds pass.

### STUDY-02 — Prioritize critical items

- **Priority:** P2
- **Size:** S
- **Depends on:** STUDY-01
- **Touches:** `firmware/src/wk/ReviewQueue.{h,cpp}` (`build` `ReviewQueue.cpp:23-58`, `due` `:16-19`; add `setCriticalSubjectIds` and a stable partition before the `resize` at `:54`); `firmware/src/app/App.cpp` (`startReview` `:1438-1520`, set ids before `gQueue.build` at `:1487-1490`; post-ARCH-01: the review controller); `firmware/test/test_wk/test_wk.cpp`
- **Settings:** R11 "Prioritize critical" — Off (default) / On, key `rv.critical` (`03-settings.md`); with Off the queue is built exactly as today

**Problem / goal.** `ReviewQueue::build` shuffles, optionally sorts by level and then truncates to the batch limit (`firmware/src/wk/ReviewQueue.cpp:46-54`), so a due critical item can fall outside the batch. R11 asks for critical items first without changing the account's normal review order.

**Behaviour.** When a review session starts, due critical subjects come before non-critical ones. Inside each group the normal order applies: shuffled, or lower levels first when the account requests it. If more critical items are due than the batch limit, only the first critical items in normal order start; the rest wait for the next batch. Without a catalog or critical list the session is identical to today, and offline prioritization still works.

**Design.**
- Add `ReviewQueue::setCriticalSubjectIds(std::vector<uint32_t>)`, called before `build`; the queue keeps the ids only for the build.
- After the shuffle/lower-levels step (`firmware/src/wk/ReviewQueue.cpp:46-53`), call `std::stable_partition` with `isCritical(subjectId)`, then apply the limit (`:54`). Stable partition preserves the normal order inside both groups.
- `startReview` reads the ids from the catalog before `gQueue.build` (`firmware/src/app/App.cpp:1487-1490`); no network request is added to the review path.
- Use a linear `std::find` over the id list: a batch is at most 50 items and the critical list is small.

**Tests.** Native: `test_queue_critical_items_first`, `test_queue_critical_limit_keeps_only_critical`, `test_queue_critical_preserves_normal_order`, `test_queue_without_critical_matches_previous_order`. Hardware: a real account with at least one critical item due shows it first; the existing shuffle and lower-levels-first modes still behave.

**Docs.** `docs/ux.md` §5.4 order note; `ROADMAP.md` EXT-03 row.

**Acceptance.**
- [ ] Due critical items precede non-critical items in the built queue.
- [ ] The normal order is unchanged inside the critical and non-critical groups.
- [ ] The batch limit never cuts a critical item in favour of a non-critical one.
- [ ] No network request is added to the review start path.
- [ ] Native tests and both device builds pass.

### STUDY-03 — Extra study menu

- **Priority:** P2
- **Size:** L
- **Depends on:** ARCH-01, ARCH-02, ARCH-03, STUDY-01
- **Touches:** `firmware/src/wk/Practice.{h,cpp}` (new: `PracticeMode`, `PracticePlan`, selection helpers); `firmware/src/app/StudyScreens.{h,cpp}` (new: `StudyMenuView`, `drawStudyMenuScreen`); `firmware/src/app/App.cpp` (practice state near `:104-117`; Home key in `onKey` `:2395-2540`, next to `L` at `:2508-2513`; `buildHomeView` `:3051-3107`; `draw` `:4166-4248`; reuse `checkCurrent` `:1348-1374`, `advanceFeedback` `:1376-1397`, `endReview` `:1399-1424`, `showReviewSummary` `:967-990`; post-ARCH-01: the study controller); `firmware/src/app/HomeScreen.cpp:118`; `firmware/src/app/ReviewScreens.{h,cpp}` (`ReviewSummaryView` `ReviewScreens.h:49-63`, `drawReviewSummaryScreen` `ReviewScreens.cpp:407-498`); `firmware/src/wk/ReviewQueue.{h,cpp}` (new `buildPractice`; reuse the shuffle/order block `:46-57`); ARCH-03 journal/catalog; `firmware/test/test_wk/test_wk.cpp`
- **Settings:** S3 "Recent lessons" — Apprentice (default) / 24 h / 3 d / 7 d; S4 "Recent mistakes" — 24 h (default) / 3 d / 7 d; S5 "Practice size" — 10 (default) / 20 (`03-settings.md`); S1/S2 from STUDY-01

**Problem / goal.** There is no extra study. Home handles only `Enter`/`L`/`O`/`S` (`firmware/src/app/App.cpp:2495-2519`), and the only session path is the due-review batch. The existing session cannot be reused as-is: `checkCurrent` decrements `gDueReviews` and calls `markCacheCompleted` (`:1369-1370`), `advanceFeedback` calls `queueReview` (`:1383-1386`), `endReview` queues a pending item (`:1400-1403`), and `ReviewQueue::build` rejects anything not due (`firmware/src/wk/ReviewQueue.cpp:16-19`). This replaces EXT-04.

**Behaviour.** `P` on Home opens `EXTRA STUDY` with four rows and counts: `Recent mistakes`, `Recent lessons`, `Critical items`, `Current level`. Up/Down moves, Enter starts, Esc returns Home. Home gains `P: practice` in the hint; if it does not fit the 224 px bar, keep it in the `?` help only (CFG-04). Each mode builds S5 items: Recent mistakes from journal mistakes inside the S4 window; Recent lessons from Apprentice 1-4 (S3 = Apprentice) or assignments started inside the S3 window; Critical items from STUDY-01; Current level from started kanji of the current level below Guru. A loading screen says `Preparing practice...` and the synchronous subject fetch cannot be cancelled, as with the review batch. Empty modes show `No recent mistakes` / `No recent lessons` / `No critical items` / `Current level complete` and stay in the menu. The session reuses the review UI and `AnswerChecker`; it has no outbox, no `POST /reviews`, no `PUT /start`, no due-count change, no review-cache/statistics mutation and no stage change. Every practice answer is journaled with `practice=true`, and Recent mistakes ignores practice records. The summary title is `PRACTICE` with the line `Nothing was sent to WaniKani`, plus `N items skipped` when STUDY-04 skipped any.

**Design.**
- State: `gPracticeActive`, `gPracticeMode`, `gPracticeSkipped` and a session `gPractice` flag; `gReviewActive` still routes input and drawing. `endReview` clears practice state and does not call `evictCachedBatch`.
- Add `ReviewQueue::buildPractice` accepting prepared items with `assignmentId = 0` and no due/available check; apply the same shuffle, meaning-first and lower-levels order as `build`.
- `checkCurrent` in practice calls `gQueue.answer` but skips `gDueReviews--`, `markCacheCompleted` and `gNeedsSubmit`; it journals the answer with `practice=true`. `advanceFeedback` skips `queueReview` and stage changes and enters the summary when the queue empties.
- Put selection in `wk/Practice.cpp`: Recent mistakes = journal incorrect records since `now - S4` (seconds), `practice == false`, deduped by subject, newest first (verify the journal query API in ARCH-03). Recent lessons = catalog assignments `!hidden`, `srsStage 1..4` when S3 = Apprentice, otherwise `startedAt >= now - S3` (seconds), `srsStage != 9`, newest first. Critical = STUDY-01 order. Current level = assignments at `gUser.level`, `type == Kanji`, `srsStage 1..4`, `!hidden`, not burned, `lessonPosition` order.
- `Assignment` has no `startedAt` today (`firmware/src/wk/Types.h:78-88`; parser at `wk/Assignment.cpp:65-78` ignores `started_at`). Add it in the ARCH-03 catalog or here with the cache version bump; do not guess it from `availableAt`.
- `ReviewSummaryView` gains `bool practice`; `drawReviewSummaryScreen` draws `PRACTICE` at `ReviewScreens.cpp:415` and replaces the outbox line (`:460-470`) with `Nothing was sent to WaniKani` in `kFaint`; stage/outbox rows are hidden.
- S5 sets the session size (default 10); the menu shows the real number of selected items after skipping.

**Tests.** Native: `test_practice_recent_lessons_stage_or_window`, `test_practice_recent_mistakes_window_and_dedupe`, `test_practice_recent_mistakes_ignores_practice`, `test_practice_current_level_kanji_below_guru`, `test_practice_limit_and_empty`, `test_practice_queue_ignores_due_and_available`. Hardware: open every mode, finish one session, confirm `PRACTICE`, the no-write line, no outbox item and no `POST /reviews` in the serial log; check pause/end returns Home.

**Docs.** `docs/ux.md` §3.3 key table, §5.3 Home hint and a new Extra study section; `docs/decisions.md` dated entry for D4 practice semantics; `ROADMAP.md` EXT-04 row.

**Acceptance.**
- [ ] `P` opens the Extra study menu and `?` help lists it.
- [ ] All four modes build a session from catalog/journal data and show an empty state when there is nothing to practise.
- [ ] Practice never queues an outbox item, never POSTs a review, never PUTs a start and never changes due counts, SRS or review statistics.
- [ ] Practice answers are journaled with `practice=true` and do not appear in Recent mistakes.
- [ ] The summary says `PRACTICE` and `Nothing was sent to WaniKani`.
- [ ] Native tests and both device builds pass.

### STUDY-04 — Subjects for practice offline

- **Priority:** P2
- **Size:** M
- **Depends on:** ARCH-03, STUDY-03
- **Touches:** `firmware/src/platform/WkLessons.cpp` (`fetchSubjectsByIds` `:101-125`, reused without changing its all-or-nothing return); `firmware/src/wk/Practice.{h,cpp}` (new `PracticePlan` / `splitCachedSubjects`); `firmware/src/app/App.cpp` (practice start; `drawLoading` `:2598-2608`; `gFreeCanvasForRequest` `:550-556`; post-ARCH-01: the study controller); the ARCH-03 catalog; `firmware/test/test_wk/test_wk.cpp`
- **Settings:** none

**Problem / goal.** Practice needs subjects that are not in the review batch. `gBatch.subjects` is built only from the due batch (`firmware/src/app/App.cpp:772-834`) and is evicted on return Home (`:1399-1423`), so practice cannot use it. Offline, a missing subject must not abort the session, and the user must see how many items were dropped.

**Behaviour.** Before a session, subject ids from the selected mode are looked up in the catalog. Missing ids are fetched only when online, five per `GET /v2/subjects?ids=` request through `fetchSubjectsByIds`. Offline, missing subjects are skipped; the menu row and summary show `N not cached` (or `N skipped`). If none resolve, the mode shows `Nothing cached for this mode` and does not start. Fetched subjects are added to the catalog, the only write; the review batch, subject cache and statistics are untouched. Partial failures keep what was fetched and stop the remaining requests; 401/429 set the normal issue state and skip the rest.

**Design.**
- `PracticePlan` is pure: ordered `ids`, resolved `subjects` and `missingIds`, with a `limit` applied after resolution so skipped subjects do not shorten a session unnecessarily.
- Call `fetchSubjectsByIds(token, group, 5)` per group and keep each successful group. The function returns `{}` if any page fails (`firmware/src/platform/WkLessons.cpp:110-118`); do not change that contract for review components.
- Free the canvas around each request (`gFreeCanvasForRequest`, `App.cpp:550-556`) and show `Preparing practice...`; the synchronous fetch is not cancellable, matching the review batch.
- Store fetched subjects in the catalog with the account key; do not hold both the catalog copy and the fetched copy. Set `gPracticeSkipped` to the final missing count and show it on the menu row and summary.
- If `wkAuthBlocked()` or `wkRateLimitWaitSeconds()` is set, skip remaining fetches without failing the session.

**Tests.** Native: `test_practice_plan_splits_cached_and_missing`, `test_practice_plan_preserves_mode_order`, `test_practice_plan_skips_missing_when_offline`. Hardware: with Wi-Fi off, start each mode and check the count; with Wi-Fi on and a subject missing from the catalog, confirm one request per group of five and a session that continues after skips.

**Docs.** `docs/api-sync.md` §3.2 subjects (practice fetch); `docs/design.md` cache/storage table; `docs/ux.md` Extra study offline note.

**Acceptance.**
- [ ] Cached subjects start without a network request.
- [ ] Online missing subjects are fetched five per request and added to the catalog.
- [ ] Offline missing subjects are skipped and the count is visible.
- [ ] No review batch, subject-cache or review-statistics file is modified by practice.
- [ ] Native tests and both device builds pass.

### STUDY-05 — Critical items list

- **Priority:** P3
- **Size:** S
- **Depends on:** STUDY-01, STUDY-03
- **Touches:** `firmware/src/app/StudyScreens.{h,cpp}` (new `CriticalListView`, `drawCriticalListScreen`; row pattern from `LessonScreens.cpp:20-100`); `firmware/src/app/App.cpp` (`onKey` `:2395-2540`, `draw` `:4166-4248`, `showReviewDetail` `:927-948`, `reviewDetailKey` `:950-965`, `subjectMainMeaning` `:1621-1626`; post-ARCH-01: the study controller); `firmware/src/app/ReviewScreens.{h,cpp}` (`drawReviewDetailScreen` `ReviewScreens.cpp:202`); `firmware/test/test_wk/test_wk.cpp`
- **Settings:** none

**Problem / goal.** STUDY-01 stores critical items but no screen exposes them. The user can only see per-item accuracy after answering in a review, not a leech list, so the stored data is invisible and there is no way to open the detail card for a critical subject outside a session.

**Behaviour.** Extra study → `Critical items` opens `CRITICAL ITEMS` with the total at the top right. Each row shows the subject prompt (Unicode character or radical bitmap), the main meaning and `NN%` right-aligned, worst first. Five rows are visible; Up/Down scrolls, Enter opens the existing detail card, and Esc returns to the list (not Home). Empty shows `No critical items`; a subject missing from the catalog is skipped. Nothing is fetched from the network and nothing is written.

**Design.**
- `CriticalListView { std::vector<CriticalRowView> rows; int selectedIndex; }`; `drawCriticalListScreen` draws 20 px rows, a selected `kSurfaceHi` fill plus a 2 px `kAccent` bar, matching `drawLessonListScreen` (`firmware/src/app/LessonScreens.cpp:20-100`).
- `buildCriticalRows` walks the STUDY-01 catalog list in order, finds the subject in the catalog, skips missing ids, and fills prompt/meaning/percentage. Use `subjectMainMeaning` (`App.cpp:1621-1626`); draw radicals with `drawSubjectPrompt` and the missing state with `drawMissingGlyph` (`firmware/src/ui/Widgets.h:22-27`).
- Generalize `gReviewDetail` with `gDetailSubjectId` and a `DetailOrigin` (`ReviewFeedback`, `CriticalList`). `showReviewDetail` (`App.cpp:927-948`) uses the explicit id; `reviewDetailKey` (`:950-965`) returns to the list when the origin is `CriticalList`.
- Route `gCriticalListActive` before the review/list routers in `draw` and `onKey`. No outbox or cache calls.

**Tests.** Native: `test_critical_rows_skip_missing_subjects`, `test_critical_rows_keep_order_and_percent`, `test_critical_rows_empty`. Hardware: scroll a list longer than five, open a Unicode row and a radical row, press Esc from the detail and land back on the list, and check the empty state.

**Docs.** `docs/ux.md` new critical-items section; `ROADMAP.md` EXT-03 row.

**Acceptance.**
- [ ] Extra study → Critical items shows the stored list, worst first.
- [ ] Each row shows character or radical bitmap, main meaning and percentage.
- [ ] Up/Down scrolls and Enter opens the existing detail card; Esc returns to the list.
- [ ] Missing subjects are skipped and the empty state is shown.
- [ ] Native tests and both device builds pass.

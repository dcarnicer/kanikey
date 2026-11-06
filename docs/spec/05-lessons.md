# Lessons: WaniKani-style batch flow

Decision D2 (2026-11-01) replaces the per-card start with WaniKani's batch flow: browse every card of the batch, quiz the batch, then start the whole batch with one confirmed action. Today `Enter` on a card sends `PUT /assignments/{id}/start` for that single item, `Q` opens a one-item optional quiz, and a started item stays in the list. These six tasks build the browse → quiz → start pipeline, add ordering and a workload guard, and split the card into topic pages.

| ID | Title | Priority | Size | Depends on | Wave | Status |
|---|---|---|---|---|---|---|
| LES-01 | Batch browse | P1 | L | ARCH-01 | 2 | todo |
| LES-02 | Batch quiz | P1 | M | LES-01 | 2 | todo |
| LES-03 | Start the batch | P1 | M | LES-01, LES-02; ARCH-03 (journal, optional) | 2 | todo |
| LES-04 | Ordering | P2 | M | LES-01, FIX-08, ARCH-03 | 2 | partial (LESSON-08 default order) |
| LES-05 | Workload guard | P2 | S | LES-01, ARCH-02 | 2 | todo |
| LES-06 | Card pages by topic | P2 | M | LES-01 | 2 | todo |

### LES-01 — Batch browse

- **Priority:** P1
- **Size:** L
- **Depends on:** ARCH-01
- **Touches:** the lesson controller created by ARCH-01 (today the lesson block in `firmware/src/app/App.cpp`: state `:146-151`, `showLessonList()` `:1628-1646`, `showLesson()` `:1660-1725`, `startLessons()` `:1868-1974`, `lessonListKey()` `:2008-2039`, `lessonKey()` `:2041-2072`, `releaseLessonData()` `:1861-1866`, key dispatch `:2464-2475`, draw dispatch `:4221-4235`, serial mirror `:4636-4760`); `firmware/src/app/LessonScreens.{h,cpp}` (`LessonListView` `.h:23-26`, `drawLessonListScreen()` `.cpp:17-117`, `LessonCardView` `.h:38-47`, `drawLessonCardScreen()` `.cpp:144-232`); new pure `firmware/src/wk/LessonSession.{h,cpp}`; `firmware/test/test_wk/test_wk.cpp`
- **Settings:** reads the existing lesson batch size (`kk::settingsLessonBatchSize()`, `platform/DeviceSettings.cpp:159-166`, presets 3/5/10 at `App.cpp:2180-2182`); registry ID L1 (`03-settings.md`)

**Problem / goal.** `Enter` on a lesson card immediately sends `PUT /assignments/{id}/start` (`lessonKey()` `App.cpp:2043-2046` → `startCurrentLesson()` `App.cpp:1976-2006`). `Enter` means next/continue everywhere else (`ux.md` §3.3), so a user paging a card starts a lesson irreversibly. There is no way to review the whole batch before starting, `Esc` returns to a list that still contains started items (`App.cpp:2054-2064`), and errors other than 401/403 are silent (no `else` branch in `startCurrentLesson()`, see FIX-11).

**Behaviour.** The list stays as it is. `Space` on a row toggles it into a custom batch (at most N; the hint shows `batch full` when N are selected), `Enter` opens the cards with the first N items (N = the existing batch size, already clamped to `gUser.lessonsBatchSize` at `App.cpp:1890-1893`). On a card: `Space`/`Right`/`Enter` go to the next page; `Enter` on the last page goes to the next card; `Left`/`Backspace` go to the previous page, or to the previous card when already on page 1. Header keeps the existing two counters `2/5` and `p1/3` (`LessonScreens.cpp:157-175`). Nothing is sent to WaniKani while browsing; `Esc` returns to the list with nothing lost and nothing started. Bottom hints: list `Space: select  Enter: study`, card `Enter/Space: next  Esc: list`.

**Design.** New pure `kk::LessonBrowse` in `wk/LessonSession.h`: `begin(size_t count, size_t maxSelected)` (selects the first N), `toggle(size_t row, size_t maxSelected)`, `selected()` (indices in list order), `cursor()`, `page()`, `setPageCount(size_t)`, `nextPageOrCard()`, `previous()`, and a `header(char* buf, size_t n)` that formats `"2/5 p1/3"`. The controller owns one `LessonBrowse` and a `gLessonPageCount` filled by `showLesson()` when it wraps the text (today the page count is computed and clamped at `App.cpp:1707-1709`, after the key handler has already incremented `gLessonPage` blindly at `:2065-2067`). `lessonKey()` asks the browse object instead of touching `gLessonPage`; the last-card/last-page case hands over to LES-02 (or LES-03 when the quiz is skipped). Keep the 401/403 auth gate (`App.cpp:1912-1918`). Mirror every new key in the serial dev console (`App.cpp:4636-4760`), which is the hardware-test path.

**Tests.** `test_lesson_browse_selects_first_n`, `test_lesson_browse_toggle_cap`, `test_lesson_browse_page_then_card`, `test_lesson_browse_previous_page_then_card`, `test_lesson_browse_header_format`. Hardware: page a 5-card batch with all three keys and confirm with `tools/device.py monitor` that no `[lesson] start` line appears.

**Docs.** `ux.md` §3.3 (key table: `Enter` no longer starts a lesson), §5.8, §5.9; `api-sync.md` §lessons (nothing is sent while browsing); `decisions.md` new dated entry that supersedes "2026-10-28 — Lesson quiz is optional and local-only"; `ROADMAP.md` row 3 and the execution-order paragraph.

**Acceptance.**
- [ ] `Enter` on a card never calls `PUT /assignments/{id}/start`; the serial log stays free of `[lesson] start` while browsing.
- [ ] `Space` on the list toggles a row and cannot exceed N; the hint says why.
- [ ] `Enter` on the last page advances the card, `Left` on page 1 goes back a card, `Esc` returns to the list with the batch intact.
- [ ] Header shows `2/5 p1/3` and the page count matches the drawn pages.
- [ ] New native tests pass and both firmware environments build.

### LES-02 — Batch quiz

- **Priority:** P1
- **Size:** M
- **Depends on:** LES-01
- **Touches:** lesson controller (quiz entry from `lessonKey()` / `App.cpp:2041-2072`, `startLessonQuiz()` `:1743-1752`, `checkLessonQuiz()` `:1754-1765`, `showLessonQuiz()` `:1767-1802`, `lessonQuizKey()` `:1804-1859`, state `:137-151`); `firmware/src/wk/ReviewQueue.{h,cpp}` (practice build, `ReviewQueue.cpp:18-20` stage filter, `:25-58` `build()`, `:73` `kRequeueGap`); `firmware/src/app/ReviewScreens.{h,cpp}` (`ReviewView` `.h:18-30`, `drawReviewScreen()` `.cpp:30`); `firmware/src/wk/LessonQuiz.{h,cpp}` (single-item practice, kept for `Q`); tests
- **Settings:** adds L3 "Lesson quiz: Required/Skip"

**Problem / goal.** The only quiz is a single-item, optional one opened with `Q` (`startLessonQuiz()` `App.cpp:1743-1752`). `kk::LessonQuiz` holds one `Subject` and completes after meaning + reading (`wk/LessonQuiz.h:13-42`, `LessonQuiz.cpp:22-32`); there is no batch quiz, no shuffle and no requeue, so a user can start lessons without ever proving the batch. The quiz screen is a separate, smaller layout (`LessonScreens.cpp:234-395`) that drifts from the review feedback rules.

**Behaviour.** After the last card, `Enter` starts a quiz over every item of the batch, shuffled, meaning first and reading where `Subject::needsReading()` is true (`wk/Types.h:67-69`). Wrong parts are requeued a few items later until every part is correct; warnings show `CHECK` and retry the same part (FIX-03 rules). Header `LESSON QUIZ`, counter `3/12`. Same input field, romaji IME, `CORRECT`/`CHECK`/`NOT QUITE` feedback and note lines as reviews. `Esc` returns to the last card. With L3 = Skip the flow goes straight to LES-03. `Q` on a card keeps opening the single-item practice, which never gates the batch.

**Design.** Add a practice entry point to `kk::ReviewQueue` — `buildPractice(const std::vector<Subject>&, size_t limit, uint32_t seed)` — because `build()` rejects lesson assignments at `ReviewQueue.cpp:18-20` (`srsStage >= 1`). It fills `SessionItem` with `assignmentId = 0`, uses the same `std::shuffle` and the same `answer()`/`skip()`/`requeueFront()` logic, so requeue and completion need no new code. The controller runs it with the selected subjects and never touches `gOutbox`, `saveCacheSnapshot()` or `POST /reviews`. Draw with `drawReviewScreen()`: add a `const char* header = "REVIEWS"` field to `ReviewView` and render `LESSON QUIZ` plus no SRS text when `srsStage == 0` (reviews never pass 0). Read L3 before the quiz starts; keep the `Q` path on `LessonQuiz` untouched.

**Tests.** `test_lesson_quiz_all_parts_required`, `test_lesson_quiz_requeues_wrong_part`, `test_lesson_quiz_warning_retries_same_part`, `test_lesson_quiz_shuffle_is_seeded`, `test_lesson_quiz_skip_setting_bypasses_quiz`. Hardware: finish a 5-item quiz with two deliberate mistakes and confirm the two items come back and no outbox file grows.

**Docs.** `ux.md` §5.10 (rewrite: batch quiz, L3, `Esc` to the cards); `decisions.md` (same new entry as LES-01; the old "Lesson quiz is optional and local-only" entry gets a superseded line); `ROADMAP.md` row 3.

**Acceptance.**
- [ ] The quiz covers every selected item, not just the last card.
- [ ] A wrong meaning or reading comes back later and the item only completes when both parts are correct.
- [ ] Feedback and input match the review screens; warnings retry the same part.
- [ ] L3 = Skip goes from the last card to the start confirmation.
- [ ] `Esc` returns to the last card and the batch is unchanged.

### LES-03 — Start the batch

- **Priority:** P1
- **Size:** M
- **Depends on:** LES-01, LES-02; ARCH-03 (journal, optional)
- **Touches:** lesson controller (`startCurrentLesson()` `App.cpp:1976-2006` replaced, `releaseLessonData()` `:1861-1866`, `gLessons` `:92`, `applySummary()` `:288-298`, `saveCacheSnapshot()` `:348-376`); `firmware/src/platform/WkLessons.cpp` (`startAssignment()` `:127-141`); `firmware/src/wk/Summary.{h,cpp}` (new bucket removal, `Summary.cpp:41`, `:71-79`); `firmware/src/app/ModalScreens.{h,cpp}` (`LowBatteryView` `.h:18-28`); new pure `firmware/src/wk/LessonSession.{h,cpp}` (start plan); tests
- **Settings:** none (reads L3 from LES-02)

**Problem / goal.** Starting is per card and unconfirmed: `startCurrentLesson()` sends one `PUT` and advances the index (`App.cpp:1976-2006`). Only 2xx is treated as success, 422 is ignored, and every other failure falls through silently (`:1997-2004`, FIX-11). The item is not removed from `gSummary.lessons`, so Home keeps counting it until the next sync, and offline the card just does nothing.

**Behaviour.** After the quiz (or after the last card when L3 = Skip) a confirmation shows `Start 5 lessons? They join your reviews in about 4 hours.` with `Enter: start  Esc: back`. `Enter` starts the items one by one with progress `Starting 2/5`; 2xx and 422 (already started) both count as started; any other status stops the run, lists the failed items with `Enter: retry  Esc: back`, and keeps the rest of the batch. Started items are removed from the summary lesson buckets, the snapshot is saved and the Home count drops immediately. Offline, browsing and the quiz still work; `Start` shows `Needs Wi-Fi — your batch stays here` and the batch stays in RAM as long as the user stays in Lessons. When ARCH-03 is available, each started item writes a journal record with the `lessonStarted` flag (no SRS fields).

**Design.** Replace `startCurrentLesson()` with `startLessonBatch()`: build a `LessonStartPlan` (indices, subject ids, assignment ids) from the browse selection, then loop over it. `kk::startAssignment()` (`WkLessons.cpp:127-141`) already returns the HTTP code and sends `started_at`, so no new endpoint is needed; add a pure `classifyStartResult(int httpCode)` → `Started` (200-299), `AlreadyStarted` (422), `Auth` (401/403), `Failed`. Progress via `drawLoading("Starting lessons...", "Starting 2/5")` (`App.cpp:231`), canvas freed per request with `gFreeCanvasForRequest` (`:1983-1986`). Add `Summary::removeLessonSubjects(const std::vector<uint32_t>&)` that erases ids from each `lessons[].subjectIds`, clamps `SummaryBucket::count` (`wk/Summary.h:11-15`), drops empty buckets, then call `applySummary(effectiveNow())` and `saveCacheSnapshot()`. Confirmation: a new modal view beside `drawLowBatteryScreen` with the same state pattern as `requestLowBatteryConfirm()` / `gLowBatteryPrompt` (`App.cpp:1431-1436`, keyed at `:2407-2425`). Offline check: `kk::DevWifi::connected()` before the first PUT (the serial path already does this at `:4636-4641`); on failure keep `gLessonBatch` and do not call `releaseLessonData()`.

**Tests.** `test_summary_remove_lesson_subjects`, `test_summary_remove_lesson_bucket_count`, `test_lesson_start_classify_http`, `test_lesson_start_plan_keeps_failed_items`, `test_lesson_start_plan_all_started`. Hardware: start a 5-item batch on Wi-Fi, then repeat offline and on a batch whose first item was already started elsewhere.

**Docs.** `ux.md` §5.8/§5.9 (confirmation text, offline text, progress); `api-sync.md` (PUT `/assignments/{id}/start`: 2xx and 422, per-item retry); `ROADMAP.md` row 3.

**Acceptance.**
- [ ] No start request is sent before `Enter` on the confirmation.
- [ ] 422 is treated as started and does not stop the batch; other errors list the failed items and offer retry.
- [ ] Started items leave the summary buckets, the snapshot is saved and Home shows the lower count without a sync.
- [ ] Offline start shows `Needs Wi-Fi — your batch stays here` and the batch is still browsable.
- [ ] Each successful start writes one journal entry when ARCH-03 exists.

### LES-04 — Ordering

- **Priority:** P2
- **Size:** M
- **Depends on:** LES-01, FIX-08, ARCH-03
- **Touches:** lesson controller (`startLessons()` `App.cpp:1894-1901` candidates, `:1924-1938` sort and trim); `firmware/src/wk/Summary.cpp` (`lessonSubjectIds()` `:71-79`); `firmware/src/platform/WkLessons.cpp` (`fetchLessonBatch()` `:31-47`, `fetchSubjectsByIds()` `:97-126`); `firmware/src/platform/WkClient.cpp` (`gBodyBuffer` `:22`); new pure `firmware/src/wk/LessonOrder.{h,cpp}`; tests
- **Settings:** adds L2 "Lesson order: WaniKani / Radicals→Kanji→Vocab / Interleave types"

**Problem / goal.** Only the first ten summary candidates were fetched and sorted: `candidateLimit = std::max<size_t>(lessonLimit, 10)` fed `gSummary.lessonSubjectIds(candidateLimit)`, so the sort never saw item 11 and later. WaniKani orders lessons by level then `lesson_position`, so on a large unlock the device could offer a vocabulary item before its radical (ROADMAP LESSON-08).

**Implementation (2026-11-06).** LESSON-08 is done: `startLessons()` now takes up to 60 summary ids, fetches the assignments in pages of 30, reads subject levels in pages of 10, and calls `kk::sortLessonCandidates()` (`wk/LessonOrder.{h,cpp}`) before trimming to the batch size. The L2 setting and its two alternative orders remain open in this task.

**Behaviour.** The list is built from up to ~60 candidate subject ids and sorted by L2: WaniKani order (level, then `lesson_position`) is the default, `Radicals→Kanji→Vocab` groups by type, `Interleave types` round-robins radicals, kanji and vocabulary. The chosen N are the only items fetched with details. The list header and row layout do not change.

**Design.** Request plan, in order: (1) `Summary::lessonSubjectIds(60)` — no request, the summary is cached; (2) `GET /v2/assignments?subject_ids=<60 ids>` through the existing `fetchLessonBatch()` call (`WkLessons.cpp:36-37`); the body cap is `gBodyBuffer[32768]` (`WkClient.cpp:22`, passed as `32768` at `WkLessons.cpp:37`), so 60 assignment objects may not fit — split the call into two pages of 30, and rely on FIX-14's truncation fallback (measure the real size on hardware, verify); FIX-08 is needed only for the larger summary id list; (3) level: `/assignments` carries neither `level` nor a reliable `lesson_position` (both belong to the subject), so read `(id, level, lesson_position, type)` from base subjects with `fetchSubjectsByIds(..., details=false)` in pages of 10 (`WkLessons.cpp:97-126`) or the ARCH-03 subject catalog when it stores them, keep only the tuples and drop the subjects before the detail fetch; (4) sort with `kk::sortLessonCandidates()` (`LessonOrder.h`), trim to N; (5) run the existing detail fetch and component fetch (`App.cpp:1939-1955`). Keep the canvas released across each call (`gFreeCanvasForRequest`, `App.cpp:1899`); do not keep 60 subjects alive across a TLS handshake.

**Tests.** `test_lesson_order_wanikani_level_then_position`, `test_lesson_order_type_groups`, `test_lesson_order_interleave_round_robin`, `test_lesson_order_ties_break_by_id`, `test_lesson_order_keeps_unknown_level_last`. Hardware: a fresh level with more than 10 unlocked lessons shows the radical first and the whole unlock is reachable.

**Docs.** `ux.md` §5.8 (ordering and L2); `api-sync.md` (the two-request plan, the body cap and FIX-08); `ROADMAP.md` LESSON-08 marked absorbed by LES-04, row 3 and the execution-order paragraph.

**Acceptance.**
- [ ] The candidate pool is larger than 10 and the sort sees every candidate.
- [ ] Default order is level then `lesson_position`; both L2 alternatives are reachable from Settings.
- [ ] Only the chosen batch is fetched with details, and no 60-subject vector survives a network call.
- [ ] Peak heap during `startLessons()` stays within the current budget (log free heap before and after).

### LES-05 — Workload guard

- **Priority:** P2
- **Size:** S
- **Depends on:** LES-01, ARCH-02
- **Touches:** lesson controller (`startLessons()` `App.cpp:1868-1899`, insert before the fetch and before the low-battery prompt at `:1879-1882`); `firmware/src/app/ModalScreens.{h,cpp}` (`LowBatteryView` `.h:18-28` as the template); `firmware/src/wk/SrsCounts.h` (`apprentice`, `:20-30`); `firmware/src/wk/Summary.h` (`dueReviews()` `:25`); `firmware/src/wk/Assignment.{h,cpp}` (`started_at` is not parsed today, verify); new pure `firmware/src/wk/WorkloadGuard.{h,cpp}`; tests
- **Settings:** adds L4 (Apprentice warning, default off), L5 (due reviews warning, default off), L6 (lessons started today, default off)

**Problem / goal.** Nothing warns before a lesson batch. The only gate is the battery prompt below 20% (`App.cpp:1879-1882`, `lowBatteryNow()` `:1426-1429`), so a user with a large Apprentice pile or a big review backlog can start ten new lessons and bury both queues.

**Behaviour.** Before the lesson list opens, when any enabled threshold is met, a screen lists the reason(s) — `Apprentice items: 120 (limit 100)`, `Reviews due: 240 (limit 150)`, `Lessons started today: 20 (limit 15)` — with `Enter: continue  Esc: back`. `Enter` continues into the normal fetch, `Esc` goes Home with the batch untouched. All three thresholds are off by default, so the screen never appears unless the user opts in.

**Design.** `kk::workloadWarning(apprentice, dueReviews, startedToday, thresholds)` in a new pure module returns a bitmask and formats the reason lines; the controller fills the inputs from `gSrs.apprentice` (cached `SrsCounts`, `App.cpp:87`, `:761-767`), `gSummary.dueReviews(effectiveNow())` and the started-today count. For that count use the ARCH-03 journal when it exists, otherwise count assignments whose `started_at` is today — `started_at` is currently not parsed into `Assignment` (`wk/Assignment.h:78-88`), so either add the field and parse it (verify) or rely on the journal only. Draw a new modal next to `drawLowBatteryScreen`; drive it with the `gLowBatteryPrompt` state pattern (`App.cpp:1431-1436`, `:2407-2425`). Order of gates: empty check, workload guard, then the existing low-battery prompt so battery stays the last word.

**Tests.** `test_workload_guard_apprentice_threshold`, `test_workload_guard_due_reviews_threshold`, `test_workload_guard_started_today_threshold`, `test_workload_guard_all_off`, `test_workload_guard_multiple_reasons`. Hardware: set L4 to a low value and confirm the screen appears once per entry and `Esc` returns Home.

**Docs.** `ux.md` §5.8 (guard screen); `03-settings.md` registry rows L4-L6 (defaults off); `ROADMAP.md` row 3.

**Acceptance.**
- [ ] With L4/L5/L6 off nothing changes.
- [ ] Each threshold alone shows the screen with its reason and the configured limit.
- [ ] `Enter` continues to the list, `Esc` returns Home without a fetch.
- [ ] The guard is evaluated before any network request.

### LES-06 — Card pages by topic

- **Priority:** P2
- **Size:** M
- **Depends on:** LES-01
- **Touches:** lesson controller (`showLesson()` `App.cpp:1660-1725`, `wrapLines()` `:1595-1619`, `subjectMainMeaning()` `:1621-1626`); `firmware/src/app/LessonScreens.{h,cpp}` (`LessonCardView` `.h:38-47`, `drawLessonCardScreen()` `.cpp:144-232`); new pure `firmware/src/wk/LessonPages.{h,cpp}`; tests
- **Settings:** none

**Problem / goal.** `showLesson()` concatenates the meaning mnemonic, component meanings, both hints and both notes into one string (`App.cpp:1687-1706`) and pages it three wrapped lines at a time (`:1707-1708`, drawn at `LessonScreens.cpp:218-225`). Reading material is mixed into the meaning text, so a long mnemonic pushes the components and notes off the card, and `partsOfSpeech` / `contextSentences` (`wk/Types.h:51-52`) never appear on a lesson card at all.

**Behaviour.** The card is split into topic pages with a title: `1 Meaning` (accepted meanings + meaning mnemonic + meaning hint), `2 Reading` (readings + reading mnemonic + reading hint; radicals and kana-only items skip it), `3 Components and notes` (component meanings, meaning and reading notes), `4 Context` (context sentences and parts of speech, only when present). Each page is paged within itself if the text is long; the header keeps `2/5` and `p1/3`, where `p` now counts topic pages. The page title is drawn above the text. Reading and the meaning panel on the right stay as they are.

**Design.** `kk::buildLessonPages(const Subject&, const std::vector<const Subject*>& components)` returns `std::vector<LessonPageText>{ const char* title; std::string body; }` — pure, no width logic. The controller wraps each body with the existing `wrapLines()` (needs the display, so it stays in the app) and fills a `std::vector<LessonPageView>{ title, lines }` on `LessonCardView`; `drawLessonCardScreen()` draws `pages[page].title` and three lines, and the page count comes from the same struct so `lessonKey()` (LES-01) can move between pages without guessing. Skip the Reading page when `!subject.needsReading()` (`wk/Types.h:67-69`) and the Context page when both vectors are empty. Hint bar unchanged.

**Tests.** `test_lesson_pages_kanji_topics`, `test_lesson_pages_radical_skips_reading`, `test_lesson_pages_context_only_when_present`, `test_lesson_pages_notes_page_omitted_when_empty`, `test_lesson_pages_titles`. Hardware: one kanji, one radical and one vocabulary lesson, with a long mnemonic, confirm every section is reachable and the `p` counter matches.

**Docs.** `ux.md` §5.9 (page topics, titles, radical exception); `ROADMAP.md` row 3.

**Acceptance.**
- [ ] Meaning, reading, components/notes and context each get their own page.
- [ ] Radicals have no Reading page and the counter reflects the shorter page list.
- [ ] The Context page appears only when the subject has sentences or parts of speech.
- [ ] The page counter in the header matches the number of topic pages actually drawn.

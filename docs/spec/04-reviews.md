# Review session

These twelve tasks turn the review session into a configurable, WaniKani-faithful loop and are the review half of wave 2. They assume the review fixes (FIX-03/04/05/09/10) and the `App.cpp` split plus settings registry and stores (ARCH-01/02) have landed, so only the task-specific dependencies are repeated below. Every option is registered in 03-settings.md with a WaniKani-faithful default, or today's KaniKey behaviour where WaniKani has no equivalent. Line references are against the 2026-11-01 tree; ARCH-01 moves the `App.cpp` blocks named here into controllers, and the function names stay the same. REV-08 is the only task that adds a network request; its cost is part of the design.

| ID | Title | Priority | Size | Depends on | Wave | Status |
|---|---|---|---|---|---|---|
| REV-01 | Show all accepted answers on feedback | P1 | S | ARCH-01, ARCH-02 | 2 | todo |
| REV-02 | Typo policy | P1 | S | FIX-03, ARCH-01, ARCH-02 | 2 | todo |
| REV-03 | Useful wrong-answer line | P2 | S | ARCH-01 | 2 | todo |
| REV-04 | Wrong-answer actions | P2 | M | ARCH-01, ARCH-02, ARCH-03 | 2 | todo |
| REV-05 | Wrap up | P2 | M | ARCH-01 | 2 | todo |
| REV-06 | Auto-advance on correct | P2 | S | ARCH-01, ARCH-02 | 2 | todo |
| REV-07 | Details on wrong | P3 | S | ARCH-01, ARCH-02 | 2 | todo |
| REV-08 | More review orders | P2 | L | ARCH-01, ARCH-02, ARCH-03; SPEED-05 for option B | 2 | todo |
| REV-09 | Back-to-back | P2 | S | ARCH-01, ARCH-02 | 2 | todo |
| REV-10 | Input editing | P2 | M | ARCH-01 | 2 | todo |
| REV-11 | Richer session summary | P2 | M | ARCH-01, SPEED-01 | 2 | todo |
| REV-12 | Minimal header | P3 | S | ARCH-01, ARCH-02 | 2 | todo |

### REV-01 — Show all accepted answers on feedback

- **Priority:** P1
- **Size:** S
- **Depends on:** ARCH-01, ARCH-02
- **Touches:** `firmware/src/app/App.cpp` (`correctAnswer()` 842-853, `showReview()` 891-899, `showLessonQuiz()` 1786-1795), `firmware/src/app/ReviewScreens.h` / `ReviewScreens.cpp` (feedback 118-190), `firmware/src/app/LessonScreens.h` / `LessonScreens.cpp` (`drawLessonQuizScreen()` 271-345), new `firmware/src/wk/AnswerDisplay.h` / `AnswerDisplay.cpp`, `firmware/test/test_wk/test_wk.cpp`
- **Settings:** none (always on)

**Problem / goal.** `correctAnswer()` (`App.cpp:842-853`) returns only the first accepted meaning or reading. The owner's capture shows `Composition` for 作文, whose accepted meanings are Composition / Writing / Essay. A learner who answers `Writing` is told the answer is only `Composition`, and a kanji with several readings sees one of them. The lesson quiz uses the same helper (`App.cpp:1792`).

**Behaviour.** On correct and incorrect feedback, show every accepted meaning joined with ` / ` and then the user synonyms in faint, e.g. `Composition / Writing / Essay  + essay, text`. Show every accepted reading; for a kanji whose `ReadingType` is parsed, group on'yomi first and kunyomi second, e.g. `じょう / せい  うえ`. Meanings use Font0 in the 136 px column, wrap to at most two lines and end with `…` when tokens are dropped. Readings use the glyph pack at 20 px, at most two lines; if a reading line is used, omit the note line. The lesson quiz feedback gets the same layout in its 150 px column.

**Design.**
- Add `acceptedMeaningTokens()` and `acceptedReadingTokens()` to `wk/AnswerDisplay`: filter `accepted`, keep WaniKani order, group kanji readings by `ReadingType` (`Types.h:24`, parsed at `SubjectParser.cpp:553-558`); unknown types stay flat.
- Keep synonyms out of the answer token list; return them separately so the view can draw them in `ui::kFaint`.
- Add `drawAnswerTokens()` in `ui/Text` (Font0, wraps 136/150 px, two lines, appends `…`) and `drawReadingTokens()` in `ui/Widgets` (uses `GlyphPack::textWidth(..., 20)` and `drawTextFit`).
- Replace `ReviewView::answer` and `LessonQuizView::answer` with `std::vector<std::string> answers; std::vector<std::string> synonyms;`; fill them in `showReview()` and `showLessonQuiz()`.
- Meaning layout: label at y=65, answers at y=74/82, note at y=92. Reading layout: answers at y=68/88; suppress the note when two lines are used.

**Tests.**
- Native: `accepted_meanings_all_in_order`, `accepted_meanings_skip_unaccepted`, `accepted_meanings_do_not_include_synonyms`, `accepted_readings_grouped_kanji`, `accepted_readings_unknown_type_flat`, `accepted_readings_skip_unaccepted`.
- Hardware: 作文 shows Composition / Writing / Essay; a kanji with on'yomi and kunyomi shows both groups; a long list ends in `…`; the lesson quiz matches.

**Docs.** `docs/ux.md` §5.5 and §5.10; `ROADMAP.md` review row.

**Acceptance.**
- [ ] 作文 feedback shows all accepted meanings, not only the primary.
- [ ] User synonyms follow the answers in faint, with no doubled separators.
- [ ] Kanji readings are grouped by on'yomi/kunyomi when the type is parsed; other items stay flat.
- [ ] Both columns stay inside their width over two lines and end with `…` when truncated.
- [ ] The lesson quiz uses the same layout.
- [ ] Native tests pass and the release/dev builds report flash and RAM.

### REV-02 — Typo policy

- **Priority:** P1
- **Size:** S
- **Depends on:** FIX-03, ARCH-01, ARCH-02
- **Touches:** `firmware/src/app/App.cpp` (`checkCurrent()` 1359-1360, `checkLessonQuiz()` 1754-1759), `firmware/src/wk/AnswerChecker.cpp` (191-196), `firmware/src/app/ReviewScreens.cpp` (183-189), `firmware/src/app/LessonScreens.cpp` (336-338), settings registry (`ARCH-02`), `docs/decisions.md`, `firmware/test/test_wk/test_wk.cpp`
- **Settings:** R6 "Typos" — WaniKani (accept close, default) / Strict (retry); key `rv.typos` (`03-settings.md`)

**Problem / goal.** `checkCurrent()` calls `checkAnswer(..., true)` (`App.cpp:1359-1360`) and the lesson quiz calls `submit(input, true)` (`App.cpp:1759`), so a close meaning is a warning. `checkAnswer(..., false)` already returns Correct (`AnswerChecker.cpp:192-196`), but its message is only `Correct, with a small typo`; the strict branch (`AnswerChecker.cpp:193-194`) prints the closest answer, which leaks the answer. WaniKani accepts a close meaning as correct and shows the accepted answer.

**Behaviour.** With the default WaniKani policy, a meaning within `typoTolerance()` of an accepted meaning or synonym (at least four characters, `Text.h:184`) is CORRECT with the note `Close: Composition`; the part is done, no mistake is counted, and auto-advance applies. With Strict, the same answer is CHECK, retries the same part, is not counted and the message does not contain the closest answer. Readings keep their current strictness; only close meanings change. Lessons follow the same policy. Offline and outbox behaviour is unchanged.

**Design.**
- Change `AnswerChecker.cpp:195` to `return correct("Close: " + closest);`.
- FIX-03 removes the closest text from the strict warning at `AnswerChecker.cpp:193-194`; do not duplicate that change here.
- Pass `strict = (R6 == Strict)` (registry read, ARCH-02) at `App.cpp:1359-1360` and `App.cpp:1759`.
- In `ReviewScreens.cpp:183-189` and `LessonScreens.cpp:336-338`, draw a non-empty correct `view.message` on the first note line, then the user note on the second; the message is already filled at `App.cpp:897` and `App.cpp:1788`.
- Add a dated `docs/decisions.md` entry that supersedes `2026-10-25 — Strict checking with warnings`: close meanings are accepted by default; Strict warns without revealing the answer.

**Tests.**
- Native: `close_meaning_is_correct_when_lenient`, `close_meaning_names_the_answer`, `close_meaning_warns_when_strict`, `strict_warning_hides_the_answer`, `short_word_typo_is_not_close`, `synonym_typo_is_close`.
- Hardware: type `Compostion` for Composition under both policies; check the note and the accuracy count; repeat in the lesson quiz.

**Docs.** `docs/decisions.md` (new 2026-11-01 entry); `docs/ux.md` §5.5 and §5.10; `ROADMAP.md`.

**Acceptance.**
- [ ] Default: a close meaning is CORRECT with `Close: <answer>` and does not change accuracy.
- [ ] Strict: the same input is CHECK, the message has no answer, and retry works.
- [ ] The lesson quiz follows the same setting.
- [ ] The superseding decisions entry exists and is dated.
- [ ] Native tests pass; builds report flash and RAM.

### REV-03 — Useful wrong-answer line

- **Priority:** P2
- **Size:** S
- **Depends on:** ARCH-01
- **Touches:** `firmware/src/app/ReviewScreens.cpp` (158-164, 183-189), `firmware/src/wk/Text.h` (`firstSentence()`), `firmware/test/test_wk/test_wk.cpp`
- **Settings:** none

**Problem / goal.** On an incorrect answer, `ReviewScreens.cpp:163` (reading) and `ReviewScreens.cpp:188` (meaning) print `view.message`, which is just `Incorrect reading` or `Incorrect meaning`; that repeats the NOT QUITE pill and uses the only free line. The hint that would help is `s->meaningHint` / `s->readingHint` (`Types.h:47-48`), or the user's own note.

**Behaviour.** On Incorrect, show the first sentence of the part's hint; if the hint is empty, show the user's note for that part; if both are empty, leave the line blank. On Warning, keep the checker message (`Close. Check the spelling…`, `That is the reading. Type the meaning`, etc.). On Correct, keep the note (and the REV-02 close line). Never show `Incorrect meaning` / `Incorrect reading` again.

**Design.**
- Add `firstSentence(std::string_view)` to `wk/Text.h`: trim, stop after `.`, `!`, `?`, `。`, `！` or `？` when followed by space or end; return the whole string if there is no stop.
- Add a small local `feedbackLine(const ReviewView&)` in `ReviewScreens.cpp`: Incorrect → `firstSentence(hint)` else note; Warning → `view.message`; Correct → note/message.
- Keep `drawWrapped(..., 2)`; append `…` when the sentence still overflows two lines.

**Tests.**
- Native: `first_sentence_period`, `first_sentence_question_and_exclamation`, `first_sentence_japanese_stop`, `first_sentence_without_stop`, `first_sentence_trims_leading_space`, `first_sentence_empty`.
- Hardware: a wrong 作文 meaning shows the first hint sentence; a wrong reading shows the reading hint; a warning still shows the `Close…`/`That is the reading…` text.

**Docs.** `docs/ux.md` §5.5.

**Acceptance.**
- [ ] Incorrect feedback shows a hint or the user's note, not the checker message.
- [ ] Warning feedback still shows the checker message.
- [ ] Correct feedback still shows the user note and the close note.
- [ ] Native tests pass.

### REV-04 — Wrong-answer actions

- **Priority:** P2
- **Size:** M
- **Depends on:** ARCH-01, ARCH-02, ARCH-03
- **Touches:** `firmware/src/wk/ReviewQueue.h` / `ReviewQueue.cpp` (answer 96-155, requeue 86-94), `firmware/src/app/App.cpp` (`checkCurrent()` 1348-1374, `reviewKey()` 1522-1576, outcome handling 1363-1371), `firmware/src/app/ReviewScreens.cpp` (196), `firmware/src/app/ReviewScreens.h`, journal (`ARCH-03`), `firmware/test/test_wk/test_wk.cpp`
- **Settings:** R7 "Wrong answer actions" — Off (default, WaniKani has no override) / On; key `rv.wrong` (`03-settings.md`)

**Problem / goal.** On NOT QUITE the only actions are Enter (next) and Tab (details). A typo or a valid answer the checker rejects cannot be accepted, and there is no way to put a known answer back without it counting as a mistake. `ReviewQueue::answer()` has already moved the item or requeued it by the time the feedback is drawn, so an undo is needed.

**Behaviour.** With the setting On, on Incorrect feedback: `C` marks the current part correct (undo the wrong count, accept the part, advance), `A` asks the part again later (undo the wrong count, requeue it unanswered, advance), `Tab` opens details, `Enter` advances as today, `Esc` pauses. The hint bar reads `C ok  A later  Tab info` on the left and `Esc quit` on the right; `Enter` and the full labels live in the help (CFG-04). On Warning, C/A do nothing. Keys are case-insensitive.

**Design.**
- In `ReviewQueue::answer(Incorrect)`, record `lastIncorrectAssignmentId_` and `lastIncorrectPart_` before advancing or requeueing.
- Add `bool undoLastIncorrect()`: find the item by id in `queue_`, decrement the matching counter (not below 0), clear that part's `attempted` flag, move the item to the front, restore `part_` to the failed part and return true. Return false when there is no recorded incorrect or the item is gone.
- Add `void askAgain()` that calls the existing `requeueFront()` (`ReviewQueue.cpp:86-94`), which resets the item's attempted flags and puts it back after `kRequeueGap`.
- In `reviewKey()` on Incorrect: `C` → `undoLastIncorrect()`, then `answer(Correct)`, then apply the outcome exactly as `checkCurrent()` does for completion (`gCompletedItem`, `gNeedsSubmit`, `gFeedbackCompleted`, `--gDueReviews`, `markCacheCompleted`); factor that block into one helper so both paths use it. `A` → `undoLastIncorrect()`, `askAgain()`. Both then call `advanceFeedback()`.
- Add `wrongAnswerOverride` to the ARCH-03 journal record (`C` or `A`); it is local only. The POST still sends the decremented `incorrect_*_answers` from `queueReview()` (`App.cpp:1146-1165`).
- Add `ReviewView::wrongShortcuts` and draw the compact bar only when the status is Incorrect and the setting is On.

**Tests.**
- Native: `undo_after_other_part`, `undo_after_requeue`, `undo_without_incorrect_is_noop`, `accept_marks_part_correct`, `accept_completes_when_other_part_done`, `ask_again_requeues_unanswered`, `ask_again_keeps_other_part_done`.
- Hardware: C on a wrong meaning completes the item when the reading was already done; A brings the meaning back later; C/A on a warning do nothing; the hint bar and help agree.

**Docs.** `docs/ux.md` §5.5 (keys and bar), §5.6; `ROADMAP.md`.

**Acceptance.**
- [ ] C accepts the failed part and the item completes/submits only when all parts are done.
- [ ] A returns the failed part to the queue without a mistake count.
- [ ] C/A are inert on warnings and correct feedback.
- [ ] The shortcut bar and help list the actions.
- [ ] Native tests pass.

### REV-05 — Wrap up

- **Priority:** P2
- **Size:** M
- **Depends on:** ARCH-01
- **Touches:** `firmware/src/wk/ReviewQueue.h` / `ReviewQueue.cpp`, `firmware/src/app/App.cpp` (`openPause()` 2108-2128, `pauseKey()` 2303-2328, `drawPause()` 3128-3135, `showReview()` 873-899, `startReview()`/`endReview()` 1438-1425), `firmware/src/app/ModalScreens.h` / `ModalScreens.cpp` (28-44), `firmware/src/app/ReviewScreens.cpp` (39-60), `firmware/src/app/ReviewScreens.h`, `firmware/test/test_wk/test_wk.cpp`
- **Settings:** none

**Problem / goal.** The pause menu only offers Continue / End session / Settings (`ModalScreens.cpp:28`). A user who wants to finish soon either ends the session and loses the batch or reviews everything. WaniKani's wrap up keeps the items already in progress and stops new ones.

**Behaviour.** Pause gains `Wrap up` after `Continue`: `Continue / Wrap up / End session / Settings`. Choosing it keeps the current item plus queued items where one part is already done, drops untouched items, returns to the current question and shows `WRAP UP 3` in the header instead of `REVIEWS` until the summary. `N` is the remaining item count after pruning. The progress counter and bar use the pruned total. Choosing Wrap up again is harmless. When the last kept item completes, the summary opens as usual. `Esc` closes pause and continues.

**Design.**
- Add `size_t ReviewQueue::wrapUp()`: keep `queue_.front()` unconditionally; keep an item when `(needsMeaning && meaningDone) || (needsReading && readingDone)`; erase the rest; do not change the current part; return `remaining()`.
- Add `bool gWrapUp` (reset in `startReview()` and `endReview()`); `showReview()` sets `view.wrapUpCount = gWrapUp ? gQueue.remaining() : 0`.
- `PauseView` gains `bool wrapUp` (always true here) or the row count is fixed at four; change the `kRows` array and loop at `ModalScreens.cpp:28-40`, and `pauseKey()` modulo 4 with Wrap up at index 1, End session at 2, Settings at 3.
- `drawReviewScreen()` draws `WRAP UP N` at 6,3 (Font1, `kDim`) when `wrapUpCount > 0`; the SRS/counter/progress stay.
- `reviewKey()` returns to the question after `wrapUp`; no network call.

**Tests.**
- Native: `wrap_up_keeps_current_and_partial`, `wrap_up_drops_fresh_items`, `wrap_up_is_idempotent`, `wrap_up_keeps_reading_only_item`, `wrap_up_count_after_current_completes`.
- Hardware: Wrap up from pause, header count, summary after the last kept item, Continue still works.

**Docs.** `docs/ux.md` §5.4 (pause rows) and §5.7; `ROADMAP.md`.

**Acceptance.**
- [ ] Pause lists four rows and Wrap up is selectable.
- [ ] Only the current item and items with one part done remain.
- [ ] The header shows `WRAP UP N` and the summary follows.
- [ ] Wrap up twice does not change the queue.
- [ ] Native tests pass.

### REV-06 — Auto-advance on correct

- **Priority:** P2
- **Size:** S
- **Depends on:** ARCH-01, ARCH-02
- **Touches:** `firmware/src/app/App.cpp` (`checkCurrent()` 1348-1374, `advanceFeedback()` 1376-1397, `reviewKey()` 1528-1539, input feed 1563-1574, `loop()` 4924-4937), settings registry (`ARCH-02`), `docs/ux.md`
- **Settings:** R8 "Auto-advance on correct" — Off (default, today's behaviour) / 0.5 s / 1 s / 2 s; key `rv.advance`, stored in milliseconds (`03-settings.md`)

**Problem / goal.** A correct answer always waits for Enter. R8 adds an optional delay after which the device advances by itself, and lets typing the next answer skip the wait; `docs/ux.md` §5.5 already describes this behaviour.

**Behaviour.** With R8 set to a delay, after CORRECT feedback the device advances after that delay unless a key is pressed. Pressing a printable key during correct feedback advances immediately and feeds that character into the next question's field. Any other key cancels the timer for that frame. Enter/Tab/Esc keep their current meanings. Wrong and warning feedback never auto-advance. The timer does not run while pause, settings, the detail card or the blank screen is open.

**Design.**
- Add `uint32_t gFeedbackCorrectAt`; set it in `checkCurrent()` when the status is Correct and in the REV-04 C path; clear it on any feedback exit.
- In `App::loop()` after the key pump (4924-4937), advance when `gReviewActive && gFeedback && !gReviewDetail && !gPauseActive && !gSettingsActive && !gScreenOff && gCheck.status == Correct && delayMs > 0 && millis() - gFeedbackCorrectAt >= delayMs`, where `delayMs` is the R8 value.
- In `reviewKey()`'s feedback branch, a printable character calls `advanceFeedback()` and then a new `feedReviewChar(c)` extracted from 1563-1574; other unhandled keys set `gFeedbackCorrectAt = 0`.
- No native timer is needed; keep the delay constant and the setting accessor pure.

**Tests.**
- Native: `auto_advance_due` for a pure `bool autoAdvanceDue(uint32_t delayMs, uint32_t elapsedMs)` helper (0 never advances).
- Hardware: correct answer advances after the delay; typing `t` shows the next reading field with `t`; pressing Tab opens details and no advance happens while it is open; a wrong or warning answer waits; the setting Off restores Enter-only.

**Docs.** `docs/ux.md` §5.5 (make the auto-advance default and delay explicit); `ROADMAP.md`.

**Acceptance.**
- [ ] A correct answer advances after the R8 delay; with R8 Off it waits for Enter.
- [ ] Typing a letter during correct feedback advances and feeds the letter.
- [ ] Wrong and warning feedback never auto-advance.
- [ ] Details/pause/settings/blank cancel the timer.
- [ ] Setting Off restores the current behaviour.

### REV-07 — Details on wrong

- **Priority:** P3
- **Size:** S
- **Depends on:** ARCH-01, ARCH-02
- **Touches:** `firmware/src/app/App.cpp` (`checkCurrent()` 1348-1374, `reviewKey()` 1524-1538, `$`detail page helpers`$` 912-925), `firmware/src/app/ReviewScreens.cpp` (233-294), settings registry (`ARCH-02`), `docs/ux.md`
- **Settings:** R9 "Details on wrong" — Off (default) / On; key `rv.details` (`03-settings.md`)

**Problem / goal.** The mnemonic is only behind Tab, and Tab always opens the first detail page (meanings), so a wrong meaning or reading does not show the page that explains it. R9 can open it automatically on the failed part.

**Behaviour.** With the setting On, when the checker returns Incorrect, the device opens the detail card at the meaning-mnemonic page (index 1, `2/5`) for a failed meaning or the reading-mnemonic page (index 2, `3/5`) for a failed reading. `Esc` or `Tab` returns to the NOT QUITE feedback with the typed answer intact. The card is opened once per wrong answer; returning does not reopen it, and a later Tab starts at page 1 as today. A missing mnemonic still shows the page with `No mnemonic cached.` Correct and warning feedback do not open the card.

**Design.**
- After `gCheck` and the queue outcome in `checkCurrent()`, if the status is Incorrect, the setting is On and `gFeedbackSubjectId` resolves, set `gReviewDetail = true`, `gReviewDetailPage = (gFeedbackPart == Meaning ? 1 : 2)` and `gReviewDetailPages = detailPageCount(*s)` (`App.cpp:912-914`).
- Keep `gFeedback = true`; `draw()` already prefers `gReviewDetail` (`App.cpp:4209-4215`), and `reviewDetailKey()` (`App.cpp:950-965`) already returns to feedback on Esc/Tab.
- No new view field.

**Tests.**
- Native: none (screen routing).
- Hardware: wrong meaning opens page 2, wrong reading page 3, Esc returns, Tab from feedback opens page 1, correct/warning do not open, missing mnemonic shows the empty state.

**Docs.** `docs/ux.md` §5.5 and §5.6; `ROADMAP.md`.

**Acceptance.**
- [ ] Wrong meaning opens the meaning mnemonic automatically when the setting is On.
- [ ] Wrong reading opens the reading mnemonic.
- [ ] Esc/Tab returns to the same feedback.
- [ ] Correct/warning do not open details; Tab still starts at page 1.
- [ ] Setting Off keeps today's behaviour.

### REV-08 — More review orders

- **Priority:** P2
- **Size:** L
- **Depends on:** ARCH-01, ARCH-02, ARCH-03
- **Touches:** `firmware/src/wk/ReviewQueue.h` / `ReviewQueue.cpp` (`build()` 23-58), `firmware/src/platform/WkReview.h` / `WkReview.cpp` (new due-assignment fetch), `firmware/src/app/App.cpp` (`runBatch()` 772-834, `startReview()` 1438-1490), `firmware/src/wk/Types.h` (`SessionItem` / `availableAt`), ARCH-03 catalog, `firmware/test/test_wk/test_wk.cpp`, `docs/api-sync.md`
- **Settings:** R2 "Order" — Account preference (default) / Random / Lowest level first / Current level first / SRS ascending / SRS descending / Oldest available first, key `rv.order`; R3 "Type order" — Off (default) / Radical→Kanji→Vocab, key `rv.types` (`03-settings.md`)

**Problem / goal.** `ReviewQueue::build()` (`ReviewQueue.cpp:23-58`) only shuffles and, for the account preference, stable-sorts by level. The account's `reviews_presentation_order` is read at `App.cpp:1487-1489`. SRS and availability are not used at all, and the choice of which due items enter the batch is made from `/summary` subject ids (`runBatch()` 772-834) that carry no `srs_stage` or `available_at`. R2/R3 need both a selection order and an in-batch order.

**Behaviour.** Account keeps today's behaviour (shuffled, or lower-levels-first from `/user`) and makes no extra request. Any other order applies to the frozen due set before the batch is limited: Random, level ascending, current level first (current level, then level ascending), SRS ascending/descending, oldest `available_at` first. Type order, when On, groups radicals, then kanji, then vocabulary/kanji-vocabulary inside the chosen order. Offline/cached batches use the cached assignments and the same order; if the extra request fails, the session still starts with the account order and the log says so, with no blocking error. The session never changes if the server adds reviews mid-session.

**Design.**
- Add `enum class ReviewOrderStrategy` to `ReviewQueue.h` and `setOrderStrategy()` / `setCurrentLevel()` / `setTypeOrder()`. Keep `setLowerLevelsFirst()` for the account preference or map `Account` to it.
- Add `availableAt` to `SessionItem` and copy it from `Assignment` in `build()`.
- Add a pure `selectDueSubjectIds(assignments, strategy, currentLevel, limit, seed)` in `ReviewQueue.cpp`: filter `due()` (`ReviewQueue.cpp:16-19`), apply the comparator (type rank last, stable), shuffle first for Random, take `limit`. `build()` applies the same comparator so the fetched order is not trusted.
- Add `fetchDueAssignments()` to `platform/WkReview`: `GET /v2/assignments?immediately_available_for_review=true` (the filter is named in `docs/api-sync.md:134`), pages of 500; follow `pages.next_url` (verify the field; if absent, page with the last id and `page_after_id`). Parse with the existing `parseAssignments()` (`Assignment.cpp:149-177`) or `parseAssignmentsStream()` (`:179-189`), which already read `id`, `subject_id`, `subject_type`, `srs_stage`, `available_at` and `hidden`.
- Level orders use the ARCH-03 subject catalog for `subject_id → level`; unknown levels sort last. Do not add a second request unless the catalog is missing (then the `levels` filter is the fallback).
- In `runBatch()`, when the setting is not Account, fetch the due assignments first, select ids, then pass them to `fetchReviewBatch()`; for Account keep `gSummary.dueSubjectIds(now, limit)`.
- Cost and memory (spike first): a page holds up to 500 assignments, roughly 150-250 KB of JSON (verify), which does **not** fit the 32 KB body buffer (`WkClient.cpp:22`). Two options, decided by a spike: (A) parse the page incrementally from the socket into `(id, subject_id, subject_type, srs_stage, available_at)` tuples (~20 B each) with a resumable tokenizer — `wk/JsonStream` currently needs the whole buffer, so this means making it chunk-fed; (B) request `GET /v2/assignments?subject_ids=…` in groups that fit 32 KB (~60 ids, using the due ids from the summary, FIX-08) over one keep-alive connection (SPEED-05). Option B is simpler but needs ~17 requests for 1,000 due items. Release the canvas around each request, keep no DOM across requests, and log page count, bytes and heap before/after.

**Tests.**
- Native: `order_random_is_a_permutation`, `order_lowest_level_first`, `order_current_level_first`, `order_srs_ascending`, `order_srs_descending`, `order_oldest_available_first`, `order_type_radical_kanji_vocab`, `order_limit_keeps_first_n`, `order_account_lower_levels_first`, `order_unknown_level_last`, `session_item_copies_available_at`.
- Hardware: with 300+ due items, check the request count/heap in the log, the visible order, a cached/offline batch, and that the account order still matches `/user`.

**Docs.** `docs/api-sync.md` §3.3 (move `immediately_available_for_review` from "future" to used, with the cost) and §5.4; `docs/ux.md` §5.4; `ROADMAP.md`.

**Acceptance.**
- [ ] Account makes no extra request and matches today's order.
- [ ] Each named order is visible and native-tested.
- [ ] Type order is stable inside the chosen order.
- [ ] A failed due-list request falls back to Account order without blocking.
- [ ] The request count, page size and heap cost are logged and documented.
- [ ] Cached/offline sessions still start.

### REV-09 — Back-to-back

- **Priority:** P2
- **Size:** S
- **Depends on:** ARCH-01, ARCH-02
- **Touches:** `firmware/src/wk/ReviewQueue.h` / `ReviewQueue.cpp` (`setPart()` 60-78, `prepareItem()` 80-84, `requeueFront()` 86-94, `answer()` 96-155), `firmware/src/app/App.cpp` (`startReview()` 1487-1490), `docs/ux.md`, `firmware/test/test_wk/test_wk.cpp`
- **Settings:** R5 "Back-to-back" — On (default, today's KaniKey behaviour) / Off (interleaved parts, closer to WaniKani's mixed queue); key `rv.b2b` (`03-settings.md`)

**Problem / goal.** `ReviewQueue::answer()` always asks the other part of the same item next: after a wrong answer at `ReviewQueue.cpp:118-124`, and after a correct part at `ReviewQueue.cpp:151-153`. So parts of different items never interleave, unlike WaniKani's mixed queue; R5 Off adds that mode.

**Behaviour.** On keeps today's behaviour: after one part, the other part of the same item follows immediately. Off: after a part is answered, the item goes back into the queue at a random position ahead and the next item is asked; the item keeps the part that is already done. After the last required part the item completes immediately, with no requeue. A wrong answer counts, and the failed part comes back later with the other part, rather than asking the other part immediately. With only one item in the queue, the other part is still asked so the queue cannot spin.

**Design.**
- Add `void setBackToBack(bool)` and a member; default true.
- Add an `std::mt19937 rng_` seeded by `build()`'s `seed` (or reuse a saved engine) and an `interleaveFront()` that pops the front, reinserts it at `1 + rng_() % max(1, queue_.size())` (never the very front when more than one item exists), then sets the part for the new front.
- In `answer(Correct)` when `!allDone && !backToBack_`, call `interleaveFront()` instead of `setPart(item)`; keep `out.advanced = true`, leave `completed` false.
- In `answer(Incorrect)` with `otherPending && !otherAttempted`: back-to-back keeps the current branch (`part_` switches); Off calls `interleaveFront()` without resetting `attempted` flags and without completing.
- In `requeueFront()` keep the fixed `kRequeueGap` for mistakes; the interleave path is separate so the two policies do not interfere.

**Tests.**
- Native: `back_to_back_on_asks_other_part`, `back_to_back_off_interleaves_two_items`, `back_to_back_off_preserves_done_part`, `back_to_back_off_last_part_completes`, `back_to_back_off_wrong_requeues_other_part`, `back_to_back_off_single_item_does_not_stall`, `back_to_back_off_same_item_not_first`.
- Hardware: with two items and Off, meaning A → meaning B → reading A → reading B; with On, A meaning → A reading; a single-item session finishes.

**Docs.** `docs/ux.md` §5.4 (part order); `ROADMAP.md`.

**Acceptance.**
- [ ] On is byte-for-byte today's order for the existing queue tests.
- [ ] Off interleaves items and keeps completed parts.
- [ ] The last part still completes immediately.
- [ ] A single-item queue cannot stall.
- [ ] Native tests pass.

### REV-10 — Input editing

- **Priority:** P2
- **Size:** M
- **Depends on:** ARCH-01
- **Touches:** `firmware/src/ui/KeyInput.h` (`keyRepeats()` 43-45, `KeyRepeat` 150-174), `firmware/src/app/App.cpp` (`loop()` 4924-4937, Backspace 1548-1561, feed 1563-1574, lesson input 1804-1859, serial `y` 4734-4748), `firmware/src/wk/RomajiIme.h` / `RomajiIme.cpp` (14-30, 65-138), `firmware/src/wk/Text.h`, `firmware/test/test_wk/test_wk.cpp`
- **Settings:** none

**Problem / goal.** (a) `keyRepeats()` returns true only for arrows, and `KeyRepeat` exists but is not called anywhere in `App::loop()` (4924-4937), so `ux.md` §3.4's 400/120 ms Backspace repeat is not implemented. (b) There is no way to clear a field. (c) `RomajiIme::backspace()` pops one raw byte (`RomajiIme.h:22-24`), so Backspace on `たべ` deletes the last romaji letter and leaves a half-converted string. (d) `gMeaningInput.push_back(c)` (`App.cpp:1566`) has no length limit.

**Behaviour.** Holding Backspace deletes repeatedly after 400 ms at 120 ms; Enter/Esc still do not repeat. Fn+Backspace or Ctrl+Backspace clears the active field (meaning or reading, including the romaji line). A Backspace in the reading field removes the last kana unit: `たべ` → `た`, `かん` → `か`, `ん` → empty, `んや` → `ん`; plain ASCII that has not converted still deletes one character. Answers stop at 64 bytes; further keystrokes and serial injections are ignored. The lesson quiz obeys the same rules.

**Design.**
- (a) Add `Key::Backspace` to `keyRepeats()`; add a `KeyRepeat` member/static in `App.cpp`, feed `down()`/`up()` for every event in `loop()` (4924-4937) and `cancel()` on `Key::None`; after the pump, when `poll(now)` returns Backspace and the review/lesson input is active (not feedback/detail/pause), dispatch a synthetic `KeyEvent{Backspace, true}` to `onKey()`. Do not repeat Enter/Esc.
- (b) In the Backspace branches of `reviewKey()` (1548-1561) and `lessonQuizKey()` (1832-1845), clear the active input when `gFn || gCtrl`: `gMeaningInput.clear()`, `gIme.clear()`, `gDirectReading.clear()` / `gLessonQuizInput.clear()`, `gLessonQuizIme.clear()`, `gLessonQuizDirectReading.clear()` as applicable.
- (c) Make `RomajiIme::backspace()` kana-aware: compute `text()`, find the longest raw prefix `k < raw_.size()` whose `romajiToHiragana(raw_.substr(0, k))` is a proper prefix of `text()` and one kana shorter, erase from `k`; if none, pop one byte. This keeps `nn`/`n'`/`nna`/`kan`/`kann`/`tta` correct without a second table. Keep `raw()` for the dim line.
- (d) Add `constexpr size_t kMaxAnswerBytes = 64` and `appendCapped(std::string&, char, size_t)` to `wk/Text.h`; use it at 1566, 1850 and the serial `y`/`text` appends (4738, 4744, 4746).
- Update the stale `KeyInput.h` comment that says `main.cpp` honours repeats.

**Tests.**
- Native: `romaji_backspace_ta`, `_tabe`, `_n`, `_nn`, `_nna`, `_n_apostrophe`, `_kan`, `_kann`, `_n_apostrophe_ya`, `_sokuon_tta`, `_ascii_only`, `_empty_noop`, `answer_cap_64_bytes`, `append_capped_stops_at_limit`.
- Hardware: hold Backspace in a long meaning; Fn+Backspace and Ctrl+Backspace clear; `たべ` backspaces to `た`; a 100-character serial paste stops at 64; the lesson quiz matches.

**Docs.** `docs/ux.md` §3.4 (repeat is now actually wired) and §5.4 (editing); `ROADMAP.md`.

**Acceptance.**
- [ ] Held Backspace repeats at 400/120 ms and stops on release.
- [ ] Fn/Ctrl+Backspace clears the active field.
- [ ] Reading Backspace removes kana units, including the `nn`/`n'` cases.
- [ ] Answers never exceed 64 bytes, including serial input.
- [ ] Lesson quiz behaviour matches; native tests pass.

### REV-11 — Richer session summary

- **Priority:** P2
- **Size:** M
- **Depends on:** ARCH-01, SPEED-01
- **Touches:** `firmware/src/app/ReviewScreens.h` / `ReviewScreens.cpp` (`drawReviewSummaryScreen()` 407-498), `firmware/src/app/App.cpp` (`showReviewSummary()` 967-1005, summary keys 2480-2482, `checkCurrent()` 1348-1374, `endReview()` 1399-1425), `firmware/src/wk/SessionStats.h`, `firmware/test/test_wk/test_wk.cpp`, SPEED-01 per-assignment stages
- **Settings:** none

**Problem / goal.** The summary only shows `3 mistakes` (`ReviewScreens.cpp:455-458`), with no way to see which items were missed; the stage line reflects only the last successful POST (`gStageChange`, `App.cpp:981-983`); and the only exit is Enter to Home (`App.cpp:2480-2482`), even when more reviews are due.

**Behaviour.** The mistakes row becomes focusable. Down or Tab moves the focus into the list; Up/Down move between missed items; Esc returns the focus to the stats; Enter goes Home. Each row shows the prompt character, the primary meaning, the reading, the wrong-answer count (or the last typed wrong answers) and the item's stage change when known (`SRS 3→4`; `pending` when the POST has not answered). `R: next batch` is shown only when due reviews remain; R starts a new batch from the summary without going Home, using the remaining cached batch or fetching a new one. The list shows at most three rows with a `1-3/7` counter. `No mistakes` is shown when the list is empty. The footer is `Enter: home  R: next batch` with R available, otherwise `Enter: home`.

**Design.**
- Add `ReviewSummaryItem { const Subject* subject; uint8_t stageFrom, stageTo; bool stageValid; uint8_t incorrectMeaning, incorrectReading; std::string wrongMeaning, wrongReading; }` and `std::vector<ReviewSummaryItem> items; bool canNextBatch; int focus; int selected;` to `ReviewSummaryView`.
- Add a pure `summaryMistakes(completed)` in `wk/SessionStats.h`: filter items with `incorrectMeaning + incorrectReading > 0`, keep assignment order, return ids/counts; test it.
- Record the typed wrong answers in `checkCurrent()` on Incorrect in a bounded `std::vector<WrongAnswer>` in `App.cpp` (assignment id, part, up to 64 bytes); clear it in `endReview()`.
- Fill stage data from SPEED-01's per-assignment map (verify its accessor); when absent, set `stageValid = false` and show `pending`.
- `drawReviewSummaryScreen()`: `focus == 0` keeps the current layout and adds the footer hint; `focus == 1` replaces the stage/item area with the scrollable rows and draws the selected row with `kSurfaceHi`.
- Summary keys: Down/Tab enter or move, Up moves, Esc leaves the list, Enter `endReview()`, R `if (gDueReviews > 0) startReview();` (`startReview()` already resets `gReviewSummary` at 1501 and fetches or reuses the batch).
- Keep the existing `reviews`, `accuracy`, `stageUps`, `duration` and outbox values.

**Tests.**
- Native: `summary_mistakes_only_incomplete_items`, `summary_mistakes_counts_both_parts`, `summary_mistakes_empty`, `summary_stage_change_text`, `summary_selection_wraps`, `summary_can_next_batch_when_due`.
- Hardware: a session with three mistakes opens the list, rows show character/meaning/reading/counts, Down/Tab move, Esc returns, Enter goes Home, R starts another batch, and R is absent when due is zero.

**Docs.** `docs/ux.md` §5.7; `ROADMAP.md`.

**Acceptance.**
- [ ] The mistakes row opens a list with character, meaning, reading and wrong answers.
- [ ] Down/Tab/Up/Esc navigate; Enter still goes Home.
- [ ] R starts another batch only when due reviews remain.
- [ ] Stage changes come from every completed item via SPEED-01; unknown stages show `pending` or are omitted consistently.
- [ ] The list fits the 240×135 screen with three rows and a counter.
- [ ] Native tests pass.

### REV-12 — Minimal header

- **Priority:** P3
- **Size:** S
- **Depends on:** ARCH-01, ARCH-02
- **Touches:** `firmware/src/app/ReviewScreens.cpp` (39-60), `firmware/src/app/ReviewScreens.h` (`ReviewView`), `firmware/src/app/App.cpp` (`showReview()` 881-899), settings registry (`ARCH-02`), `docs/ux.md`
- **Settings:** R10 "Header info" — SRS+accuracy (default, today's behaviour) / Minimal; key `rv.header` (`03-settings.md`)

**Problem / goal.** The header always draws `SRS 3 86%` (`ReviewScreens.cpp:43-51`). R10 wants a Minimal mode that hides it; today there is no setting and no way to reclaim the space.

**Behaviour.** With SRS+accuracy (default), the header is unchanged: `REVIEWS`, `SRS 3 86%`, `12/48`. With Minimal, the `SRS N NN%` string at x=62 is not drawn; `REVIEWS`, the position counter and the 2 px progress bar stay. The `KANJI L5` pill in the left panel and the SRS value on feedback stay. The setting applies immediately to the next review screen.

**Design.**
- Add `bool minimalHeader` to `ReviewView`; `showReview()` sets it from R10 (registry read, ARCH-02).
- In `drawReviewScreen()`, wrap the block at 43-51 in `if (!view.minimalHeader)`; keep the right counter and bar unchanged.
- No layout shift for the remaining header elements.

**Tests.**
- Native: none (drawing only).
- Hardware: screenshots in both modes; the counter and bar stay aligned; changing the setting takes effect on the next question.

**Docs.** `docs/ux.md` §5.4 (header options); `ROADMAP.md`.

**Acceptance.**
- [ ] SRS+accuracy keeps `SRS 3 86%`; Minimal hides it and nothing else.
- [ ] The counter and progress bar stay correct in both modes.
- [ ] The setting persists in NVS and applies without a reboot.
- [ ] Hardware screenshots are attached to the review.

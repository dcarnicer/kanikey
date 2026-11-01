# Statistics

Decision D4 puts statistics in scope; this file replaces ROADMAP `EXT-05` and `WK-11`. Today Home only says `Next review 18:00` even when 409 reviews are due, and there is no way to see the forecast, the SRS split or level progress without the web app. All four tasks work from data the device already caches (summary buckets, SRS counts, level progressions) plus the device-local journal from ARCH-03; none of them needs WaniKani content. `GET /v2/reviews` is deprecated and returns an empty array (verified in the API reference, 2026-11-01), so history must be local.

| ID | Title | Priority | Size | Depends on | Wave | Status |
|---|---|---|---|---|---|---|
| STATS-01 | Forecast on Home | P2 | S | ARCH-01 (controller split) | 3 | todo |
| STATS-02 | Stats screen | P2 | M | ARCH-01 | 3 | todo |
| STATS-03 | History and streak | P2 | M | ARCH-01, ARCH-03, STATS-02 | 3 | todo |
| STATS-04 | Level-up estimate | P3 | S | STATS-02, STATS-03 | 3 | todo |

### STATS-01 — Forecast on Home

- **Priority:** P2
- **Size:** S
- **Depends on:** ARCH-01 (controller split)
- **Touches:** `firmware/src/app/App.cpp` (`gNextReview` :94, `applySummary()` :288-298, `buildHomeView()` :3051-3067), `firmware/src/app/HomeScreen.cpp` (:60-67), `firmware/src/app/HomeScreen.h` (`HomeView::nextReview` :19), `firmware/src/wk/Summary.h` (:23-31), `firmware/src/wk/Summary.cpp` (:50-57), `firmware/test/test_wk/test_wk.cpp`, `docs/ux.md`, `ROADMAP.md`
- **Settings:** none

**Problem / goal.** `applySummary()` (`App.cpp:288-298`) keeps only the next hour (`localHm`), so the bucket count is lost and the owner's capture shows `Next review 18:00` while 409 reviews are already due. `HomeScreen.cpp:60-67` renders one short value next to the fixed label `Next review`. `gNextReview` is `char[12]` (`App.cpp:94`), which cannot hold `+12 at 18:00` plus the terminator.

**Behaviour.** With due > 0 and the next bucket holding n items at HH:MM, Home shows `Next review   +n at HH:MM`. With due = 0 and a next bucket it shows `Next review   HH:MM (+n)`. With due > 0 and no later bucket it keeps `now`; with nothing due or scheduled it keeps `all clear`. The label stays `Next review`; the value is right-aligned at x=232 as today. Offline/cached summaries follow the same rules. A bucket count of 1 is written without a plural (`+1 at 18:00`). If the clock is untrusted the cached summary still produces the relative text with the local `--:--` fallback from `localHm`.

**Design.** Add to `wk/Summary.h` a pure `uint16_t nextReviewCount(uint32_t now) const` and `void formatNextReview(const Summary&, uint32_t now, const char* hhmm, char* out, size_t n)`. Both reuse the scan already in `Summary::nextReviewAt()` (`Summary.cpp:50-57`); write the scan once and derive both values from it. `applySummary()` computes `hhmm` through the existing `localHm()` (`App.cpp:2576-2584`), calls the helper and widens `gNextReview` to 20 bytes. No new view field, no new cache field, no API request.

**Tests.** `test_forecast_line_due_with_next` (`+12 at 18:00`), `test_forecast_line_idle_with_next` (`18:00 (+12)`), `test_forecast_line_due_without_next` (`now`), `test_forecast_line_idle_without_next` (`all clear`), `test_forecast_count_single_and_zero`; add the cases to `RUN_TEST` next to `test_summary_parse_and_counts` (:2024). Hardware check: sync, let a bucket pass, confirm the count and time on the panel.

**Docs.** `docs/ux.md` §5.3 (`Next review` line) and §5.15 empty states; `ROADMAP.md` rows for `WK-11` (:333) and `EXT-05` (:648).

**Acceptance.**
- [ ] With reviews due and a later bucket, Home shows `+n at HH:MM` and the count matches the summary.
- [ ] With nothing due, Home shows `HH:MM (+n)`; with no bucket it shows `all clear`.
- [ ] The helper has native tests for all four branches and both builds pass.
- [ ] No layout regression: the value still clears the `Next review` label at 240 px.

### STATS-02 — Stats screen

- **Priority:** P2
- **Size:** M
- **Depends on:** ARCH-01
- **Touches:** `firmware/src/app/App.cpp` (Home shortcut :2502-2519, draw routing :4243-4248, `drawStatus()` guard :4250-4254), new `firmware/src/app/StatsController.{h,cpp}`, new `firmware/src/app/StatsScreen.{h,cpp}`, new `firmware/src/wk/Stats.{h,cpp}`, `firmware/test/test_wk/test_wk.cpp`, `docs/ux.md`, `docs/api-sync.md`, `ROADMAP.md`
- **Settings:** none

**Problem / goal.** There is no stats screen; `docs/ux.md` §5.14 (:612-618) is still a phase-2 placeholder. The cached summary already has 24 hourly review buckets (`Summary.h:11-21`), the SRS distribution is cached (`SrsCounts`, `App.cpp:87`), and `gLevelProgress` holds level, unlock date and kanji counts (`App.cpp:82`, `LevelProgress.h:10-21`), but Home shows none of the detail. `T` is unused on Home.

**Behaviour.** `T` on Home opens a read-only `STATS` screen; `Esc` returns Home. `Left`/`Right` move between four pages, clamped at the ends, with `STATS 1/4` in the header and `Esc: back  Left/Right: page` at y=125. Page 1 `Forecast`: 24 one-hour bars in a 228×80 px area at (6,26), baseline and four labels every 6 h in local time (`00 06 12 18` style time), empty state `No reviews in the next 24 h`. Page 2 `SRS`: Apprentice/Guru/Master/Enlightened/Burned with counts and proportional bars using the Home colours (`HomeScreen.cpp:73-76`), `Sync for counts` when invalid. Page 3 `Level`: `Level 12`, `8 days`, `kanji 12/34 (35%)`; missing counts keep the dates and drop the fraction; radicals are not fetched today, so show `radicals: unavailable` rather than guessing (verify whether a count is added later). Page 4 is STATS-03. Stats uses only cached state and never blocks on the network; `T` is added to the Home hint only if it fits, otherwise it is listed in help (CFG-04). The optional per-stage split (`A1-A4`, `G1-G2`) may be added only if it uses the same five count requests; today it needs six more requests per full sync (`WkSrsCounts.cpp:13-19`), so skip it unless the owner accepts that cost.

**Design.** Add `gStatsActive`/`gStatsPage` in the stats controller created by ARCH-01; `App.cpp` only routes the key (`onKey`, after the Home `S` branch :2514-2519), draws before `gShowHome` (:4243), and adds the flag to the `drawStatus()` guard. A `StatsView` carries the page number plus `ForecastView { uint16_t counts[24]; char labels[4][6]; uint16_t max; }`, the five SRS counts, and level fields; keep it under ~300 B and on the stack. `wk/Stats` gets `buildForecast(const Summary&, uint32_t now, ForecastView*)`: slot i is the hour `floor(now/3600)*3600 + i*3600`, gaps are zero, buckets outside the window are ignored, `max` is the largest count. Labels are formatted by the controller with `localHm()` (`App.cpp:2576`) around slots 0/6/12/18. `StatsScreen.cpp` draws the frame with `ui::kLine`, `ui::kBrand`, `ui::kDim`, `ui::kText`; no new colours and no `present()`.

**Tests.** `test_forecast_24_slots_fill_gaps`, `test_forecast_includes_current_hour`, `test_forecast_ignores_past_buckets`, `test_forecast_max_and_empty`; hardware checks for legibility in RGB332 and for all four pages on `cardputer-adv`. If the stage split is implemented, add `test_srs_stage_split_counts`; otherwise it is absent from the acceptance.

**Docs.** `docs/ux.md` §5.14 (real screen), §5.3 (Home `T`/hint), §1.3 key table (:150-163); `docs/api-sync.md` §3 note that stats adds no endpoint; `ROADMAP.md` `EXT-05`/`WK-11` rows.

**Acceptance.**
- [ ] `T` opens Stats from Home and `Esc` returns Home; all four pages reachable with `Left`/`Right`.
- [ ] Forecast shows 24 bars, four local-time labels and an empty-state message.
- [ ] SRS page matches the Home distribution or shows `Sync for counts`.
- [ ] Level page shows level, days on level and the kanji fraction; missing data degrades without a wrong number.
- [ ] Stats opens with no network and no new request; the hint or help lists `T`.
- [ ] `pio test -e native`, `pio run -e cardputer-adv` and `pio run -e cardputer-adv-dev` pass.

### STATS-03 — History and streak

- **Priority:** P2
- **Size:** M
- **Depends on:** ARCH-01, ARCH-03, STATS-02
- **Touches:** `firmware/src/wk/Stats.{h,cpp}`, `firmware/src/app/StatsScreen.{h,cpp}`, `firmware/src/app/StatsController.{h,cpp}`, the ARCH-03 journal read interface (no write-path edit here), `firmware/test/test_wk/test_wk.cpp`, `docs/ux.md`, `docs/api-sync.md`, `docs/decisions.md`, `ROADMAP.md`
- **Settings:** none

**Problem / goal.** The API no longer stores review history: `GET /v2/reviews` is deprecated and returns an empty `data` array (verified in the WaniKani API reference, 2026-11-01), yet `docs/api-sync.md:84-88` still lists it as an optional phase-2 source. Without it there is no 7-day history, accuracy or streak. ARCH-03 introduces the device-local journal that can supply this, but nothing reads it for statistics yet.

**Behaviour.** Page 4 is `History` with the subtitle `On this device`. It shows seven bars for the last seven local days including today, each labelled with a two-letter weekday and its review count; `Today 86%` (or `Today -` when no answers were recorded), `Streak 6 days`, `Best 21 days`. An empty journal shows `No reviews on this device yet` and still shows the streak zeros. Current streak counts consecutive local days with at least one review ending today; when today has none yet, it is counted from yesterday so the morning does not reset the display. Best streak is the longest such run in the stored journal. Day boundaries are local time, including DST changes.

**Design.** Do not add a `reviews` fetch. Read the daily rollup from ARCH-03 (proposed `JournalDay { uint32_t dayKey; uint16_t reviews; uint16_t correct; uint16_t incorrect; }`; align the name with the actual ARCH-03 API — verify). Put the pure logic in `wk/Stats`: `uint32_t localDayKey(uint32_t epoch, int32_t utcOffsetSeconds)`, `HistoryStats buildHistory(const JournalDay* days, size_t n, uint32_t todayKey)`, `uint8_t accuracyPercent(correct, incorrect)`. `dayKey` is the local days-since-epoch value stored when the review was journaled, so a later time-zone edit cannot move old reviews. If ARCH-03 stores raw reviews, aggregate them in the controller and keep only the last 90 days; do not hold the whole history in RAM. The current streak walks back from `todayKey`; the best streak scans once. The controller fills `HistoryView` with seven counts, labels, today accuracy and the two streaks; `StatsScreen.cpp` draws bars like the forecast page.

**Tests.** `test_journal_seven_day_counts`, `test_journal_today_accuracy`, `test_journal_current_streak_today_empty`, `test_journal_best_streak_with_gaps`, `test_journal_day_key_time_zone_boundaries` (+02:00 and −05:00 crossing midnight), `test_journal_empty`; hardware check: review a few items, reopen Stats offline and confirm the bars/streak.

**Docs.** `docs/api-sync.md` move `GET /reviews` (:84-88) to a deprecated/not-used note and describe the local journal; `docs/ux.md` §5.14 page 4; new dated entry in `docs/decisions.md` recording that history is device-local because the endpoint is deprecated; `ROADMAP.md` `WK-11`/`EXT-05`.

**Acceptance.**
- [ ] No code path calls `GET /v2/reviews`.
- [ ] Page 4 shows 7 local-day bars, today's accuracy, current streak, best streak and the `On this device` label.
- [ ] An empty journal shows the empty message instead of fake data.
- [ ] Day keys are time-zone aware and the seven named tests pass.
- [ ] The page works offline from the journal only.

### STATS-04 — Level-up estimate

- **Priority:** P3
- **Size:** S
- **Depends on:** STATS-02, STATS-03
- **Touches:** `firmware/src/wk/Stats.{h,cpp}`, `firmware/src/app/StatsScreen.{h,cpp}`, `firmware/src/app/StatsController.{h,cpp}`, `firmware/test/test_wk/test_wk.cpp`, `docs/ux.md`, `ROADMAP.md`
- **Settings:** none

**Problem / goal.** The Level page shows `kanji 12/34 (35%)` but not how many more kanji are needed to reach WaniKani's 90% pass threshold, so the user cannot tell how close the level-up is. No estimate exists today. The only Apprentice figure on the device is the account-wide `gSrs.apprentice` (`SrsCounts.h:21-27`); a per-level Apprentice count would need an extra assignments request, which this S-only task does not add.

**Behaviour.** The Level page gains one line under the kanji fraction: `Need 12 kanji for 90%` when passed is below the threshold, `90% reached` when it is at or above, and no line when the total is unknown. Next to it, `A 42 total` shows the cached account-wide Apprentice count with the word `total` so it is not read as level-specific; with no cached SRS it says `A -- total`. No date or "level up in N days" prediction is shown. The line uses existing cached data only.

**Design.** Add pure `uint16_t kanjiNeededForNinetyPercent(uint16_t passed, uint16_t total)` to `wk/Stats`: threshold `ceil(total * 9 / 10)`, result `threshold > passed ? threshold - passed : 0`, `0` when `total == 0`. Reuse `levelKanjiPercent()` (`LevelProgress.cpp:56-60`) for the existing fraction. The controller passes `kanjiPassed`, `kanjiTotal`, the computed need and `gSrs.apprentice`/`gSrs.valid` into `LevelView`; `StatsScreen.cpp` prints one line with `snprintf` into a fixed buffer. Do not add a sync row or a request.

**Tests.** `test_kanji_needed_rounds_up` (e.g. 40 total → 36 threshold, passed 17 → 19), `test_kanji_needed_zero_when_reached` (passed 36/40 and 40/40), `test_kanji_needed_unknown_total`, `test_kanji_needed_small_levels` (1/3 → 3, 2/3 → 1); hardware check that the line fits below the fraction on the Level page.

**Docs.** `docs/ux.md` §5.14 Level page; `ROADMAP.md` `EXT-05` row; note in the task report that the Apprentice figure is account-wide (verify with the owner if a per-level count is wanted later).

**Acceptance.**
- [ ] The Level page shows `Need N kanji for 90%` and `90% reached` correctly.
- [ ] `A <n> total` comes from cached SRS, or `A -- total` when unavailable.
- [ ] No date prediction and no new network request are introduced.
- [ ] The four helper tests pass and both builds succeed.

# Faster open → review → close

The README promise is "open it, the pending review appears, type, close". Today the loop is slow at every step: each API call opens a new TLS connection (`platform/WkClient.cpp:112`, `Connection: close` at `:138`) and costs ~2 s (`docs/hardware.md:63-64`); a wake from deep sleep is a cold boot whose `setup()` runs `startSync()` (`firmware/src/app/App.cpp:4885`), a modal 10-request sync of 20-25 s; `Enter` without a cached batch adds 8-10 requests; and every completed item POSTs for ~2 s with the canvas released (`App.cpp:1085-1087`), freezing the screen. This file removes those waits: defer submissions, trim and background the boot sync, prefetch the batch, reuse the TLS connection and keep RAM alive in light sleep. SPEED-02, SPEED-03 and SPEED-04 rewrite the same sync code and run in that order on one branch; SPEED-05 touches the HTTP client and the sync steps, so it starts after SPEED-03 merges (its spike can run any time).

| ID | Title | Priority | Size | Depends on | Wave | Status |
|---|---|---|---|---|---|---|
| SPEED-01 | Submission policy | P1 | M | FIX-10 | 2 | todo |
| SPEED-02 | Quick and full sync | P1 | M | none | 2 | todo |
| SPEED-03 | Background (non-modal) sync | P1 | L | SPEED-02 | 2 (after 02) | todo |
| SPEED-04 | Prefetch the review batch | P2 | M | SPEED-02, SPEED-03 | 2 (after 02/03) | todo |
| SPEED-05 | TLS keep-alive (spike, then implement) | P2 | L | none | 2 | todo |
| SPEED-06 | Light sleep before deep sleep (spike first) | P2 | M | SPEED-01 (for `endReview(send)`) | 2 | todo |
| SPEED-07 | Home updates over time | P2 | S | none | 2 | todo |

### SPEED-01 — Submission policy

- **Priority:** P1
- **Size:** M
- **Depends on:** FIX-10
- **Touches:** `firmware/src/app/App.cpp` (`queueReview` :1146, `sendOutboxItem` :1035, `checkCurrent` :1348, `advanceFeedback` :1376, `endReview` :1399, `startReview` :1438, `showReviewSummary` :967, `onKey` :2395, stage globals :118-126); `firmware/src/app/ReviewScreens.h` (`ReviewSummaryView` :49-63, `ReviewView` :18-30) and `ReviewScreens.cpp` (`drawReviewScreen` :30, `drawReviewSummaryScreen` :407); new `firmware/src/wk/SubmitPolicy.{h,cpp}`; `firmware/src/platform/DeviceSettings.{h,cpp}`; `firmware/test/test_wk/test_wk.cpp`
- **Settings:** R12 "Submit reviews" — At session end (default) / Every 5 / Immediately; NVS key `rv.submit` (`03-settings.md`).

**Problem / goal.** `Enter` after the last feedback calls `advanceFeedback()` (`App.cpp:1376`), which calls `queueReview()` (`:1146`); that immediately POSTs through `sendOutboxItem()` (`:1035`) with the canvas released (`:1085-1087`). The POST blocks for ~2 s (`docs/hardware.md:63-64`), so the screen freezes after every item and the next prompt waits for the network. The pre-review stage is kept in one pair of globals (`gPendingStageAssignment`/`gPendingStageFrom`, `:124-125`), set in `queueReview()` (`:1147-1148`) and read in `sendOutboxItem()` (`:1102`), so only the last item's move can ever be reported.

**Behaviour.** Three policies, selected in Settings:
- **At session end (default).** Nothing is POSTed while reviewing; FIX-10 puts each completed review in the outbox at completion. When the last item is advanced, the summary opens and a send loop runs: the outbox slot at y=98 shows `Sending 3/12`, a 3 px bar at y=121 fills, and the summary is redrawn after each item so `N items moved up` and the outbox count grow live. `Esc` stops the loop; the rest stays pending and goes out on the next sync. `Enter`/`Esc` after the loop closes the session as today.
- **Every 5 items.** After the 5th, 10th, … completed item the session's pending items are sent before the next prompt; the review hint bar (left, y=125) shows `Sending 2/5`, `Esc` stops the batch and the session continues.
- **Immediately.** Today's behaviour: one POST per completion, same hint.

Offline: the first failed send stops the loop (as `flushOutbox()` does at `:1172-1178`) and the item stays `Pending`; the session continues. 401/403/429 arm the existing gates (`WkClient.cpp:228-246`) and stop the loop. `Ambiguous`/`reconcile` behaviour (`sendOutboxItem` :1039-1056) is unchanged.

**Design.**
- `wk/SubmitPolicy.{h,cpp}`: `enum class SubmitPolicy : uint8_t { AtSessionEnd, EveryFive, Immediately }`; `bool submitAfterItem(policy, completedCount)`; `bool submitAtSessionEnd(policy)`.
- Same file: a bounded `StageTracker` (`record(seq, assignmentId, fromStage)`, `fromStage(assignmentId)`, `forget(assignmentId)`, capacity `kOutboxMax` from `wk/Outbox.h:35`) replacing the two globals. `queueReview()` records; `sendOutboxItem()` uses `fromStage()` (0 when unknown, as today for items queued before a reboot) and forgets on success.
- `sendPendingSession()` in App.cpp: iterate the recorded seqs, call `sendOutboxItem()` one at a time, redraw the visible frame and pump keys exactly like `syncStopRequested()` (`:2665`). Stop on `Esc`, on `wkAuthBlocked()`, on `wkRateLimitWaitSeconds() > 0`, or on the first non-transient failure.
- `queueReview()` keeps the `createdAt` logic (`:1149-1150`) and no longer calls `sendOutboxItem()` directly. `advanceFeedback()` and `checkCurrent()` consult `submitAfterItem()`; the summary entry point consults `submitAtSessionEnd()`.
- `endReview()` gets a `bool send` argument so the sleep path (SPEED-06) can end a session without sending.
- View: `ReviewSummaryView` gets `sending`, `sent`, `sendTotal`; `ReviewView` gets `sending`, `sent`, `sendTotal`. Only view data, no globals in the screens.

**Tests.** Native: `test_submit_policy_values`, `test_submit_policy_every_five_counts`, `test_submit_policy_session_end`, `test_stage_tracker_per_assignment`, `test_stage_tracker_unknown_is_zero`, `test_stage_tracker_bounded`. Hardware: complete 12 items with the default, watch `Sending n/12`, press `Esc` halfway, confirm Home shows the remaining pending count and the next sync sends them.

**Docs.** `ux.md` §5.7 (sending line and hint), §5.4 (review hint), §4.2 (completed → outbox); `api-sync.md` §5.4 (`item_complete`) and §6.2 (when a flush runs); `decisions.md` new dated entry (submissions are deferred by default); `ROADMAP.md`.

**Acceptance.**
- [ ] The setting exists with the three values and defaults to At session end.
- [ ] With the default, `Enter` after an item performs no POST and the item is in the outbox.
- [ ] The summary shows `Sending n/m` with a bar; `Esc` stops it and the rest stays pending.
- [ ] `N items moved up` counts every item sent in the session, not only the last one.
- [ ] Immediately reproduces today's behaviour; `Ambiguous`/reconcile is untouched.
- [ ] `pio test -e native`, `pio run -e cardputer-adv` and `-dev` pass.

### SPEED-02 — Quick and full sync

- **Priority:** P1
- **Size:** M
- **Depends on:** none
- **Touches:** `firmware/src/app/App.cpp` (`setup` :4822, `startSync` :2691, `runProbe` :659, `runSummary` :691, `runLevelProgress` :746, `runSrsCounts` :761, `saveCacheSnapshot` :348, `loadCache` :396); `firmware/src/wk/Cache.{h,cpp}` (`kCacheVersion` :22, `CacheSnapshot` :27-34, encode/decode :494-536); `firmware/src/platform/DeviceSettings.{h,cpp}`; new `firmware/src/wk/SyncPlan.{h,cpp}`; `firmware/test/test_wk/test_wk.cpp`
- **Settings:** Y1 "Auto sync on wake" — Quick (default) / Full / Off, key `sy.wake`; Y2 "Full sync every" — 1 h / 6 h (default) / 24 h, key `sy.full` (`03-settings.md`). Off skips the automatic sync on wake; Settings › Sync now still works.

**Problem / goal.** `setup()` calls `startSync()` on every boot (`App.cpp:4885`), and `startSync()` (`:2691`) joins Wi-Fi, waits up to 5 s for SNTP (`:2726-2728`), flushes the outbox, then fetches `/user`, `/summary`, level progress (3 GETs, `WkLevelProgress.cpp:32`) and five SRS count buckets (`WkSrsCounts.cpp:40`) — about 10 requests, 20-25 s on the modal Sync screen. A wake from deep sleep is a cold boot (`board/Board.h:116`, `BoardCardputerAdv.cpp:254`), so a device that slept for two minutes pays the full cost although its cache is seconds old.

**Behaviour.**
- **Quick** = Wi-Fi, outbox flush, `/summary`; `/user` only on a cold power-on (`!wokeFromSleep()`) or when the last `/user` is older than 6 h.
- **Full** = Quick + level progress + SRS counts.
- A wake from deep sleep runs Quick. Full runs when the last full sync is older than "Full sync every", on a cold power-on with no recent full sync, and always on Settings › Sync now (which keeps the manual screen).
- The automatic sync is skipped entirely when the last successful sync is < 5 min old; Home then shows the cache. This also skips the outbox flush for that window, which is intended.
- Offline or failed requests behave as today; with an untrusted clock the intervals cannot be compared, so no skip is taken and a cold power-on runs Full, a wake runs Quick.

**Design.**
- `wk/SyncPlan.{h,cpp}`: `enum class SyncKind : uint8_t { Skip, Quick, Full }`; `struct SyncTimes { uint32_t now, lastQuick, lastFull, lastUser; }`; `SyncKind planSync(SyncKind requested, bool coldBoot, const SyncTimes&, uint32_t fullEverySeconds)`; `bool quickNeedsUser(const SyncTimes&, bool coldBoot)` (cold boot or `now - lastUser >= 6 h`). Manual always returns Full.
- Snapshot: add `lastFullAt`, `lastQuickAt`, `lastUserAt` to `CacheSnapshot` (`wk/Cache.h:27-34`), encoded after the SRS counts in `encodeCacheSnapshot()` (`Cache.cpp:494-509`) and decoded in `decodeCacheSnapshot()` (`:511-536`). Bump `kCacheVersion` 11 → 12 (`Cache.h:22`); the header's reserved u16 at offset 6 is too small for two epochs, so use the payload. `decodeFile()` (`Cache.cpp:134-138`) accepts v11 and v12; a v11 snapshot defaults all three fields to `meta.savedAt`.
- App: keep `gLastFullAt/gLastQuickAt/gLastUserAt` in RAM, written by `saveCacheSnapshot()` (`:348`) and read by `loadCache()` (`:396`). `runProbe()` (`:659`) sets `gLastUserAt`, `runSummary()` (`:691`) sets `gLastQuickAt`, the last full step sets `gLastFullAt`.
- `startSync(SyncKind kind)` keeps the current body split into `syncQuick()` and the full-only steps; `setup()` computes the plan with `tgc::board::wokeFromSleep()` and calls it only when it is not `Skip`.
- Read Y1/Y2 through the ARCH-02 registry (`settingsGetInt("sy.wake")`, `settingsGetInt("sy.full")`, stored as 3600/21600/86400 seconds, default 21600).

**Tests.** Native: `test_sync_plan_manual_is_full`, `test_sync_plan_wake_quick`, `test_sync_plan_full_when_due`, `test_sync_plan_skip_recent`, `test_sync_plan_untrusted_clock_is_full`, `test_sync_plan_user_due_cold_and_six_hours`, extend `test_cache_snapshot_round_trip` for v12 and add `test_cache_v11_snapshot_loads`. Hardware: boot cold and from deep sleep with the serial log, check the request list and the time to Home.

**Docs.** `api-sync.md` §5.2, §5.3 and §11.4 (version 12, new fields); `ux.md` §5.3 (sync clock) and §5.12 (setting); `decisions.md`; `ROADMAP.md`.

**Acceptance.**
- [ ] A wake from deep sleep with a fresh cache does not run the full sync.
- [ ] `/user` is skipped on a warm quick sync and fetched on a cold power-on or after 6 h.
- [ ] Full sync runs after the interval and on Sync now.
- [ ] A sync less than 5 min old is skipped and Home shows the cache.
- [ ] A v11 cache still loads with the three times defaulted to `savedAt`.
- [ ] Native tests and both builds pass.

### SPEED-03 — Background (non-modal) sync

- **Priority:** P1
- **Size:** L
- **Depends on:** SPEED-02 (same `startSync()` code; one branch: SPEED-02 → SPEED-03 → SPEED-04)
- **Touches:** `firmware/src/app/App.cpp` (`startSync` :2691, `syncScreenKey` :2372, `syncStopRequested` :2665, `finishSyncCancelled` :2677, `resetSyncScreen` :2610, sync row helpers :2624-2662, `onKey` :2395, `loop` :4892, `drawHome` :3109, `buildHomeView` :3051); `firmware/src/app/HomeScreen.{h,cpp}` (`HomeView` :12-31, `drawHomeScreen` :9); `firmware/src/platform/WkLevelProgress.{h,cpp}` (`fetchLevelProgress` :32); `firmware/src/platform/WkSrsCounts.{h,cpp}` (`fetchSrsCounts` :40); new `firmware/src/wk/SyncSteps.{h,cpp}`; `firmware/test/test_wk/test_wk.cpp`
- **Settings:** none (reads the plan from SPEED-02 and `settingsReviewBatchSize()` for SPEED-04).

**Problem / goal.** `startSync()` (`App.cpp:2691`) takes over the screen: `resetSyncScreen()` (`:2610`) draws the six-row Sync screen and every step blocks until its request returns; keys are only pumped by `syncStopRequested()` (`:2665`) and only `Esc` is honoured. A boot with cached data therefore hides Home for the whole 20-25 s sync although the cache could be shown at once. Each step is a blocking function (`runProbe` :659, `runSummary` :691, `runLevelProgress` :746, `runSrsCounts` :761, `flushOutbox` :1163), so nothing can be resumed from `loop()`.

**Behaviour.**
- An automatic sync (boot) keeps Home on screen. The header's `Synced HH:MM` text (x=198, `HomeScreen.cpp:19-25`) is replaced by `sync 2/4` (step/total for the plan) and returns to the clock when done.
- Manual Sync now (`S` on Home `:2517`, Settings `:2202`, Sync-screen retry `:2384`, `portalFinish` :2918, `retryBoot` :3021) keeps today's six-row Sync screen and `Esc` cancel.
- `Enter` during an automatic sync: at the next boundary the remaining optional steps (Account when optional, Level, SRS, prefetch) are skipped and `startReview()` (`:1438`) runs from the cached batch; if no cached batch loads, the sync continues. `Esc` opens Settings (`openSettings(SettingsOrigin::Home)`, `:2094`); the job pauses while another screen owns the display and resumes when Home returns. Other Home keys work as usual.
- Errors: Auth/Forbidden/RateLimit/Server set `gSyncIssue` and appear through the existing Home priority (`buildHomeView` :3067-3105); a network failure sets `gOffline = true` and shows the existing cache line. The job stops; data already fetched is kept.

**Design.**
- `wk/SyncSteps.{h,cpp}`: `enum class SyncStep : uint8_t { Idle, Wifi, Time, Outbox, Account, Summary, Level, Srs, Prefetch, Done, Failed }`; `bool stepIsOptional(SyncStep)`; `uint8_t stepCount(SyncKind, bool needsAccount)`; `SyncStep nextStep(SyncKind, SyncStep current, bool interrupted)`.
- App: `struct SyncJob { SyncKind kind; SyncStep step; uint8_t sub; bool modal; bool interrupted; uint32_t startedAt; }` and `syncAdvance()` called from `loop()` (`:4892`) after key handling, before the draw. Each call performs **one request** and returns; a coarse step with several requests keeps a sub-index: `Outbox` sends one item per advance, `Level` sub 0 = `/level_progressions`, sub 1-2 = the two counts, `Srs` sub 0-4 = one bucket each.
- Split `fetchLevelProgress()` (`WkLevelProgress.cpp:32`) into `fetchLevelProgressions()` and `fetchLevelCounts()`; split `fetchSrsCounts()` (`WkSrsCounts.cpp:40`) into a per-bucket call. Keep the old wrappers so the other callers (`App.cpp:748`, `:763`) do not change.
- `Wifi` calls `DevWifi::connect(30000)` (the one unavoidable block, up to 30 s); `Time` is non-blocking: each `loop()` checks `time(nullptr) >= 1700000000` and gives up after 5 s.
- `modal = (kind == Manual)` reuses the existing row helpers (`syncRowBegin/Done/Warn/Fail`, `:2629-2662`) and `drawSync()`; non-modal updates `gSyncJob` only and sets `gDirty`, so `drawHome()` (`:3109`) renders the indicator. The job does not draw when `gSettingsActive`, `gPauseActive`, `gOutboxScreen`, `gFirmwareActive` or a review is active.
- `HomeView` gets `uint8_t syncStep, syncTotal`; `drawHomeScreen()` draws `sync n/m` in place of the sync clock while `syncTotal > 0`.
- Enter handling in `onKey()` (`:2395`): if a non-modal job is running, set `interrupted` and a `startReviewPending` flag instead of calling `startReview()` immediately; the flag is consumed at the next boundary. Reuse `loadCachedBatch()` (`:443`) to check a batch exists.

**Tests.** Native: `test_sync_steps_quick_count`, `test_sync_steps_full_count`, `test_sync_steps_optional_flags`, `test_sync_steps_interrupt_skips_optional`, `test_sync_steps_error_is_terminal`. Hardware: boot with Wi-Fi on, confirm Home appears at once with `sync 1/4`, that `Esc` opens Settings mid-sync, and that `Enter` starts a cached review before the sync ends.

**Docs.** `ux.md` §5.3 (header indicator), §5.11 (automatic vs manual), §3.3 (keys during a sync); `api-sync.md` §5.3 (step machine); `decisions.md`; `ROADMAP.md`.

**Acceptance.**
- [ ] Boot shows Home, not the Sync screen, and the header shows `sync n/m`.
- [ ] Keys are handled between requests; `Enter` starts a cached review, `Esc` opens Settings.
- [ ] Manual Sync now still shows the six-row screen with `Esc` cancel.
- [ ] A failure stops the job and appears on Home under the existing priority rules.
- [ ] Nothing is drawn over Settings, Pause, Outbox, Firmware or a review.
- [ ] Native tests and both builds pass.

### SPEED-04 — Prefetch the review batch

- **Priority:** P2
- **Size:** M
- **Depends on:** SPEED-02, SPEED-03
- **Touches:** `firmware/src/app/App.cpp` (`runBatch` :772, `startReview` :1438, `lowBatteryNow` :1426, `loadCachedBatch` :443, `evictCachedBatch` :491, `gCachedBatchCount` :103, `saveCacheAll` :391); `firmware/src/wk/SyncPlan.{h,cpp}` (predicate, from SPEED-02); `firmware/test/test_wk/test_wk.cpp`
- **Settings:** Y3 "Prefetch reviews" — On (default) / Off, key `sy.prefetch`; reads R1 batch size (`settingsReviewBatchSize()`, `DeviceSettings.cpp:150`).

**Problem / goal.** Without a cached batch, `Enter` on Home calls `startReview()` (`App.cpp:1438`), which draws `Fetching reviews...` (`:1459`) and runs `runBatch()` (`:772`). `fetchReviewBatch()` (`platform/WkReview.cpp:26`) fetches the assignments, the subjects in pages of five (`:67-91`), study materials (`:93`), review statistics (`:94`) and the component subjects (`:109-122`): 8-10 requests at ~2 s each. The user waits ~20 s after pressing `Enter`, even though the summary already said how many items are due.

**Behaviour.** As the last optional step of an automatic full sync, when `gDueReviews > 0` and the cached batch is empty or smaller than the batch size (`gCachedBatchCount < settingsReviewBatchSize()`), the batch is fetched. Skipped when `lowBatteryNow()` (`:1426`, < 20 % on battery). `Enter` afterwards starts the session from the cache with no `Fetching reviews...` screen. If the prefetch failed or was skipped, `Enter` behaves as today.

**Design.**
- Predicate in `wk/SyncPlan.{h,cpp}`: `bool shouldPrefetch(uint32_t due, size_t cachedCount, size_t batchSize, bool lowBattery)`.
- `SyncStep::Prefetch` after `Srs`; it calls `runBatch()` once. No loading screen: Home stays and the indicator keeps the last step.
- `runBatch()` (`:772`) currently clears `gBatch` on failure (`:787`); change it to clear only when the batch was empty before the call, so a failed prefetch cannot throw away a good cached batch.
- After a successful prefetch, call `evictCachedBatch(true)` (`:491`): the assignments and subjects are on disk (`saveCacheAll()` at `:831`), and `startReview()` reloads them through `loadCachedBatch()` (`:1454`) with no network. This keeps the ~20 subjects out of RAM on Home, as today.
- Battery and due count are read at the step boundary, after `/summary`.

**Tests.** Native: `test_prefetch_when_empty`, `test_prefetch_when_smaller`, `test_prefetch_skips_full_batch`, `test_prefetch_skips_low_battery`, `test_prefetch_skips_no_due`. Hardware: full sync with due reviews, then `Enter`; compare the time to the first prompt with and without the prefetch and log the heap after the sync.

**Docs.** `api-sync.md` §5.3 (last step) and §5.4 (batch from cache); `ux.md` §4.2; `ROADMAP.md`.

**Acceptance.**
- [ ] A full sync with due reviews and an empty or short batch fills the cache.
- [ ] `Enter` then shows the prompt with no `Fetching reviews...` screen.
- [ ] Prefetch is skipped below 20 % on battery and when the cached batch is full.
- [ ] A failed prefetch keeps the previous cache; the batch is not cleared.
- [ ] Heap after the sync is back to the pre-sync level (batch evicted).
- [ ] Native tests and both builds pass.

### SPEED-05 — TLS keep-alive (spike, then implement)

- **Priority:** P2
- **Size:** L
- **Depends on:** none
- **Touches:** `firmware/src/platform/WkClient.{h,cpp}` (`request` :82-250, `wkGet` :253, `wkPost` :258, `wkPut` :263); `firmware/src/app/App.cpp` (`requestBefore/requestAfter` :551-556, `runBatch` :772, `flushOutbox` :1163, sync steps :2691); new `firmware/src/wk/HttpChunked.{h,cpp}`; `firmware/test/test_wk/test_wk.cpp`; `docs/hardware.md`
- **Settings:** none.

**Problem / goal.** `request()` (`platform/WkClient.cpp:82`) constructs a `WiFiClientSecure` per call (`:112`), sends `HTTP/1.0` with `Connection: close` (`:131-138`) and stops the client (`:247`). Every request pays a new TLS handshake: `/user` 2.0 s and `/summary` 2.1 s on hardware (`docs/hardware.md:63-64`), so a 10-request sync spends most of its time in handshakes. HTTP/1.0 was chosen to avoid chunked responses (`docs/hardware.md:67-68`), which is also what blocks keep-alive.

**Behaviour.** No user-visible change except shorter waits; same endpoints, headers and error handling. One TLS connection serves a sequence (a sync, a batch fetch, an outbox flush). Any connection error falls back to a fresh connection once. Progress is drawn straight to the panel while the canvas is released.

**Design.**
- **Spike first.** A temporary dev build (or a dev serial command) runs 10 GETs with and without keep-alive and prints per-request ms (`[http] ... -> code, bytes, ms`, `WkClient.cpp:225-227`), total ms and `logHeap()` (`:49-55`) after each. Record in `docs/hardware.md` §Wi-Fi and API: per-request ms, total, minimum free heap, whether WaniKani answers chunked. No production code in the spike commit.
- `WkSession`: `bool wkSessionBegin()` / `void wkSessionEnd()` / `bool wkSessionActive()` in `WkClient.{h,cpp}`. When active, `request()` reuses one `WiFiClientSecure` created by `wkSessionBegin()` instead of constructing one; `wkSessionEnd()` stops it. Request line `HTTP/1.1`, `Connection: keep-alive`, `Content-Length` on POST/PUT (`:139-144`).
- Framing: read status and headers; on `Transfer-Encoding: chunked` read until the zero chunk plus trailers, then decode **in place** in `gBodyBuffer` (output never exceeds input, so no second 32 KB buffer); otherwise use `Content-Length`; a response without either closes the connection and reads until close. Never read until timeout on a reusable connection.
- Reuse rule: keep the connection for a complete 2xx/4xx body; close on `Connection: close`, a truncated body, a read timeout, or a 429.
- `wk/HttpChunked.{h,cpp}`: pure `bool chunkedDecodeInPlace(char* data, size_t len, size_t cap, size_t* outLen)`; hex sizes, `;ext`, CRLF, zero chunk with trailers, malformed and truncated input.
- Heap: `requestAfter()` (`App.cpp:554`) restores the canvas only when `!wkSessionActive()`; while a session is open, `target()` falls back to `gDisp` (`:515-518`) and `ensureCanvas()` (`:523-539`) is not called. Restore once with `ensureCanvas()` at `wkSessionEnd()`. Sessions wrap `runSummary` + the full-sync steps, `fetchReviewBatch()` (`WkReview.cpp:26`) and `flushOutbox()` (`:1163`).
- Fallback: a reused connection that reports `connected == false` is closed, reopened with `wkSessionBegin()` and the request retried once; a second failure is reported as today.

**Tests.** Native: `test_chunked_single_chunk`, `test_chunked_multiple_chunks`, `test_chunked_extension_and_trailer`, `test_chunked_truncated_is_incomplete`, `test_chunked_bad_size_is_error`, `test_chunked_respects_cap`, `test_chunked_in_place_output`. Hardware: the spike numbers, then a 10-request sync and a batch fetch with one handshake in the log.

**Docs.** `docs/hardware.md` §Wi-Fi and API (measurements; replace the HTTP/1.0 note); `api-sync.md` §2 and §5.3 (transport); `decisions.md` (keep-alive + chunked); `ROADMAP.md`.

**Acceptance.**
- [ ] The spike numbers are recorded in `docs/hardware.md`.
- [ ] A sync uses one TLS connection (one handshake in the log).
- [ ] A chunked response parses correctly; the decoder is native-tested.
- [ ] A 10-request sync takes ≤ 50 % of today's time; minimum free heap ≥ 20 KB.
- [ ] A dropped reused connection retries once on a new one, then reports the error.
- [ ] Native tests and both builds pass.

### SPEED-06 — Light sleep before deep sleep (spike first)

- **Priority:** P2
- **Size:** M
- **Depends on:** SPEED-01 (`endReview(bool send)`); the spike can start earlier
- **Touches:** `firmware/src/board/Board.h` (power section :113-137) and `firmware/src/board/BoardCardputerAdv.cpp` (`kSelectPin` :38, `kKbdIntPin` :39, `setDisplayOn` :262, `deepSleep` :272-309); `firmware/src/app/App.cpp` (`loop` :4892, blank branch :4975-4978, deep-sleep condition :4979-4983, `wakeScreen` :4263, `endReview` :1399, `saveCacheSnapshot` :348); `firmware/src/platform/DeviceSettings.{h,cpp}`; new `firmware/src/wk/SleepPolicy.{h,cpp}`; `firmware/test/test_wk/test_wk.cpp`
- **Settings:** P5 "Light sleep" — On (default) / Off, key `dp.lightsleep`; P6 "Sleep during review" — Never (default) / After 10 min, key `dp.revsleep` (`03-settings.md`).

**Problem / goal.** After "Blank after" (default 60 s, `platform/DeviceSettings.cpp:26`) the panel goes off but the CPU, Wi-Fi and RAM stay up (`App.cpp:4975-4978`); the only low-power state is deep sleep after "Sleep after" (default 120 s, `DeviceSettings.cpp:27`). Deep sleep is a cold boot: `setup()` runs again (`App.cpp:4822`), the cache is reloaded and a sync starts (`:4885`), so a pause cannot resume the same screen. During a review the deep-sleep condition excludes every review state (`:4979-4983`), so a forgotten device stays fully awake.

**Behaviour.**
- After "Blank after": panel off, Wi-Fi off, light sleep with the TCA8418 INT (G11) and G0 as wake sources. Any key wakes in well under a second and the same screen returns, mid-review included, with no boot and no sync.
- Deep sleep still happens after "Sleep after". If nobody presses a key, a timer wake lets the device check the deadline and deep-sleep.
- Review: "Sleep during review" Never (default) = no deep sleep while a review is open, as today; After 10 min = 10 min without a key ends the session cleanly (completed items are already in the outbox per FIX-10, a half-typed item is dropped and nothing is sent), saves the cache and deep-sleeps.
- USB power: no sleep (`externalPower()`, `:4983`).

**Design.**
- **Spike first.** A dev build that enters light sleep on demand, prints the wake cause, and is measured with an inline USB meter: confirm the TCA8418 INT wakes the chip, G0 wakes it, the first key is delivered, and record current draw and wake latency. Add a power section to `docs/hardware.md` (verify the section name).
- `board::lightSleep(uint32_t maxMs)` in `Board.h` (after `deepSleep`, :125) and `BoardCardputerAdv.cpp` (after :272): blank with `setDisplayOn(false)`, `WiFi.disconnect(true); WiFi.mode(WIFI_OFF)`, wait for all keys up and INT high as `deepSleep()` does (:280-290), `gpio_wakeup_enable(kKbdIntPin, GPIO_INTR_LOW_LEVEL)`, `gpio_wakeup_enable(kSelectPin, GPIO_INTR_LOW_LEVEL)`, `esp_sleep_enable_gpio_wakeup()`, `esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON)`, pull-ups on both, and a timer wake after `maxMs` when it is non-zero. On return, `setDisplayOn(true)` and return whether a key or the timer woke it.
- `wokeFromSleep()` (:254) stays EXT1-only: a light-sleep wake is not a boot.
- App: at `blankMs` call `board::lightSleep(slice)` where `slice` is the time left to `sleepMs` (0 when `sleepMs == 0`). After a key wake, `wakeScreen()` (:4263) restores the panel and sets `gDirty`; after a timer wake, fall through to the sleep check. Recompute the deadline from `gLastInputAt` as today.
- `wk/SleepPolicy.{h,cpp}`: `enum class SleepDuringReview : uint8_t { Never, After10Min }`; `bool reviewMayDeepSleep(policy, idleMs)`; `uint32_t lightSleepSliceMs(idleMs, blankMs, sleepMs)`.
- Deep-sleep condition: add the review rule; when it fires during a review, call `endReview()` (`:1399`) with `send = false` (SPEED-01), then `saveCacheSnapshot()`, `firmwareBeforeSleep()`, `deepSleep()`.

**Tests.** Native: `test_sleep_policy_never_during_review`, `test_sleep_policy_after_ten_minutes`, `test_sleep_policy_idle_at_home`, `test_light_sleep_slice_to_deep_sleep` (blank 60, sleep 120, idle 60 → 60 s; sleep 0 → 0 = no timer). Hardware: wake from a key mid-review, current draw, G0 wake, first key delivered, no reboot.

**Docs.** `ux.md` §5.3/§5.4 (sleep and wake) and §5.12 (setting); `docs/hardware.md` power section; `decisions.md` new dated entry amending "Idle dim, blank, then deep sleep" (2026-10-25); `ROADMAP.md`.

**Acceptance.**
- [ ] A key after blank returns to the same screen without a boot, mid-review included.
- [ ] Light sleep turns Wi-Fi off; current draw and wake latency are recorded.
- [ ] Deep sleep still happens after "Sleep after"; a timer wake checks the deadline.
- [ ] "After 10 min" ends the session, sends nothing from the half-typed item and deep-sleeps.
- [ ] "Never" keeps today's behaviour.
- [ ] Native tests and both builds pass.

### SPEED-07 — Home updates over time

- **Priority:** P2
- **Size:** S
- **Depends on:** none
- **Touches:** `firmware/src/app/App.cpp` (`loop` :4892, `applySummary` :288, `effectiveNow` :280-286, `buildHomeView` :3051, `drawHome` :3109); `firmware/src/app/HomeScreen.cpp` (`drawHomeScreen` :9); `firmware/test/test_wk/test_wk.cpp`
- **Settings:** none.

**Problem / goal.** Home is redrawn only when `gDirty` is set (`loop()`, `App.cpp:4940-4944`). The due count and `Next review` come from the cached `/summary` and are recomputed only by `applySummary()` (`:288`) during a sync or a review, so a review that becomes due at 14:00 stays invisible on an idle Home until the next sync. The battery percentage is read in `buildHomeView()` (`:3053`), so it is only as fresh as the last redraw.

**Behaviour.** Once a minute on Home, the due count and `Next review` are recomputed from the cached hourly buckets and the battery percentage is re-read. Works offline with a trusted clock. Nothing else changes: level, lessons, SRS bar and sync clock stay as cached. A newly due item is counted but is not in the cached batch; `Enter` fetches it (SPEED-04 may already have filled the batch). No network request.

**Design.**
- `loop()`: `if (gShowHome && now - gLastHomeTickMs >= 60000) { gLastHomeTickMs = now; applySummary(effectiveNow()); gDirty = true; }`, skipped while a review, the review summary, a modal, the portal, the asset server or a manual sync owns the screen.
- Battery needs no new state: `buildHomeView()` (`:3053`) reads it on every draw, so the forced redraw refreshes it.
- `effectiveNow()` (`:280-286`) is pinned to `gCacheSavedAt` when the clock is untrusted, so nothing advances offline after a reboot. Make it advance with uptime: `gCacheSavedAt + (millis() - gBootMs) / 1000`, with `gBootMs` captured in `setup()` after `loadCache()`; SNTP corrects it on the next sync. Drift on a long uptime (verify).
- `nextReviewAt()` (`wk/Summary.h:28`) already returns the earliest future bucket, and `applySummary()` turns a passed bucket into `now` when items are due (`:291-297`); no new logic there.

**Tests.** Native: `test_summary_counts_advance_with_time` (buckets at `t` and `t+3600`: `dueReviews(t)`, `dueReviews(t+3600)`, `nextReviewAt(t) == t+3600`), `test_summary_next_review_after_last_bucket`. Hardware: leave Home open across a due time and confirm the counter changes within a minute.

**Docs.** `ux.md` §5.3 (Home updates without a sync); `api-sync.md` §5.5; `ROADMAP.md`.

**Acceptance.**
- [ ] A bucket that becomes due is counted within a minute without network.
- [ ] `Next review` moves to the next bucket when the current one passes.
- [ ] The battery percentage refreshes without input.
- [ ] Nothing is redrawn while Settings, Pause, Outbox or a review is open.
- [ ] Native tests and both builds pass.

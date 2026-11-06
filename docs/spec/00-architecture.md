# Architecture prerequisites

These four tasks are wave 1 of round 2: they do not add features, they make the codebase safe for parallel subagents and give CFG, SPEED, REV, LES, STUDY, STATS, REM and AUD their shared building blocks. ARCH-01 must land first because every other task in the round edits a file that today only exists inside `app/App.cpp`; ARCH-02, ARCH-03 and ARCH-04 then run in parallel on disjoint files. ARCH-01 and ARCH-04 change no user-visible behaviour; ARCH-02 changes no menu row; ARCH-03 adds two local files that no screen reads yet.

| ID | Title | Priority | Size | Depends on | Wave | Status |
|---|---|---|---|---|---|---|
| ARCH-01 | Split `app/App.cpp` into controllers | P1 | L | none | 1 (first) | todo |
| ARCH-02 | Settings registry | P1 | M | ARCH-01 | 1 (after ARCH-01; ∥ ARCH-03, ARCH-04) | todo |
| ARCH-03 | Local stores shared by STATS, STUDY and REM | P1 | M | ARCH-01 | 1 (after ARCH-01; ∥ ARCH-02, ARCH-04) | done (2026-11-06) |
| ARCH-04 | Screen state machine | P2 | M | ARCH-01 | 1 (after ARCH-01; before wave 2 starts) | todo |

### ARCH-01 — Split `app/App.cpp` into controllers

- **Priority:** P1
- **Size:** L (mechanical moves; ~4,300 lines relocated, ~250 lines of new headers)
- **Depends on:** none
- **Touches:** `firmware/src/app/App.cpp` (4,999 lines, everything except the parts listed below), new `firmware/src/app/{AppState,AppUi,CacheController,SyncController,OutboxController,ReviewController,LessonController,SettingsController,SetupController,PowerController,DevConsole}.{h,cpp}`, new `firmware/src/wk/SubjectText.h`; `firmware/src/app/App.h` and `firmware/src/main.cpp` stay as they are; no `platformio.ini` change (`src/` is compiled recursively)
- **Settings:** none

**Problem / goal.** `App.cpp` is 4,999 lines with about 120 file-static variables in lines 66–226; the cache, sync, outbox, review, lesson, settings, setup, power and dev-console flows plus both routers live in one translation unit. Wave 2's four tasks (CFG, SPEED, REV, LES) would all edit this file, so parallel agents conflict on every hunk and every merge. There is no seam to move behind either: `startReview()` calls `flushOutbox()` and `evictCachedBatch()`, `endReview()` calls `queueReview()` and `evictCachedBatch()`, and the key handlers reach into every other flow's flags.

**Behaviour.** No user-visible change. `draw()` (4166–4248) keeps its exact priority list and `onKey()` (2395–2541) keeps its exact dispatch order, including the asymmetries between them (sign-out and About are drawn before Outbox/Firmware/Settings, but are keyed inside `settingsKey()`). Boot, sync, review, feedback, detail, lessons, pause, settings, outbox, firmware, setup, sleep/wake and the serial console behave as today, with the same `Serial` lines.

**Design.**
- `app/AppState.h/.cpp`: one `AppState` instance behind `AppState& state()`, grouping today's globals into `DisplayState` (66–70), `StorageState` (`gSdOk`, `gSdLine` 72–73, `gPack` 143), `SessionState` (`gUser`…`gSyncClock` 80–95, `gLevelUp*` 83–86), `CacheState` (`gBatch`, `gBatchEvicted`, `gCachedBatchCount`, `gCacheSavedAt`, `gOffline` 97–103, 155), `ReviewState` (104–129, 113–128), `OutboxState` (134, 215–216), `LessonState` (135–142, 146–151), `SyncState` (152–183), `UiState` (189–198, 217–221), `OtaState` (199–214), `SetupState` (185–187), `DevState` (222–226). Keep field names; no accessors except where an invariant must be enforced.
- `app/AppUi.h/.cpp` (shared primitives, not a controller): `target()` 515–518, `present()` 519–521, `ensureCanvas()` 523–539, `releaseCanvas()` 541–545, `gFreeCanvasForRequest` + `requestBefore/After` 550–556, modifiers 558–562 and `updateModifier()` 590–599 (expose `shift()`), `keyName()` 569–583, `pushLog()` 585–588, `drawLoading()` 2598–2608, and `gDisp`/`gCanvas`/`gDirty`/`gLastDrawMs`/`gLastStatusMs`.
- `app/CacheController`: `clearLevelUpNotice` 266–271, `currentAccountKey` 273–276, `effectiveNow` 280–286, `applySummary` 288–298, `removeSubjectFromReviews` 306–313, `pruneCacheOutbox` 317–330, `pruneBatchToSummary` 334–346, `saveCacheSnapshot` 348–376, `saveSubjectCache` 378–389, `saveCacheAll` 391–394, `loadCache` 396–438, `loadCachedBatch` 443–486, `evictCachedBatch` 491–499, `markCacheCompleted` 501–513, `mountSd` 2543–2574, `cacheAgeSeconds/cacheIsStale/cacheStatusLine` 3027–3049.
- `app/SyncController`: `setSyncIssue` 601–638, `applyDeferredAuthIssue` 640–649, `clearOutboxAuthIssue` 651–657, `runProbe` 659–689, `runSummary` 691–721, `formatLevelProgressLine` 723–744, `runLevelProgress` 746–759, `runSrsCounts` 761–770, `runBatch` 772–834, `syncScreenKey` 2372–2393, `resetSyncScreen` 2610–2622, `syncRowText/Begin/Done/Warn/Fail` 2624–2662, `syncStopRequested` 2665–2675, `finishSyncCancelled` 2677–2689, `startSync` 2691–2851, `drawSync` 3895–3921.
- `app/OutboxController`: `outboxHasAssignment` 300–304, `saveOutbox` 997–999, `outboxNeedsAttention` 1001–1003, `reconcileOutboxItem` 1005–1033, `sendOutboxItem` 1035–1144, `queueReview` 1146–1161, `flushOutbox` 1163–1184, `attentionOutboxItems` 1186–1191, `selectedOutboxItem` 1193–1203, `outboxItemLabel` 1205–1226, `outboxReasonShort/AgeLabel/Detail` 1228–1273, `moveOutboxSelection` 1275–1286, `openOutbox` 1288–1296, `discardSelectedOutboxItem` 1298–1346, `outboxKey` 2330–2370, `drawOutbox` 3181–3220.
- `app/ReviewController`: `subjectById` 836–840, `correctAnswer` 842–853, `clearReviewInput` 855–861, `reviewInput` 865–871, `showReview` 873–906, `hasRelatedSubjects` 908–910, `detailPageCount` 912–914, `detailPageKind` 916–925, `showReviewDetail` 927–948, `reviewDetailKey` 950–965, `showReviewSummary` 967–995, `checkCurrent` 1348–1374, `advanceFeedback` 1376–1397, `endReview` 1399–1424, `startReview` 1438–1520, `reviewKey` 1522–1576. Export `applyReviewUpdate(const kk::ReviewUpdate&, uint32_t pendingAssignment)` for the stage-change block that `sendOutboxItem` runs at 1096–1111.
- `app/LessonController`: `lessonSubjectById` 1578–1582, `lessonComponentById` 1584–1588, `currentLessonSubject` 1590–1593, `wrapLines` 1595–1619, `showLessonList` 1628–1646, `showLessonEmpty` 1648–1658, `showLesson` 1660–1725, `clearLessonQuizInput` 1727–1733, `lessonQuizInput` 1735–1741, `startLessonQuiz` 1743–1752, `checkLessonQuiz` 1754–1765, `showLessonQuiz` 1767–1802, `lessonQuizKey` 1804–1859, `releaseLessonData` 1861–1866, `startLessons` 1868–1974, `startCurrentLesson` 1976–2006, `lessonListKey` 2008–2039, `lessonKey` 2041–2072.
- `app/SettingsController`: `lowBatteryNow` 1426–1429, `requestLowBatteryConfirm` 1431–1436, `kSettingsRows` 2074, `setSettingsNote` 2076–2079, `openSettings` 2081–2092, `closeSettings` 2094–2106, `openPause` 2108–2117, `nextPreset` 2119–2125, `secondsLabel` 2127–2131, `settingsAction` 2133–2223, `settingsKey` 2225–2301, `pauseKey` 2303–2328, `drawPause` 3124–3138, `drawSettings` 3140–3179, `drawLowBattery` 3222–3234, `drawSignOutConfirm` 3236–3243, firmware UI `formatBytes`…`firmwareKey` 3247–3702, `drawAbout` 3704–3755.
- `app/SetupController`: `portalValidateToken` 2853–2890, `portalRecheckPack` 2892–2896, `portalPackStatus` 2898, `portalSwitchToStation` 2900–2916, `portalFinish` 2918–2925, `startSetupPortal` 2927–2981, `retryBoot` 2983–3025, `drawSetupPortal` 3923–3944.
- `app/PowerController`: `drawStatus` 4250–4261, `wakeScreen` 4263–4270, the screen-off wake block 4900–4914 and dim wake 4916–4920 as `powerWakeCheck(now)`, the dim/blank/sleep tail 4968–4988 as `powerTick(now)` (returns true when the loop must `return`).
- `app/DevConsole`: `showDebug` 3947–3970, `showRadicalTest` 3972–3982, `seedDevCache` 3986–4085, `clearDevCache` 4087–4104, `seedDevRejectedOutbox` 4109–4163, `dumpCanvas` 4272–4304, `assetRespond` 4306–4312, `stopAssetServer` 4314–4318, `serviceAssetServer` 4320–4405, `startAssetServer` 4407–4423, `uploadSdFile` 4425–4505, `handleSerial` 4507–4818. Keep today's `#ifdef KANIKEY_DEV` guards exactly: `dumpCanvas`, the asset server, `uploadSdFile` and the `up`/`p`/`h`/`i`/`S`/`r` commands must still compile in the release image.
- Move `subjectMainMeaning` (1621–1626) and `correctAnswer` (842–853) to `wk/SubjectText.h` (pure, inline, includes `wk/Types.h` and `wk/ReviewQueue.h`) because `LessonController` and `OutboxController::outboxItemLabel` both use them.
- `App.cpp` keeps only: includes, `setup()` 4822–4890, `loop()` 4892–4997, `draw()` 4166–4248, `onKey()` 2395–2541, `buildHomeView()` 3051–3107, `drawHome()` 3109–3122, `bootState()` 3772–3785, `drawBoot()` 3787–3893.
- Controller headers declare only the entry points `App.cpp` and other controllers call (e.g. `ReviewController.h`: `startReview`, `reviewKey`, `drawReview`, `drawReviewDetail`, `drawReviewSummary`, `endReview`, `applyReviewUpdate`); everything else stays `static` in the `.cpp`. Named cross-controller calls: Review→Outbox (`queueReview`), Review→Cache (`markCacheCompleted`, `evictCachedBatch`), Outbox→Cache (`markCacheCompleted`), Outbox→Review (`applyReviewUpdate`), Sync→Outbox (`flushOutbox`), Sync→Cache (`saveCacheSnapshot`), Settings→Setup (`startSetupPortal`), Settings→Sync (`startSync`), Setup→Sync/Cache (`startSync`, `loadCache`, `mountSd`), DevConsole→all.
- One commit per module in the order above, each compiling both environments, so a bad move is bisectable.

**Tests.** No new native tests: the suite is 77 tests before and after, and `test_wk.cpp` is unchanged. Hardware smoke on both images: boot to Home, manual sync, one review with feedback and item detail, one lesson list → card → quiz, settings open/close, pause → resume, sleep then wake, dev image only: Tab debug screen, `p`, `o`, `v`, `C`/`X` seeds.

**Docs.** `docs/design.md` §9.1 (replace the "`app/App.cpp` holds the application and session state…" sentence with the controller table), `docs/decisions.md` (new dated entry: controller split, shared `AppState`), `ROADMAP.md` (task row).

**Acceptance.**
- [ ] `app/App.cpp` is under 800 lines and contains only `setup`, `loop`, `draw`, `onKey` and the Home/Boot adapters.
- [ ] Every module in the Design list exists with the listed functions; no global is declared twice.
- [ ] `draw()` and `onKey()` bodies are unchanged apart from the calls into controllers (diff review).
- [ ] `pio run -e cardputer-adv` and `-e cardputer-adv-dev` build; release flash is within ±2 KB of `main`.
- [ ] `pio test -e native` passes with `test_wk.cpp` untouched.
- [ ] The hardware smoke list passes on both images.

### ARCH-02 — Settings registry

- **Priority:** P1
- **Size:** M
- **Depends on:** ARCH-01
- **Touches:** new `firmware/src/wk/SettingsModel.h/.cpp` and `firmware/src/wk/SettingsRegistry.cpp`, `firmware/src/platform/DeviceSettings.h/.cpp` (getters 114–173, `DisplaySettings` 23–54, `saveInt` 56–62), `firmware/test/test_wk/test_wk.cpp`; `App.cpp`/`SettingsController` `settingsAction()` (App.cpp:2133–2223) and `drawSettings()` (App.cpp:3140–3179) are **not** touched in this task
- **Settings:** the registry that CFG-01/CFG-02 build the menu from; every existing setting is listed below

**Problem / goal.** Every setting is a hand-written getter/setter pair in `platform/DeviceSettings.cpp` (114–173) plus a hard-coded row in `settingsAction()` — a 13-case index switch at `App.cpp:2133–2223` — plus a hard-coded label/value pair in `drawSettings()` (`App.cpp:3144–3146` and `3147–3161`). Adding one setting means editing three files in three different shapes, and CFG-02 adds about 25 of them. There is no single place that knows a setting's NVS key, default, allowed values or help text, so nothing can validate a stored value and nothing can be tested without hardware.

**Behaviour.** No visible change: the menu still has the same 13 rows in the same order and the same values. The seven numeric/toggle settings keep their current NVS keys, defaults and ranges, so a device with `bright=192`, `sleep=600` or `order=0` in NVS reads the same value after the update. A stored value that is not in a setting's choice list falls back to the default (only reachable if NVS was edited by hand).

**Design.**
- `wk/SettingsModel.h/.cpp` (pure, no Arduino, natively compiled; the tables live in `wk/SettingsRegistry.cpp`, which CFG-02 extends): `enum class SettingKind { Choice, Toggle, Action, Submenu, Info }`; `struct SettingChoice { const char* label; int value; }`; `struct SettingDef { const char* id; const char* section; const char* label; const char* help; SettingKind kind; const char* nvsKey; const SettingChoice* choices; uint8_t choiceCount; int defaultValue; bool live; }`.
- Functions: `settingsAll(size_t*)`, `settingById(const char*)`, `settingByNvsKey(const char*)`, `settingNextValue(const SettingDef&, int current)`, `settingPrevValue(...)` (both wrap, unknown current → default), `settingValidate(const SettingDef&, int stored)` (not a choice → default), `settingValueLabel(const SettingDef&, int, char* buf, size_t)` (`off`/`15s`/`2m`/`128`), `settingsInSection(const char* section, const SettingDef** out, size_t max)`, and `SettingsCache` (fixed array of `{const char* nvsKey, int value}`, no allocation: `load`, `get`, `set`).
- Registry rows, same order as today's menu (`App.cpp:3144–3146`):

| id | section (CFG-02) | kind | nvsKey | default | choices |
|---|---|---|---|---|---|
| `P1` Brightness | Display & power | Choice | `bright` | 128 | 64, 128, 192, 255 |
| `P2` Dim after | Display & power | Choice | `dim` | 30 | 0, 15, 30, 60 s |
| `P3` Blank after | Display & power | Choice | `blank` | 60 | 0, 30, 60, 120 s |
| `P4` Sleep after | Display & power | Choice | `sleep` | 120 | 0, 120, 300, 600 s |
| `R1` Batch size | Reviews | Choice | `revsize` | 20 | 10, 20, 30 |
| `R4` First question | Reviews | Toggle | `order` | 1 | 0 Reading first, 1 Meaning first (CFG-02 migrates it to `rv.first`) |
| `L1` Batch size | Lessons | Choice | `lessonsz` | 5 | 3, 5, 10 |
| `setup.portal` | Account | Action | — | — | opens the setup portal |
| `sync.now` | Sync & data | Action | — | — | runs a sync |
| `system.firmware` | System | Submenu | — | — | firmware screen |
| `sync.clearCache` | Sync & data | Action | — | — | clears snapshot and subjects |
| `system.about` | System | Info | — | — | About screen |
| `account.signOut` | Account | Action | — | — | clears token, cache, outbox |

IDs and section names are the ones CFG-02 uses, so CFG-02 only adds rows. The rows above keep today's menu order; CFG-01 renders them by section.

- Non-menu keys stay in `DeviceSettings.cpp` and are listed here for the record: `tzrule` (string, default `CET-1CEST,M3.5.0,M10.5.0/3`, ≤63 chars, POSIX charset check at 64–72) and `tzname` (string, ≤47, display only); both are written only by the setup portal.
- `platform/DeviceSettings`: add `bool settingsGetInt(const char* nvsKey, int* out)` and `bool settingsSetInt(const char* nvsKey, int value)`. `DisplaySettings` (23–54) becomes a `SettingsCache` filled once at boot from `Preferences` (`kanikey` namespace, lines 12–21) and validated with `settingValidate`; the existing getters (114–173) become one-line wrappers so no caller changes. `settingsSetInt` writes only when the value differs, keeping one NVS write per user change.
- Help text budget: ≤38 characters (one Font0 line at 228 px, drawn by CFG-01 at `y=123`); add a test so CFG-02 cannot exceed it.
- Do not touch `settingsAction()`/`drawSettings()`; the registry is used only by the getters and the tests until CFG-01.

**Tests.** `test_settings_registry_ids` (unique ids, non-empty section/label, `nvsKey` ≤15 chars, Action/Info have no key); `test_settings_registry_defaults` (128/30/60/120/20/1/5, matching `DeviceSettings.cpp:24–31`); `test_settings_cycle_wraps` (next from the last choice wraps, prev from the first wraps, unknown current → default); `test_settings_validate` (200 → 128 for brightness, 15 stays 15 for dim, −1 → default); `test_settings_help_width` (every help ≤38 characters); `test_settings_sections` (13 rows, every row in one of the eight CFG-02 sections); `test_settings_cache` (load once, get/set without reload, setting the same value returns false). Hardware: flash over a device that has `bright=192` and `order=0` and confirm both survive.

**Docs.** `docs/decisions.md` (registry as the single source of setting metadata; generic NVS access), `docs/ux.md` §5.12 (one sentence: rows come from the registry, UI in CFG-01), `ROADMAP.md`.

**Acceptance.**
- [ ] `wk/SettingsModel` has no Arduino include and runs in `pio test -e native`.
- [ ] All seven existing keys and defaults are in the table and unchanged on a device that already stores them.
- [ ] `settingsAction()` and `drawSettings()` are untouched and still show 13 rows.
- [ ] NVS is read once at boot; a change performs at most one write and no write when the value is unchanged.
- [ ] The seven tests above pass.

### ARCH-03 — Local stores shared by STATS, STUDY and REM

**Implementation (2026-11-06).** Both stores are live. The journal
(`wk/Journal.{h,cpp}`, `platform/JournalStore.{h,cpp}`) implements the 16 B
record, 520 B CRC block, 4,000-record ring, account key and SRS-after patch;
`queueReviewLocal()` appends and the Stats History page consumes it. The
subject catalog (`wk/SubjectCodec`, `wk/Catalog`, `platform/SubjectCatalog`)
stores one `KKSB` file per subject under `/kanikey/cache/catalog/`, account
scoped, with a `KKCT` meta file, a 2,000-subject / 8 MB cap and LRU eviction.
No screen reads the catalog yet; STUDY and REM can call it. The SD mount now
keeps four file handles so the glyph pack, a catalog file and a directory scan
can coexist.

- **Priority:** P1
- **Size:** M
- **Depends on:** ARCH-01
- **Touches:** new `firmware/src/wk/Journal.h/.cpp`, `firmware/src/wk/SubjectCodec.h/.cpp`, `firmware/src/platform/JournalStore.h/.cpp`, `firmware/src/platform/SubjectCatalog.h/.cpp`; `firmware/src/wk/Cache.cpp` (lift `encodeSubject` 334–404 / `decodeSubject` 406–481 and their `Writer`/`Reader` helpers out of the anonymous namespace, format unchanged), `firmware/src/wk/Cache.h` (no constant changes, `kCacheVersion` stays 11), `firmware/src/app/OutboxController.cpp` (`queueReview`, `sendOutboxItem`), `firmware/src/app/SettingsController.cpp` (sign-out), `firmware/test/test_wk/test_wk.cpp`
- **Settings:** none

**Problem / goal.** Nothing on the device records what was reviewed: `SessionItem` (`wk/ReviewQueue.h:16–30`) is discarded when `endReview()` runs (`App.cpp:1399`) and the only history is the summary counters, which the next sync overwrites. STATS (forecast, history, streak), STUDY (critical items, recent mistakes) and REM all need per-review history, and STUDY/REM also need subjects that are not in the current batch. Without a shared format each task would invent its own file, and the existing subject cache cannot help: `wk/Cache.cpp:18` caps it at 128 subjects and it only ever holds the current review batch.

**Behaviour.** No new screen and no new network call. A completed review appends one 16-byte record to `/state/journal.bin`; the record's `srsAfter` is filled in when the POST succeeds. The journal survives reboot and offline sessions. It belongs to one account: sign-out deletes it and a different account key at load discards it. Settings › Clear cache does not delete it (history is not cache). Subjects fetched for lessons, critical items or recent mistakes land in `/kanikey/cache/catalog/`, keyed by subject id, up to 2,000 subjects; beyond that the least recently used is deleted. A corrupt journal block loses at most that block and prints one `[journal]` line; the app never refuses to boot because of it.

**Design.**
- (a) Journal, `/state/journal.bin` on LittleFS with temp + `.bak` like `platform/OutboxStore.cpp:15–17`:
  - Record, 16 bytes, `static_assert`ed: `completedAt` u32 (epoch, 0 when the clock is untrusted), `subjectId` u32, `assignmentId` u32, `stage` u8 (low nibble `srsBefore`, high nibble `srsAfter`, `0xF` = unknown), `flags` u8 (bit0 offline, bit1 practice, bit2 wrongAnswerOverride, bit3 srsAfterKnown, bit4 lessonStarted — a lesson start from LES-03, no SRS or answer fields), `incorrectMeaning` u8, `incorrectReading` u8 (saturating at 255).
  - Header 32 B: magic `KKJR`, version u16 = 1, recordBytes u16, recordsPerBlock u16, blockCount u16, `accountKey` u32 (`kk::cacheAccountKey`), nextRecord u32, count u32, CRC32 (fits in 32 B).
  - Blocks: 32 records + trailer `{seq u32, crc32 u32}` = 520 B; 125 blocks = 65,000 B = 4,000 records (~64 KB).
  - Torn write: only the block being written fails its CRC; the reader stops at the first bad block, reports `validCount` and keeps everything before it.
  - `srsAfter` patch: read-modify-write of the single block holding the record, recompute the trailer CRC, write to a temp block then copy. Worst case loses that block, never the whole file.
  - Pure `wk/Journal.h/.cpp`: `JournalRecord`, `journalEncodeRecord`/`journalDecodeRecord`, `journalEncodeBlock`/`journalDecodeBlock`, `journalWrapNext(next, count)`, `journalSlot(index)`; no allocation, no Arduino.
  - `platform/JournalStore.h/.cpp`: `journalOpen`, `journalAppend(const JournalRecord&)` → handle, `journalPatchSrsAfter(handle, stage)`, `journalCount`, `journalReadAt`, `journalFlush` (block boundary, `endReview`, before sleep), `journalClear`.
  - Wiring, three call sites only: where a completed review is added to the outbox (`queueReview`, `App.cpp:1146`; at completion once FIX-10 lands) append after `kk::outboxAdd` returns non-zero, with `offline = !kk::DevWifi::connected()`; the success branch of `sendOutboxItem` (`App.cpp:1091–1117`) patches `srsAfter` from `kk::ReviewUpdate`; sign-out (`App.cpp:2231-2275`) calls `journalClear()`.
- (b) Subject catalog, `/kanikey/cache/catalog/` on microSD:
  - `meta.bin`: magic `KKCT`, version u16 = 1, `accountKey` u32 from `kk::cacheAccountKey` (`wk/Cache.h:46`), `savedAt` u32, `count` u16, `bytes` u32, CRC32.
  - One `<subjectId>.kkc` per subject: magic `KKSB`, version u16, `accountKey` u32, `lastUsed` u32, payloadLen u16, CRC32, then the subject bytes.
  - Reuse the subject codec: move `encodeSubject`/`decodeSubject` and `Writer`/`Reader` from `wk/Cache.cpp`'s anonymous namespace into `wk/SubjectCodec.h/.cpp`; `Cache.cpp` calls them, so the KKCS payload is byte-identical and `kCacheVersion` stays 11.
  - Cap: 2,000 subjects or 8 MB, whichever comes first; eviction picks the smallest `lastUsed` (pure `catalogEvictIndex(const CatalogEntry*, size_t)`, ties by lowest id), deletes the file and logs one line. No index is kept in RAM: a lookup is one file open, and the LRU scan only runs when the cap is hit.
  - `platform/SubjectCatalog.h/.cpp`: `catalogOpen(sd, accountKey)`, `catalogClose`, `catalogHas`, `catalogLoad(sd, id, Subject*)`, `catalogStore(sd, id, const Subject&)`, `catalogCount`, `catalogClear`, `catalogPruneToCap`.
- Both stores are shipped and tested with no consumer; STATS, STUDY and REM only call the APIs.

**Tests.** `test_journal_record_roundtrip`; `test_journal_block_crc` (one flipped byte → decode false, earlier blocks still valid); `test_journal_ring_wraps` (nextRecord 3,999 → 0, count capped at 4,000); `test_journal_patch_srs_after` (unknown → 7, stage nibble and CRC updated); `test_journal_bad_version`; `test_journal_saturating_counts` (300 → 255); `test_subject_codec_roundtrip` (meanings, readings, stats, context sentences); `test_catalog_meta_account_mismatch`; `test_catalog_lru_victim` (oldest `lastUsed`, ties by id); `test_catalog_cap_math`. Hardware: 30 reviews offline, reboot, check the `[journal] n records` line and the file size (32 + 4×520 B); overwrite one block with a hex editor and confirm the reader stops cleanly.

**Docs.** `docs/design.md` §9.2 (two new rows: review journal on LittleFS, subject catalog on microSD), `docs/decisions.md` (formats, caps, why the journal is not an outbox), `ROADMAP.md`.

**Acceptance.**
- [ ] Record is 16 B, block 520 B, ring 4,000 records, all `static_assert`ed.
- [ ] A corrupt block costs at most that block; the reader logs and continues.
- [ ] `srsAfter` is unknown until the POST succeeds and is patched afterwards.
- [ ] The catalog survives reboot, rejects another account key and evicts at 2,000 subjects.
- [ ] Existing `KKCS`/`KKOB` files still load; `kCacheVersion` and `kOutbox` format unchanged.
- [ ] All ten tests pass and the release flash increase is reported (target ≤ 6 KB).

### ARCH-04 — Screen state machine

- **Priority:** P2
- **Size:** M
- **Depends on:** ARCH-01
- **Touches:** new `firmware/src/wk/ScreenModel.h/.cpp`, `firmware/src/app/AppState.h` (replace the screen booleans), `firmware/src/app/App.cpp` (`draw()` 4166–4248, `onKey()` 2395–2541, `loop()` sleep tail), `firmware/src/app/{SettingsController,ReviewController,LessonController,SetupController,SyncController,PowerController,DevConsole}.cpp` (set `state().screen` instead of the booleans), `firmware/test/test_wk/test_wk.cpp`
- **Settings:** none

**Problem / goal.** About 20 booleans decide what is drawn and who gets keys: `gShowHome`, `gReviewActive`, `gReviewSummary`, `gFeedback`, `gReviewDetail`, `gLessonActive/ListActive/EmptyActive/QuizActive`, `gPauseActive`, `gSettingsActive`, `gAboutActive`, `gSignOutConfirm`, `gOutboxScreen`, `gOutboxConfirm`, `gFirmwareActive`, `gSyncScreen`, `gLowBatteryPrompt`, `gPortalActive` (`App.cpp:90, 105–110, 146–151, 187–198, 217`). `draw()` (4166–4248) tests 17 of them in one order, `onKey()` (2395–2541) tests 13 in a different order, and the sleep predicate (4979–4983) tests 13 more. Nothing prevents `gAboutActive && gPauseActive`, and FIX-12 is a direct consequence.

**Behaviour.** No visible change for any state reachable today: same screen, same keys, same sleep and wake behaviour, same hint bars. Invalid combinations become unrepresentable: a modal always sits above the screen that opened it, and `Esc` pops exactly one level. Pause → Settings → About returns About → Settings → Pause without the `gSettingsOrigin` flag, and the device still cannot sleep under Pause, Settings, About, a review, a lesson or a confirmation.

**Design.**
- `wk/ScreenModel.h/.cpp` (pure, no Arduino): `enum class Screen { Boot, Home, Review, ReviewSummary, LessonEmpty, LessonList, LessonCard, LessonQuiz, Outbox, Firmware, Sync, Setup, Debug, RadicalTest }`; `enum class Modal { None, Pause, Settings, About, SignOut, OutboxConfirm, LowBattery, FirmwareConfirm }`; `struct ScreenDef { Screen screen; const char* name; bool keepsAwake; bool sessionActive; }` with `screenDef(Screen)`; `struct ScreenState { Screen screen; Modal stack[4]; uint8_t depth; }`.
- Functions: `screenPush(ScreenState*, Modal)` (false when full or already on top), `screenPop(ScreenState*)`, `screenTop(const ScreenState&)`, `screenBase(const ScreenState&)`, `screenCanSleep(const ScreenState&, bool portalActive, bool assetServerActive, bool firmwareBusy)`, `screenReset(ScreenState*)`.
- Screen table, matching today's sleep predicate exactly: Boot false/false, Home false/false, Review true/true, ReviewSummary true/true, LessonEmpty true/true, LessonList true/true, LessonCard true/true, LessonQuiz true/true, Outbox true/false, Firmware true/false, Sync false/false, Setup false/false, Debug false/false, RadicalTest false/false. Every modal keeps the device awake; `SignOut` and `OutboxConfirm` are always above Settings/Outbox, so the result matches today's list, which omits them.
- `AppState`: `ScreenState screen;` replaces all the booleans above; `ReviewState.detail` and `.feedback` stay as sub-state of `Screen::Review`; `PowerState.dimmed/screenOff` stay out of the screen model; `assetServerActive` stays a separate busy flag. No alias macros — grep for the old names must return nothing.
- `draw()`: `switch (screenBase())` draws one screen; then, if `screenTop() != Modal::None`, draw that modal on top. Reachable combinations reproduce today's order (About above Settings, SignOut above Settings, LowBattery above Home/Settings, OutboxConfirm above Outbox).
- `onKey()`: `screenTop()` wins, otherwise the base screen's key handler. Sync and Setup stay base screens, so they keep today's priority over everything else.
- `loop()`: replace the 13-term sleep predicate (4979–4983) with `screenCanSleep(...)`; the portal guard and the `externalPower` check stay.

**Tests.** `test_screen_modal_push_pop` (LIFO, pop on empty false); `test_screen_modal_full` (fifth push refused, state unchanged); `test_screen_pause_settings_about_chain` (Pause → Settings → About → pop → Settings → pop → Pause); `test_screen_sleep_predicate` (Home sleeps, Review does not, Sync keeps today's false, any modal blocks sleep, portal/asset/firmware-busy block sleep); `test_screen_session_active` (Review, ReviewSummary and the four lesson screens true; Outbox and Firmware false); `test_screen_table_complete` (every enum value has a def, names unique). Hardware: the ARCH-01 smoke list plus pause → settings → about → `Esc` ×3 returning to the review, and a 2-minute idle check that the device does not sleep mid-review.

**Docs.** `docs/ux.md` §3.3 (global shortcuts) and §5 (screen list: which screens are modal), `docs/design.md` §9.2, `docs/decisions.md` (screen state machine, modal stack), `ROADMAP.md`.

**Acceptance.**
- [ ] No `gShowHome`/`gReviewActive`/`gSettingsActive`/… booleans remain; the old names do not appear in `firmware/src`.
- [ ] `draw()` and `onKey()` are a screen switch plus one modal dispatch, and both match today's order for every reachable state.
- [ ] Sleep is decided only by `screenCanSleep`.
- [ ] `wk/ScreenModel` is pure and in the native suite.
- [ ] Release and dev images build; the hardware checks pass.

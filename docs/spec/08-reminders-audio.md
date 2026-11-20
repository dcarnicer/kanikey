# Reminders and audio

**Implementation (2026-11-06).** AUD-01 and the cache half of AUD-02 are in:
`Subject` keeps up to four transient `pronunciation_audios` clips (URL, content
type, voice), both parsers populate them, and the Cardputer board enables
`M5.Speaker`/ES8311 (`internal_spk = true`); serial `n` plays a 150 ms 880 Hz
test tone. `platform/AudioCache` stores clips under
`/kanikey/audio/<subject>-<voice>.<mp3|ogg>`, with a `KKAU` index, a 50 MB cap
and LRU eviction; serial `a` caches up to four clips from the current
review/lesson batch. Clear cache and sign-out delete the directory. Playback
(`AUD-03`) has a 2026-11-20 desktop decoder probe and a conditional go on
minimp3 (MP3 only); no playback code is merged and the device measurements
are still open. Automatic prefetch still waits for the AUD-01 hardware
numbers.

Decision D4 brings Kakehashi's reminders and vocabulary audio into this round. Reminders are device-local only: no push, no server; the device wakes itself from deep sleep, reads the cached summary and goes back to sleep. Audio is the `pronunciation_audios` path through the ES8311, cached on the user's own card. Both blocks start with a spike (wave 4) because they touch deep-sleep power, flash and the tight no-PSRAM heap. Together they replace ROADMAP EXT-09 and EXT-06.

| ID | Title | Priority | Size | Depends on | Wave | Status |
|---|---|---|---|---|---|---|
| REM-01 | Spike: timer wake and sound | P2 | M | none | 4 | partial (2026-11-20: timer wake ×3, 544 ms wake, tone 7.9 KB heap; current/ext1 abort pending) |
| REM-02 | Reminder wake | P2 | M | REM-01, SPEED-02, CFG (M1–M3) | 4 | review (2026-11-20: code + native tests; hardware pending) |
| REM-03 | Sounds | P3 | S | REM-01, REM-02, CFG (M4, M5) | 4 | review (2026-11-20: code; four cues played on hardware, heap one-off 7.9 KB; listening pending) |
| AUD-01 | Spike: vocabulary audio | P2 | M | none | 4 | partial (2026-11-06: parser + speaker) |
| AUD-02 | Audio cache on SD | P3 | M | AUD-01, SPEED-04, CFG (M7) | 4 | review (2026-11-20: batch prefetch hook; 5/5 clips in 3.8 s on hardware) |
| AUD-03 | Playback | P3 | M | AUD-01, AUD-02, CFG (M6, M7) | 4 | review (2026-11-20: minimp3 on-demand R; three plays on hardware, heap stable; listening/soak/current pending) |

### REM-01 — Spike: timer wake and sound

- **Priority:** P2 · **Size:** M (< ~500 lines)
- **Depends on:** none
- **Touches:** `docs/hardware.md` (new dated section); throwaway probes only, not merged: `firmware/src/board/BoardCardputerAdv.cpp` (`begin()` 155-179, `deepSleep()` 272-307), `firmware/src/app/App.cpp` (`setup()` 4822-4889)
- **Settings:** none

**Problem / goal.** Deep sleep arms only `ext1` on G0/G11 (`board/BoardCardputerAdv.cpp:296-304`) and never a timer; `wokeFromSleep()` (`:254`) recognises only `ESP_SLEEP_WAKEUP_EXT1`. Nothing in `docs/hardware.md` measures deep-sleep current, the cost of one wake (boot to sleep, no Wi-Fi) or what enabling `M5.Speaker` costs in flash and RAM. Without these numbers REM-02 and REM-03 cannot be judged, and the 250 KB round budget (README §2) is at risk.

**Behaviour.** No user-visible change. The spike answers four questions and records the numbers plus a go/no-go for reminders and for sound.

**Design.**
- Timer coexists with `ext1`: add `esp_sleep_enable_timer_wakeup(60ULL * 1000000ULL)` next to the `esp_sleep_enable_ext1_wakeup()` call (`BoardCardputerAdv.cpp:304`), and log `esp_sleep_get_wakeup_cause()` at boot. Run ≥10 wakes; confirm that both a key and the timer wake the chip and that `rtcMicros()` (`Board.h:126-128`) measures sleep time.
- Current: USB power meter, 5-minute averages for (a) deep sleep with `ext1` only, (b) `ext1` + timer armed at 15 min. Energy per wake = average wake current × boot-to-sleep time (log it, no Wi-Fi, `DevWifi` untouched).
- Speaker: set `cfg.internal_spk = true` (`BoardCardputerAdv.cpp:164`), call `M5.Speaker.tone()` after `M5.begin()`; measure the release-build flash delta and free heap/largest block before and after the first tone. Check the ES8311 path in the pinned M5Unified (`platformio.ini:52`) for `board_M5CardputerADV` (verify).
- RGB LED: check M5Unified's board data for `board_M5CardputerADV` and the Cardputer ADV documentation for a user-addressable RGB LED; the repo has no LED code today (verify).
- No new library; nothing is merged.

**Tests.** Hardware only, in `docs/hardware.md`: `timer-wake-cause`, `sleep-current-ext1`, `sleep-current-ext1-timer`, `wake-boot-ms`, `tone-flash-ram`, `led-present`.

**Docs.** `docs/hardware.md` — new dated section with a numbers table and an explicit go/no-go for REM-02 and REM-03; note the method and the meter used. No ROADMAP change yet.

#### Probe (dev image, `KANIKEY_DEV`; hardware run pending)

Code: `firmware/src/app/RemProbe.cpp` (dev only; the release image has none of it), `Board::deepSleep(timerUs)` (timer optional, release passes none). Serial commands:
- `rem <s> [n]` — arm a timer of `s` seconds (1–86400) for `n` timer wakes (1–32) and deep-sleep now with the key wake (G0/keyboard) still armed. Runs on battery or USB.
- `rem` — the probe record and the wake history of the last run.
- `rem tone` — one 880 Hz tone through `playTone()`, with heap before and after.

Repeat loop: each probe wake returns to `setup()` right after `firmwareBoot()` and sleeps again before storage, Wi-Fi or sync start, so no probe wake joins Wi-Fi. The last timer wake continues into the normal boot. A key wake (EXT1), a reset or a power-on ends the run at once and boots normally. Sleep time is `rtcMicros()` across sleep. The record survives deep sleep in RTC memory; `seq` counts up across runs until a power cut.

Log lines to collect (serial, 115200):
- `[rem] wake cause timer|ext1|other N seq n slept_ms ms requested_s s` — every boot; `other` with no run is a cold power-on or reset.
- `[rem] sleep seq n requested_s s left k` and `[rem] boot_ms ms` — at each probe sleep; `boot_ms` is boot to sleep with no Wi-Fi.
- `[rem] boot_ms ms` — every normal boot at the first draw (boot to ready).
- `[rem] repeat done after n timer wakes: normal boot` or `[rem] repeat aborted by cause N at seq n: normal boot`.
- `[rem] hist i seq n slept_ms ms boot_ms ms` — the run's wakes, printed once the run ends. USB drops while the chip sleeps, so read this line after the run, not the per-wake lines.
- `[rem] tone heap before free F largest L min M; after free F largest L min M (played 1)`.

The record is `RTC_NOINIT_ATTR`: opening the USB CDC port resets the board,
and an `RTC_DATA_ATTR` record was re-initialised by that reset before the host
could read it (first run, 2026-11-20).

#### Results (2026-11-20, Cardputer ADV, dev image, on USB)

| Check | Result |
|---|---|
| `timer-wake-cause` | `rem 20 3`: three timer wakes in a row (port gone/back at 20.4 s and 61.5 s); history `seq 1..3`, each `slept_ms 20545` for a 20 s timer; the last wake continued into the normal boot. The EXT1 key-abort path was not exercised (no one at the keys). |
| `wake-boot-ms` | Probe wake, boot → sleep with no storage/Wi-Fi: **544 ms** (both repeat wakes). Normal boot to the first draw: 941–1,069 ms. |
| `sleep-current-ext1` / `-ext1-timer` | **Not measured**: no meter in series with the battery. Energy per wake stays `current × 0.54 s`. |
| `tone-flash-ram` | Flash: no delta to measure, `internal_spk` has been on since AUD-01. Heap: the first `playTone(880,150)` took **7,880 B** that stay allocated (speaker task + I2S DMA; free 104,732 → 96,852 B), the largest block was unchanged (53,236 B); a second tone changed nothing. |
| `led-present` | M5Unified board data lists one RGB LED for `board_M5CardputerADV` on G21 (`M5Unified.inl:259`, `_setup_led()` one-LED RMT strip). Not checked visually; the repo drives no LED. |

**Go/no-go.** REM-02: **conditional go** — the timer coexists with the key
wake, sleep time is measured across deep sleep and a probe wake costs about
half a second with the radio off; daily energy waits for a meter, so the
default stays Off (M1 = 0). REM-03: **go** — the speaker is already
initialised, a tone costs a one-off 7.9 KB of heap and no largest-block
change; keep `internal_spk = true`. Both share the speaker with AUD-03, whose
36 KB decode block must be refused when the heap is short.

**Acceptance.**
- [ ] `ext1` + timer wake works repeatedly; both causes are logged. (Timer: yes; ext1 abort not run.)
- [ ] Deep-sleep current and wake cost are recorded with units and method. (Wake time yes; current needs a meter.)
- [x] Speaker flash/RAM cost and a short tone's heap use are recorded.
- [x] The RGB LED question is answered from M5Unified board data (present/absent).
- [x] A go/no-go for REM-02 and REM-03 is written; a no-go says what changes.

### REM-02 — Reminder wake

- **Priority:** P2 · **Size:** M (< ~500 lines)
- **Depends on:** REM-01, SPEED-02, CFG (M1–M3)
- **Touches:** new `firmware/src/wk/Reminder.{h,cpp}`; `firmware/test/test_wk/test_wk.cpp`; `firmware/src/board/Board.h` (power, 113-128) and `firmware/src/board/BoardCardputerAdv.cpp` (`begin()` 155-179, `wokeFromSleep()` 254, `deepSleep()` 272-307); `firmware/src/app/App.cpp` (`localHm()` 2576-2584, `effectiveNow()` 280-287, `loadCache()` 396-441, `buildHomeView()` 3051, `setup()` 4822-4889, idle sleep 4979-4988); `firmware/src/app/HomeScreen.{h,cpp}` (`HomeView` 12-31, next-review row 60-67, hint 117-126); settings accessors in `firmware/src/platform/DeviceSettings.{h,cpp}`
- **Settings:** M1 (minimum due count; 0 = off), M2 (quiet hours), M3 (daily reminder time); reads M4

**Problem / goal.** Deep sleep only arms `ext1` on G0/G11 (`BoardCardputerAdv.cpp:296-304`), and `wokeFromSleep()` checks only `ESP_SLEEP_WAKEUP_EXT1` (`:254`), so any timer wake would look like a power-on: `setup()` falls through to `startSync()` (`App.cpp:4884-4885`) and joins Wi-Fi. There is no scheduler at all — `Summary::nextReviewAt()` (`wk/Summary.cpp:50-57`) returns only future buckets and `dueReviews()` (`:43-48`) only counts now. The wall clock is also lost across deep sleep (`time()` restarts; only `rtcMicros()` keeps counting, `board/Board.h:126-128`), so a wake cannot say what time it is without help.

**Behaviour.** Before the idle sleep the device computes the next reminder from the cached summary and arms a timer next to the key wake. On a timer wake it starts no Wi-Fi: it reconstructs the time, recomputes due from the cache, and if `due ≥ M1` (and the moment is outside quiet hours M2) draws Home with the banner `25 reviews ready` for 30 s, plus the M4 chime when REM-03 is present. Any key in that window cancels the banner and continues the normal boot (quick sync, SPEED-02). Otherwise the device re-arms and sleeps without lighting the panel. The next wake is the earlier of the first future review bucket and the next local M3 time, skipped past quiet hours and clamped to 24 h; after a reminder fires, the next wake must be strictly later (no wake loop). No cached summary: no banner, no radio, re-arm the next daily time and sleep.

**Design.**
- New pure module `wk/Reminder`: `ReminderRules` (minDue, quiet on/off + start/end hour, daily on/off + hour/minute) and `reminderNextWakeAt(const Summary&, uint32_t nowUtc, uint32_t lastFiredAt, const ReminderRules&, LocalTimeFn)` plus `reminderWanted()` / `inQuietHours()`. Local time comes through a `uint32_t (*)(uint32_t utc)`-style callback so native tests use a fake clock (DST is a table, no host tzdata).
- Quiet hours wrap midnight; the daily time is built from local midnight + offset with `mktime` (`tm_isdst = -1`), so a spring-forward gap normalises and a fall-back overlap picks the first occurrence. A DST change during sleep may shift one reminder by ≤1 h; the post-wake recompute corrects the next one.
- Board HAL: `void setTimerWake(uint32_t epoch)` (0 = none) called just before `deepSleep()`; it arms `esp_sleep_enable_timer_wakeup()` beside the existing `ext1` call. `wokeFromSleep()` includes `ESP_SLEEP_WAKEUP_TIMER`; add `bool wokeFromTimer()`.
- Keep the epoch across sleep in `RTC_DATA_ATTR uint32_t gSleepEpoch` and `uint64_t gSleepRtcUs` (survive deep sleep, reset on power-on); after a timer wake `now = gSleepEpoch + (rtcMicros() - gSleepRtcUs) / 1000000`, never `time(nullptr)` before SNTP. Fall back to `effectiveNow()` (`App.cpp:280-287`) on a cold boot.
- In `setup()`, replace the final `else { startSync(); }` with `else if (!reminderWake()) startSync();`. `reminderWake()` returns false on a key wake/cold boot and true after it has slept again; it polls `tgc::board::update()`/`nextKeyEvent()` for up to 30 s and calls `saveCacheSnapshot()` before re-sleeping.
- Home: add `char reminder[24]` and `bool reminderActive` to `HomeView`; draw the banner in accent instead of the next-review row (`HomeScreen.cpp:60-67`) and swap the hint for `any key: continue` (117-126).

**Tests.** Native: `test_reminder_wake_at_first_bucket`, `test_reminder_quiet_hours_across_midnight`, `test_reminder_quiet_hours_disabled`, `test_reminder_daily_time_past_is_tomorrow`, `test_reminder_dst_spring_forward`, `test_reminder_dst_fall_back`, `test_reminder_empty_forecast`, `test_reminder_no_repeat_after_fire`, `test_reminder_disabled_arms_no_timer`. Hardware: a timer wake with Wi-Fi off (log shows no join, no TLS), the 30 s expiry re-sleeps, a key continues into the quick sync, banner text fits, measured daily energy against REM-01.

**Docs.** `docs/ux.md` §5.3 (banner + hint) and §5.12 (M1–M3 rows); `docs/api-sync.md` §7 (the summary cache is the only input; no request on a timer wake); `docs/decisions.md` new dated entry (device-local reminder, no push; clock reconstruction across deep sleep); `ROADMAP.md` EXT-09 → REM-02; `docs/hardware.md` from the hardware run.

**Acceptance.**
- [ ] A timer wake shows the banner without touching Wi-Fi or TLS.
- [ ] Quiet hours (including across midnight) and the daily fallback are native-tested.
- [ ] Any key during the 30 s window continues the normal boot; otherwise the device re-arms and sleeps.
- [ ] No wake loop after a reminder fires; the next wake is strictly later.
- [ ] M1 = 0 arms no timer; a cold boot behaves exactly as today.

**Dev check (procedure only; not run on hardware).** Dev image
`cardputer-adv-dev`, with `remind` and the log in [`docs/dev-tools.md`](../dev-tools.md) §2.4.
1. Send `T` then `C`, which give a dev token and 6 due reviews. Check local time is
   outside M2 quiet hours (22–08 by default) or set M2 Off.
2. Send `remind 30`. The log shows `[remind] next wake … in 30 s`, then
   `[remind] run 1 armed`. The port drops.
3. Watch the panel (no serial needed). Do not open the port during the 30 s
   banner: a reset ends it. Expected on the panel: the banner, with no Wi-Fi
   activity, then the banner goes and the chip sleeps.
4. After the expiry the chip sleeps and the port is gone. Press a key to wake it
   (a normal boot; the record keeps the run), open the port and send `remind`.
   Expected: one `[remind] hist 1` line with `rebuilt` near `expected` (the drift
   is a few seconds at most), `due 6`, `banner yes` and `outcome expiry`.
   `ended_by` reads `no-next-wake` when nothing re-armed (the seed has no future
   bucket; set M3 for a next wake), and `reset` when a next wake exists.
5. Key case: send `remind 30` again and press any key during the banner. Then
   open the port and send `remind`. Expected: `outcome key` and `ended_by key`,
   then the quick sync on that normal boot.

### REM-03 — Sounds

- **Priority:** P3 · **Size:** S (< ~150 lines)
- **Depends on:** REM-01, REM-02, CFG (M4, M5)
- **Touches:** new `firmware/src/platform/Sound.{h,cpp}` and pure `firmware/src/wk/SoundPlan.{h,cpp}`; `firmware/src/board/BoardCardputerAdv.cpp` (`begin()` 155-179) and `firmware/src/board/Board.h` (69-71); `firmware/src/app/App.cpp` (`checkCurrent()` 1348-1375, `showReviewSummary()` 967-996, `settingsAction()` 2133-2221); settings accessors in `firmware/src/platform/DeviceSettings.{h,cpp}`
- **Settings:** M4 "Sounds" — Off (default) / On, key `rm.sound`, enables the reminder chime and the answer/session tones; M5 "Volume" — Low / Medium (default) / High, key `rm.volume` (`03-settings.md`)

**Problem / goal.** `cfg.internal_spk = false` (`board/BoardCardputerAdv.cpp:164`) keeps the ES8311 off and `M5.Speaker` is never initialised, so every screen is silent: correct/wrong feedback (`app/ReviewScreens.cpp:117-210`) and the session summary (`App.cpp:967-996`). Kakehashi's `answerFeedbackSoundEnabled` has no counterpart. The reminder chime from REM-02 is also missing.

**Behaviour.** Off by default (today's behaviour). With M4 On, the reminder wake plays a short chime, a correct answer plays two short rising tones, a wrong answer one low tone, and a session end (only when at least one review was completed) a three-note figure. M5 sets the volume: Low/Medium/High. Tones are ≤120 ms each and never delay input. No sound during the setup portal or while a sync is running.

**Design.**
- `Sound::begin()` sets up `M5.Speaker` once; `Sound::tone(SoundKind, volume)` maps to `M5.Speaker.tone()`/`setVolume()` (verify the 0-255 range and the ES8311 path in the pinned M5Unified). Flash/RAM is what REM-01 measured.
- `board::begin()` keeps `internal_spk = true` when REM-01 shows the cost is acceptable; otherwise add a `bool speaker` parameter to `board::begin()` (`Board.h:69-71`) and set it in `App::setup()` from M4. Decided by the spike, not by the task.
- Pure `wk/SoundPlan`: kind → {frequency, duration} table and volume → level mapping, so the table is native-tested; the M5 calls stay in `platform/Sound`.
- Call sites: `checkCurrent()` after `gCheck` for correct/wrong; `showReviewSummary()` for the session end; `reminderWake()` for the M4 chime. Guard on the settings value and stop any tone before another starts.

**Tests.** Native: `test_sound_tones` (kind → frequency/duration), `test_sound_volume_map` (Low/Medium/High → the three levels), `test_sound_off_plays_nothing`. Hardware: each tone audible at each volume, no input lag, no tone before playback of audio starts (AUD-03), heap/stack unchanged after a session with sounds on.

**Docs.** `docs/ux.md` §5.5, §5.7 and §5.12 (M4/M5 rows); `docs/decisions.md` new dated entry that supersedes "2026-10-25 — Audio and stroke order out of v1" for device tones; `docs/hardware.md` (latency and heap); `ROADMAP.md` note under EXT-06.

**Acceptance.**
- [ ] Sounds are silent until M4 is turned On.
- [ ] Correct, wrong, session end and the reminder chime are distinct.
- [ ] No tone blocks the loop or delays a key.
- [ ] M5's Low/Medium/High is stored and applied without a reboot.
- [ ] A no-go in REM-01 marks this task dropped with the reason instead of shipping half of it.

**Dev check (procedure only; not run on hardware).** Dev image
`cardputer-adv-dev`; commands in [`docs/dev-tools.md`](../dev-tools.md) §2.4. M4 may stay Off.
1. Send `cue chime`, `cue correct`, `cue wrong`, `cue end` in turn. Each line
   prints `heap before … after …; played 1 (level 128, N ms)`. Each tone must be
   audible, and no input may lag.
2. Send `cue vol low`, then `cue correct`, then `cue vol high` and `cue correct`.
   The levels are 64 and 255. `cue vol` alone prints the current level.
3. Heap: the first tone may take a one-off block (REM-01: about 7.9 KB). Later
   cues must leave `after free` and `largest` unchanged.
4. M4 check: set M4 On and run `remind 30` (the timer wake plays the chime with the
   banner). With M4 Off the same wake is silent.
5. Guard: during a sync, `cue` prints `refused: a sync is running`.

### AUD-01 — Spike: vocabulary audio

- **Priority:** P2 · **Size:** M (< ~500 lines)
- **Depends on:** none
- **Touches:** `docs/hardware.md` (new dated section) and, only if a decoder is adopted, `THIRD_PARTY_NOTICES.md`; throwaway probes (not merged) around the serial handler in `firmware/src/app/App.cpp` (~4513-4760) and `firmware/src/platform/WkClient.cpp`
- **Settings:** none

**Problem / goal.** The parser drops audio completely: `subjectKey()` (`wk/SubjectParser.cpp:389-416`) has no audio key, `onArrayStart()` (`:146-167`) has no audio array, and the DOM path (`parseSubjects()`, `:516-666`) reads only `readings`. `Subject` (`wk/Types.h:33-73`) has no audio field, so neither the review batch (`WkReview.cpp:70`) nor the lesson batch (`WkLessons.cpp:66`) keeps a clip URL. `internal_spk = false` (`BoardCardputerAdv.cpp:164`) also means there is no output path yet. Clip size, decoder cost and the license are unknown.

**Behaviour.** No user-visible change. The spike records numbers and a go/no-go for AUD-02 and AUD-03.

**Design.**
- Shape: fetch one live vocabulary page through the dev serial path and dump the `pronunciation_audios` objects. Record `url` and `content_type`, and the exact voice metadata keys and values (the parser reads none of them today; names are verify).
- Size: byte length of ~10 clips per format (`audio/mpeg` vs `audio/ogg`), per voice; note the average, min and max and whether both formats exist for every clip.
- Decoder: compare minimp3 (CC0) and libhelix (RealNetworks RPSL — verify redistribution terms) on the S3 without PSRAM: decoder flash, decoder + output RAM, decode ms for a 1–2 s clip at 240 MHz, and whether it fits beside the 32 KB TLS buffer and the 24 KiB loop stack (README §2).
- Output: M5Unified Speaker/I2S → ES8311 with `internal_spk = true`; note whether `M5.Speaker.playRaw()` is enough or a custom I2S write is needed (verify the pinned API).
- No library is added during the spike. A decoder must fit the ≤250 KB round budget.

**Tests.** Hardware/host probe, recorded in `docs/hardware.md`: `audio-json-shape`, `clip-bytes-mpeg`, `clip-bytes-ogg`, `decoder-flash`, `decoder-ram`, `decode-ms`, `i2s-es8311`, `decoder-license`.

**Docs.** `docs/hardware.md` — new dated section with the size table, decoder table, I2S check and an explicit go/no-go; `THIRD_PARTY_NOTICES.md` only if a decoder is adopted (AUD-02 does that, not the spike).

**Acceptance.**
- [ ] The exact `pronunciation_audios` shape (url, content_type, voice keys) is written down.
- [ ] Clip sizes per format and per voice are recorded.
- [ ] Decoder flash, RAM and decode time are measured on the S3.
- [ ] The license of the chosen decoder is recorded or rejected.
- [ ] A go/no-go for AUD-02/AUD-03 is written; a no-go drops them with the reason.

### AUD-02 — Audio cache on SD

- **Priority:** P3 · **Size:** M (< ~500 lines)
- **Depends on:** AUD-01, SPEED-04, CFG (M7)
- **Touches:** `firmware/src/wk/SubjectParser.{h,cpp}` (`subjectKey()` 389-416, `onArrayStart()` 146-167, `onObjectStart()` 72-115, `onString()` 226-265, `parseSubjects()` 547-561); `firmware/src/wk/Types.h` (`Subject` 33-73); new `firmware/src/wk/AudioIndex.{h,cpp}` and `firmware/src/platform/AudioCache.{h,cpp}`; `firmware/src/platform/RootCerts.h` (reuse); `firmware/src/app/App.cpp` (clear cache 2211-2216, sign-out 2233-2238, after the SPEED-04 prefetch); `firmware/test/test_wk/test_wk.cpp`
- **Settings:** reads M7 (voice preference)

**Problem / goal.** There is no audio cache and nothing to fill it: subjects are cached as `KKCS` (`platform/CacheStore.cpp:15-17`, 119-136) without audio, and `cacheClearSubjects()` (`:138-142`) only removes the subject files. Audio must stay on the user's card, never in the repo (README §2), and be tied to the account key like the other caches (`wk/Cache.h:27-30`, `cacheAccountKey()` `wk/Cache.cpp:485-492`).

**Behaviour.** When online, right after the SPEED-04 subject prefetch, the device downloads the reading clip for each vocabulary in the review batch and the lesson batch to `/kanikey/audio/<subjectId>-<voice>.<ext>`, `.ext` from `content_type` (`mpeg` → `mp3`, `ogg` → `ogg`). Total is capped at 50 MB with LRU eviction; the least recently played clip goes first. Switching the M7 voice fetches the other voice on the next online batch; the old file stays until evicted. Failures are silent in the UI (one log line): no file, no retry loop, the session continues. **Which clip (2026-11-20):** the parsers keep only MP3 clips (up to 6) with their `metadata.pronunciation`; the download picks the clip that speaks the subject's primary reading (preferred voice, then the other), and only falls back to any MP3 when none does. A word with several readings played an accepted but non-primary one (近々 → きんきん); the `KKAU` index went to version 2, so older caches start over. **Playback (2026-11-20):** the player skips a leading ID3v2 tag of any size before the first-frame search (WaniKani clips carry ID3v2.4 tags, one longer than the 4 KB window), reads into a DMA-capable window, and, when the review heap is under the admission rule, borrows the 32.4 KB display canvas for the clip (the feedback frame is already on the panel) and gives it back when the pieces are freed. The prefetch shows progress and can be cancelled with `Esc` (README §2); a full or missing card skips it.

**Design.**
- Parser: add `ArrayKind::PronunciationAudios` and `ObjKind::Audio` at depth 5, read `url`/`content_type` (and the voice metadata from AUD-01) into a transient `std::vector<AudioClip>` on `Subject`. The DOM path gets the same fields. `encodeSubjectCache()` ignores `audios`, so `kCacheVersion` stays 11 (`wk/Cache.h:24`) and old files stay readable; URLs are re-fetched online, playback only needs the file name.
- `platform/AudioCache`: list/download/delete files, an index at `/kanikey/audio/index.bin` (own magic + version + `accountKey` + entries {subjectId, voice, bytes, usedAt}), atomic `.tmp` write. On an account-key mismatch, delete the directory contents. Sign-out and Settings › Clear cache clear it with the subject cache.
- `wk/AudioIndex`: pure encode/decode and `audioEvict(entries, capBytes, newBytes, outIds)` — native-tested.
- Download streaming: absolute S3 URLs, so `wkGet()` (`platform/WkClient.h:33-34`) cannot be reused; a small HTTPS GET with `RootCerts.h`, body written to the card in ≤4 KB chunks (no clip in RAM) and a per-batch time budget.
- Hook after `fetchReviewBatch()`/`fetchLessonBatch()` in the app adapter, not inside `WkReview`/`WkLessons` (file ownership).

**Implementation note (2026-11-20, download queue).** The batch hooks only queue: `audioPrefetchBatch()` copies the preferred MP3 URL of each vocabulary subject not on the card into a RAM queue (bounded at 24, deduped by subject and voice, emptied on an account change). The review hook never downloads, because the heap is short right after a large fetch. The lesson hook drains inline within 8 s under the same gates. On an idle Home, `audioTick()` downloads one queued clip per call. The drain waits for: no WkClient session, no sync, Wi-Fi up, the token usable, the card up, no modal, not dimmed, nothing playing, and 2 s since the last attempt (10 s after a heap refusal). It also needs a largest free block of at least 45 KB and 60 KB free, measured after the canvas is released and restored after the download. A clip gives up after two failed attempts. Each result is logged as `[audio] clip <id> ok|failed`.

**Implementation note (2026-11-20, persisted URLs, refill and fixed queue storage).** After the KKAU v2 bump wiped the clip cache, a review loaded its batch from the card and showed `no audio` on every vocabulary: the URLs existed only right after a subject fetch, so a card batch could not refill the queue, and a restart lost it. A dev `b` fetch was the only thing that queued clips again. On the same day the drain stopped with 127 KB free but a 41 KB largest block (fragmented), after the queue's per-entry URL strings and vector growth had been allocated during the fetch.
- **Persisted URLs.** Each parser calls `audioChooseSubjectUrls()` (`wk/AudioPlan`) once a subject is complete: `Subject::audioUrl[2]` holds, per voice, the MP3 that speaks the primary reading, else the first MP3 of that voice. Vocabulary and kana vocabulary only; other types get two empty strings. The choice is per voice, so a voice whose only clip is another reading still gets that clip; the old cross-voice priority is not kept. `KKCS` writes both URLs after `hasReviewStats` as length-prefixed strings (empty is 2 bytes) and is version 13 (`kSubjectsVersion`, an exact match). The snapshot `KKCA` stays 12. `wk/SubjectCodec` carries the same fields, so the catalog `KKSB` payload is version 2. Old files are not read: the review batch is fetched again, or offline it is unavailable until a fetch succeeds.
- **Cost.** On the card, about 2 × (2 + ~85) B per vocabulary subject (≈175 B with both voices) and 4 B for the others. In RAM, the URLs are `std::string` heap blocks, about 190 B per vocabulary subject (both voices), freed when the batch is evicted. `Subject::audios` stays transient.
- **Refill triggers.** `audioRequestRefill()` runs after `loadCachedBatch()` succeeds. `audioCacheTakeCreated()` asks as well when the clip cache was created empty (first use, an account change or an index version change). `audioTick()` then queues each playable subject's clip for the current voice (the other voice when only that one has a URL), skipping clips already on the card. It runs once per request and only while the batch is in RAM, so an idle loop does no SD work. The review and lesson batch hooks still queue directly.
- **Queue storage.** `AudioQueue` is a fixed array of 24 entries, each with `char url[129]`. A URL over 128 bytes is refused and logged as `URL too long`; it is never truncated. The queue is a static object in `app/AudioController.cpp` (about 3.3 KB of static RAM, set up at boot), so enqueueing does no heap work. The vector and the per-entry `std::string` copies are gone.
- **Other long-lived allocations found, not changed.** The clip index `gAudio.entries` (a vector that grows by `push_back` during a session, up to 2000 × 16 B), the review summary's bucket vectors, and the batch's subject strings (freed on eviction; the largest part of the review heap). A reserve at boot for the index would cost up to 32 KB, so it waits for measurements from `hh`.
- **Dev heap command.** `hh` (KANIKEY_DEV only) prints the internal heap: free, largest free block, fragmentation %, minimum free, free and allocated block counts.
- **Drain threshold unchanged.** `kAudioDrainMinLargest` stays 45 KB. A single clip download (one TLS session and a 4 KB chunk) might fit in less, but the only evidence is 2026-11-20: 23.5 KB largest failed at `start_ssl_client: -1`. Lowering it needs a measured success at that size.
- **Tests.** `test_audioq_*`: fixed storage (every URL kept through pops, failures and slot reuse), the 128-byte limit and `TooLong` without a slot, the refill rule, the own-voice-then-other choice, and the per-voice primary-reading choice. `test_cache`: the subject URL round trip (both encoders, empty URL), truncation inside the URL, and rejection of versions 12, 14 and the snapshot's 12. `test_wk`: the parser sets the URLs and leaves them empty without details.

**Tests.** Native: `test_audio_subject_parse_stream_and_dom`, `test_audio_subject_parse_absent`, `test_audio_filename_voice_and_ext`, `test_audio_lru_evicts_oldest`, `test_audio_lru_keeps_newest_under_cap`, `test_audio_index_round_trip`, `test_audio_index_version_rejected`, `test_audio_index_account_mismatch`. Hardware: 20 clips in one batch (time, heap largest block), reboot persistence, 50 MB eviction, card removed mid-download, cancel with `Esc`.

**Docs.** `docs/api-sync.md` §3.4 (audio fields) and §7 cache table (`/kanikey/audio`, index, cap, LRU); `docs/ux.md` §5.11 (progress/cancel); `docs/decisions.md` new dated entry (clips stay on the user's card, transient URLs, no format bump); `ROADMAP.md` EXT-06.

**Acceptance.**
- [ ] Audio URLs and content types survive the streaming and DOM parsers identically.
- [ ] Clips land at `/kanikey/audio/<subjectId>-<voice>.<ext>` and survive a reboot.
- [ ] The cap and LRU eviction are native-tested and hold on the card.
- [ ] A different token discards the audio directory; clear cache and sign-out remove it.
- [ ] `KKCA`/`KKCS` stay version 11 and an old cache still loads.
- [ ] Offline batches download nothing and log no error storm.

### AUD-03 — Playback

- **Priority:** P3 · **Size:** M (< ~500 lines)
- **Depends on:** AUD-01, AUD-02, CFG (M6, M7)
- **Touches:** new `firmware/src/platform/AudioPlayer.{h,cpp}`; `firmware/src/wk/AudioIndex.{h,cpp}` (file choice helpers); `firmware/src/app/App.cpp` (`reviewKey()` 1522-1568, `reviewDetailKey()` 950-967, `lessonKey()` 2041-2073, `checkCurrent()` 1348-1375, `advanceFeedback()` 1376-1398, `showLesson()` 1660, `loop()` 4892-4999); `firmware/src/app/ReviewScreens.{h,cpp}` (`ReviewView` 18-30, hint 196); `firmware/src/app/LessonScreens.{h,cpp}` (reading 205-217, hint 227-231); `firmware/test/test_wk/test_wk.cpp`
- **Settings:** M6 (autoplay), M7 (voice preference)
- **Status:** partial (2026-11-20: desktop decoder/playback probe; playback not written)

**Spike result (2026-11-20, desktop cross-compile).** A real clip from the
official WaniKani API example (`3020-subject-2467.mp3`) is 8,689 B: MPEG-1
Layer III, 128 kbps, 44.1 kHz, mono, with an ID3v2.4 tag and 0.52 s of audio.
The matching OGG (`3018-subject-2467.ogg`) is 8,535 B of Ogg Vorbis, 44.1 kHz,
mono. The API documentation (`docs.api.wanikani.com/20170710`, Pronunciation
Audio) says it delivers `audio/mpeg` and `audio/ogg`; the example carries both
for the same subject. A host minimp3 sanity run
decoded the MP3 as 20 frames / 23,040 samples / 0.522 s, so the ID3 tag and the
frame loop are understood. These are desktop probe numbers, not Cardputer
measurements.

The pinned build is `platform = espressif32@6.12.0` (Arduino-ESP32 2.0.17 /
ESP-IDF 4.4.7). The probe used `board_build.arduino.memory_type = qio_qspi`
with no PSRAM define, matching the Cardputer ADV, and a minimal Arduino sketch
as the baseline, so the deltas below are decoder-only. Its
`tools/sdk/esp32s3/lib` has no MP3, OGG/Vorbis, Opus or ADF decoder. The
separately installed `framework-arduinoespressif32-esp32s3-libs` 3.3.12
(IDF 5.5.5) does contain `libchmorgan__esp-libhelix-mp3.a` 1.0.3, but
that package is not the one platform 6.12.0 selects, and mixing its IDF 5.5 /
newer-toolchain objects into an IDF 4.4 build is not a drop-in. M5Unified has
no decoder either: its `Speaker_Class` plays PCM (`playRaw`/`playWav`) and
`tone`. The ES8311 path is already enabled (`internal_spk = true` in
`BoardCardputerAdv.cpp` `begin()`) and the pinned M5Unified configures the
Cardputer ADV as I2S1, BCK 41, WS 43, DATA 42, with an ES8311 enable callback.

| Decoder route | Cross-compile probe (desktop) | RAM / stack | License | Verdict |
|---|---|---|---|---|
| Helix (libhelix-mp3 source) | +32,488 B flash | 23,816 B heap at `MP3InitDecoder`; `MP3Decode` 80 B stack, largest callee 288 B | RPSL 1.0 / RCSL 1.0 | no: license and flash |
| minimp3 (generic) | +17,928 B flash | 6,668 B `mp3dec_t` + 16,236 B scratch; stock `mp3dec_decode_frame` uses 17,344 B stack | CC0 | possible, but stock stack is too large |
| minimp3 `MINIMP3_ONLY_MP3`, scratch made static | +16,384 B flash | 22,904 B static (6,668 B state + 16,236 B scratch); decode stack 1,104 B | CC0 | **recommended** |
| M5Unified Speaker | already in the firmware | PCM only; no compressed decoder | MIT | output path, not a decoder |
| ESP-ADF / esp-audio-libs | not in the pinned core | — | — | no: separate IDF component and output path |
| libmad | not in the pinned core | — | GPL-2.0 | no |
| OGG / Vorbis | not in the pinned core | — | — | no: second decoder; MP3 is delivered for the same clip |
| raw PCM | no source on the server | 46 KB per 0.52 s at 44.1 kHz mono | — | no: the API only serves MP3/OGG |

The Helix heap total is 2,032 B (`MP3DecInfo`) + 56 + 328 + 284 + 4,624 + 840
+ 6,944 + 8,708 B across eight allocations. The 661,456 B archive in the
newer package is only the on-disk form; the linked source build is the 32 KB
above.

The minimp3 figure is a one-line local patch: the 16,236 B
`mp3dec_scratch_t` is static instead of a stack local. That makes the decoder
non-reentrant, which is fine for one clip at a time, and drops the decode stack
from 17,344 B to 1,104 B so it cannot consume the 24 KiB loop stack. The
probe's 22,904 B is static RAM; if the state and scratch are allocated once on
first play instead, the same ~22.9 KB comes from the no-PSRAM heap. Add two
2,304 B PCM frame buffers and a 2 KB file buffer for a ~30 KB working set
(plus M5Unified's own speaker task/DMA, which starts on first playback).
The pinned M5Unified defaults are `dma_buf_len = 256`,
`dma_buf_count = 8`, a speaker task stack of 1,280 + 256 × 4 = 2,304 B and
8 × 256 × 32-bit = 8,192 B of I2S DMA; these are source values, not device
measurements. M5Unified is already a firmware dependency and its Cardputer
ADV speaker configuration is enabled; the decoder is the only new library.

The latest BT-01 entry records 170,787 B free in the 1,835,008 B app slot; the
2026-11-19 release pass recorded 278,019 B before later changes. Even the
smaller figure leaves about 154 KB after the 16.4 KB decoder, before player
code, so the 16.4 KB decoder fits. The 250 KB round budget is not the binding
constraint; the no-PSRAM heap and the 24 KiB loop stack are.

**Go/no-go.** Conditional go for the on-demand MP3 player with minimp3. No-go
for OGG and for autoplay in the first cut. If the hardware decode misses the
2× real-time gate, Helix is the only fallback and only if the RPSL/RCSL terms
are accepted; if the heap/stack gates fail too, drop AUD-03 rather than ship
a stuttering player.

**Problem / goal.** Nothing plays a cached clip: `R` is unused on reading
feedback (`ReviewScreens.cpp:196`), item detail (`reviewDetailKey()`,
`App.cpp:950-967`) and the lesson card (`lessonKey()`, `:2041-2073`), and there
is no "missing" state. The demo case is a vocabulary review where the reading
is on screen and the user hears nothing.

**Behaviour.** `R` replays the current subject's reading clip on reading
feedback (correct or wrong), on any item-detail page, and on the lesson card
when the reading line is shown. M7 chooses the voice; if that voice is missing
but the other voice has an MP3, the other voice plays. A missing clip is silent
and the hint reads `no audio` instead of `R: audio`. `R` during playback
restarts the clip. Changing item, advancing feedback or leaving the screen
stops playback before the screen changes. Kanji and radicals never offer `R`.
The first cut is on demand only: M6 autoplay is a follow-up after the hardware
run proves the decode time and heap, and stays Off by default. **M6 is now
implemented (2026-11-20; hardware pending):** see the M6 note below.

**Design.**
- `platform/AudioPlayer`: a small state machine over one open SD file. `begin()`
  opens the cache for the account; `play(subjectId, voice)` chooses an MP3
  entry, opens `/kanikey/audio/<subject>-<voice>.mp3`, reads the first chunk
  and starts the decoder. `audioTick(now)` decodes one MP3 frame per call into
  one of two 2,304 B mono PCM buffers (4,608 B each if a clip is stereo) and
  queues it with `M5.Speaker.playRaw(int16_t*, samples, hz, channels == 2, 1,
  kChannel, false)`. `stop()` calls `M5.Speaker.stop(kChannel)`, closes the
  file and resets the stream. `isPlaying()` and `hasClip(subjectId, voice)` are the only
  queries the UI needs. No clip is held in RAM and nothing is allocated per
  clip.
- Use M5Unified's buffer-release callback (pinned `e125e73`) to mark a PCM
  buffer free. Two buffers are enough; `playRaw` keeps reading the caller's
  buffer until the request is released, so the callback is the ownership
  hand-off. Queue only while `M5.Speaker.isPlaying(kChannel) < 2`; never wait
  in the tick. This keeps `audioTick()` non-blocking and the loop stack small.
- Decoder: vendor minimp3 `MINIMP3_ONLY_MP3` (CC0) with the static scratch
  patch; keep `mp3dec_t` and the scratch as one player-owned block, either
  static or allocated once on the first play. If the allocation fails, return
  false and show `no audio`; never retry in a loop and never fall back to
  network. Do not put the decoder in the 24 KiB loop stack. The ID3v2 tag is
  skipped by minimp3's frame finder; still test a real tagged clip.
- MP3 only. `wk/AudioIndex` gets a playable-entry helper (or
  `audioChooseEntry` gains an `AudioFormat` filter) that prefers the M7 voice
  and then the other voice, but returns only `AudioFormat::Mp3`. AUD-02's
  downloader must prefer `audio/mpeg` when a subject has both formats; an
  OGG-only entry stays in the cache but is not offered. This avoids a Vorbis
  decoder and keeps the hint honest.
- Hints: add `R: audio` only when an MP3 exists; drop `Tab: details` first, and
  list hidden keys in `?` when it still does not fit (README §2, CFG-04). Add
  `hasAudio` to `ReviewView`/`LessonCardView`; missing means `no audio`.
- Stop points: `advanceFeedback()`, `endReview()`, `showReview()`,
  `showLesson()` paging, `openPause()`, and any navigation that changes the
  subject. `R` restarts; do not let playback outlive the card.
- Share the M5Unified Speaker with `platform/Sound` (REM-03): stop a tone
  before playback, stop playback before a tone, and never run both.
- Autoplay (M6), if adopted later, is one call after a correct reading answer
  and once for the first page of a lesson card with a reading; it must use the
  same state machine and must not fire twice for one card.
- **M6 implementation (2026-11-20; hardware pending).** `rm.autoplay` On asks
  for the clip at two points: a reading answer checked Correct or Incorrect on
  review feedback (not a Warning), and the first draw of a lesson card with a
  reading line, once per card per lesson session (the cards and their quiz;
  paging back does not repeat it). The request is only pending. `audioTick()`
  starts it when `audioAutoplayWanted()` (`wk/AudioPlan`) still holds: M6 On,
  vocabulary or kana vocabulary, a cached MP3, no sync, no setup portal, and the
  screen still a review or lesson card. It waits while the REM-03 answer cue
  plays (`soundBusy()`), so a tone never cuts the clip and no key waits. Any key
  cancels a pending start; leaving the screen or any `audioStop()` also does. A
  start is tried once; a heap refusal is transient, like an explicit `R`. With M6
  Off nothing changes. `audioPlayerPlay()` now stops a running cue first, as its
  comment already said. Native: `test_audio_autoplay_wanted_rules`.

**Hardware spike.** Run on a real Cardputer ADV with the release build, a real
account and a real cached MP3; no device or serial access was available for
this probe. The exact checks:

| Check | Method | Proposed gate |
|---|---|---|
| `audio-clip-cache` | Cache one 0.5 s and one 1.5 s clip for the preferred voice with serial `a`; for the other voice use a probe-only dev hook or a card copy of `<id>-1.mp3` plus the index entry. Log path, bytes, content type and first MPEG sync | files exist, two voices, no network during play |
| `audio-decode-time` | `esp_timer` around each `mp3dec_decode_frame`, 240 MHz, release build, 3 s clip | total ≤ 50% of clip duration (≥2× real time); no frame > 20 ms |
| `audio-start-stop` | 100 `R`/stop cycles; log file open, first frame, `M5.Speaker` start and stop | no leak; first sound <100 ms; stop <50 ms |
| `audio-i2s-stack` | `uxTaskGetStackHighWaterMark` for the loop and speaker tasks; `M5.Speaker.isPlaying`; listen for underruns | loop keeps ≥4 KB headroom; speaker task ≥512 B; no glitch |
| `audio-latency` | Timestamp the key, the first `playRaw` and the first audible sample (scope or phone slow-motion) | first sound <100 ms; stop <50 ms |
| `audio-heap` | Free/largest/min before, after decoder allocation, during and after playback; then a TLS request | after playback + TLS ≥8 KB free and ≥4 KB largest; TLS block still allocatable |
| `audio-no-psram` | Allocate ballast until the decoder/speaker allocation fails; play | silent `no audio`, no panic/reboot; recovers after freeing |
| `audio-current` | USB power meter: 5-minute idle, 3 s clip loop, speaker on vs off | record mA average/peak and mAh per clip; no brown-out; compare REM-01 |
| `audio-ux` | `R` on feedback, detail and lesson; M6 Off; missing, OGG-only and R spam; navigate away; Wi-Fi off | stops immediately; no request; hint matches the file |
| `audio-build` | Release `pio run -e cardputer-adv`; record `.bin` and static RAM | fits the slot with the measured free bytes; no regression |

The 8,689 B / 0.52 s example is a real WaniKani clip but not necessarily the
user's voice or length; the hardware run must use a 1–2 s clip from the test
account. The example clip is 44.1 kHz mono; if a clip is stereo or a
different rate, pass the frame's `channels`/`hz` to `playRaw` and note it.

**Tests.** Native: `test_audio_choice_preferred_voice`,
`test_audio_choice_fallback_voice`, `test_audio_choice_missing`,
`test_audio_choice_prefers_mp3`, `test_audio_choice_ogg_only_missing`,
`test_audio_choice_no_audio_for_kanji`, `test_audio_hint_when_missing`.
Hardware: the table above, plus R on feedback/detail/lesson, both voices,
missing clip, R spam, screen change stops playback, heap/stack during a 3 s
clip and playback while offline.

**Docs.** `docs/ux.md` §5.5, §5.6, §5.9 (R and autoplay), §5.12 (M6/M7 rows);
`docs/decisions.md` 2026-11-20 entry (minimp3, MP3 only, on-demand first);
`ROADMAP.md` EXT-06 only after the hardware go; `THIRD_PARTY_NOTICES.md`
(minimp3 CC0) when it is vendored; `docs/hardware.md` for the device run.

**Acceptance.**
- [x] Desktop probe records decoder flash/RAM/license and the MP3/OGG sample (2026-11-20).
- [ ] A real cached MP3 plays offline through the ES8311, with no PSRAM.
- [ ] Decode is at least 2× real time and the loop keeps its stack/heap gates.
- [ ] `R` plays the right voice on feedback, item detail and the lesson card.
- [ ] A missing clip (including OGG-only) is silent, shows `no audio`, and makes no request.
- [ ] Changing item, advancing or leaving a screen stops playback immediately.
- [ ] Kanji and radicals show no audio hint.
- [ ] M6 autoplay stays Off in the first cut; it is a follow-up after the hardware go.

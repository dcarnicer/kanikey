# Reminders and audio

**Implementation (2026-11-06).** AUD-01 is partially done: `Subject` now keeps
up to four transient `pronunciation_audios` clips (URL, content type, voice)
and both parsers populate them, but the KKCS cache still ignores them. The
Cardputer board enables `M5.Speaker`/ES8311 (`internal_spk = true`) and serial
`n` plays a 150 ms 880 Hz test tone. The decoder, SD audio cache and playback
(`AUD-02`/`AUD-03`) are not shipped: choosing a decoder without the hardware
measurements in AUD-01 would violate the memory budget. The clip size, decoder
flash/RAM and I2S route still need a real-device run.

Decision D4 brings Kakehashi's reminders and vocabulary audio into this round. Reminders are device-local only: no push, no server; the device wakes itself from deep sleep, reads the cached summary and goes back to sleep. Audio is the `pronunciation_audios` path through the ES8311, cached on the user's own card. Both blocks start with a spike (wave 4) because they touch deep-sleep power, flash and the tight no-PSRAM heap. Together they replace ROADMAP EXT-09 and EXT-06.

| ID | Title | Priority | Size | Depends on | Wave | Status |
|---|---|---|---|---|---|---|
| REM-01 | Spike: timer wake and sound | P2 | M | none | 4 | todo |
| REM-02 | Reminder wake | P2 | M | REM-01, SPEED-02, CFG (M1–M3) | 4 | todo |
| REM-03 | Sounds | P3 | S | REM-01, REM-02, CFG (M4, M5) | 4 | todo |
| AUD-01 | Spike: vocabulary audio | P2 | M | none | 4 | partial (2026-11-06: parser + speaker) |
| AUD-02 | Audio cache on SD | P3 | M | AUD-01, SPEED-04, CFG (M7) | 4 | todo |
| AUD-03 | Playback | P3 | M | AUD-01, AUD-02, CFG (M6, M7) | 4 | todo |

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

**Acceptance.**
- [ ] `ext1` + timer wake works repeatedly; both causes are logged.
- [ ] Deep-sleep current and wake cost are recorded with units and method.
- [ ] Speaker flash/RAM cost and a short tone's heap use are recorded.
- [ ] The RGB LED question is answered from M5Unified board data (present/absent).
- [ ] A go/no-go for REM-02 and REM-03 is written; a no-go says what changes.

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

**Behaviour.** When online, right after the SPEED-04 subject prefetch, the device downloads the reading clip for each vocabulary in the review batch and the lesson batch to `/kanikey/audio/<subjectId>-<voice>.<ext>`, `.ext` from `content_type` (`mpeg` → `mp3`, `ogg` → `ogg`). Total is capped at 50 MB with LRU eviction; the least recently played clip goes first. Switching the M7 voice fetches the other voice on the next online batch; the old file stays until evicted. Failures are silent in the UI (one log line): no file, no retry loop, the session continues. The prefetch shows progress and can be cancelled with `Esc` (README §2); a full or missing card skips it.

**Design.**
- Parser: add `ArrayKind::PronunciationAudios` and `ObjKind::Audio` at depth 5, read `url`/`content_type` (and the voice metadata from AUD-01) into a transient `std::vector<AudioClip>` on `Subject`. The DOM path gets the same fields. `encodeSubjectCache()` ignores `audios`, so `kCacheVersion` stays 11 (`wk/Cache.h:24`) and old files stay readable; URLs are re-fetched online, playback only needs the file name.
- `platform/AudioCache`: list/download/delete files, an index at `/kanikey/audio/index.bin` (own magic + version + `accountKey` + entries {subjectId, voice, bytes, usedAt}), atomic `.tmp` write. On an account-key mismatch, delete the directory contents. Sign-out and Settings › Clear cache clear it with the subject cache.
- `wk/AudioIndex`: pure encode/decode and `audioEvict(entries, capBytes, newBytes, outIds)` — native-tested.
- Download streaming: absolute S3 URLs, so `wkGet()` (`platform/WkClient.h:33-34`) cannot be reused; a small HTTPS GET with `RootCerts.h`, body written to the card in ≤4 KB chunks (no clip in RAM) and a per-batch time budget.
- Hook after `fetchReviewBatch()`/`fetchLessonBatch()` in the app adapter, not inside `WkReview`/`WkLessons` (file ownership).

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

**Problem / goal.** Nothing plays a cached clip: `R` is unused on reading feedback (`ReviewScreens.cpp:196`), item detail (`reviewDetailKey()`, `App.cpp:950-967`) and the lesson card (`lessonKey()`, `:2041-2073`), and there is no "missing" state. The demo case is a vocabulary review where the reading is on screen and the user hears nothing.

**Behaviour.** `R` replays the current subject's reading clip on reading feedback (correct or wrong), on any item-detail page, and on the lesson card when the reading line is shown. With M6 on, the clip plays automatically after a correct reading answer and once when a lesson card with a reading opens (first page only). M7 chooses the voice; if that file is missing but the other voice is cached, the other voice plays. A missing clip is silent and the hint reads `no audio` instead of `R: audio`. `R` during playback restarts the clip. Changing item, advancing feedback or leaving the screen stops playback. Kanji and radicals never offer `R`.

**Design.**
- `platform/AudioPlayer`: open the file, decode with the AUD-01 decoder, feed I2S/`M5.Speaker`; pump from `loop()` with a static ≤4 KB buffer (`audioTick(now)`) so keys stay responsive and nothing large sits on the loop stack. Stop/restart on demand; never allocate per clip.
- `wk/AudioIndex` helpers: `audioFileName(subjectId, voice, ext)` and `audioVoiceFallback(cached, preferred)` — pure and native-tested.
- Hints: add `R: audio` only when a clip exists; drop `Tab: details` first, and list hidden keys in `?` when it still does not fit (README §2, CFG-04). Add `hasAudio` to `ReviewView`/`LessonCardView`.
- Stop points: `advanceFeedback()`, `endReview()`, `showReview()`, `showLesson()` paging, `openPause()`.
- Share the M5Unified Speaker with `platform/Sound` (REM-03): stop a tone before playback, mute playback when a tone starts.

**Tests.** Native: `test_audio_choice_preferred_voice`, `test_audio_choice_fallback_voice`, `test_audio_choice_missing`, `test_audio_choice_no_audio_for_kanji`, `test_audio_hint_when_missing`. Hardware: R on feedback/detail/lesson card, autoplay on and off, both voices, missing clip, R spam, screen change stops playback, heap/stack during a 3 s clip, playback while offline.

**Docs.** `docs/ux.md` §5.5, §5.6, §5.9 (R and autoplay), §5.12 (M6/M7 rows); `docs/decisions.md` new dated entry (playback defaults, voice fallback); `ROADMAP.md` EXT-06 → done; `THIRD_PARTY_NOTICES.md` (decoder license); `docs/hardware.md` (playback run).

**Acceptance.**
- [ ] R plays the right voice on feedback, item detail and the lesson card.
- [ ] Autoplay follows M6 and never fires twice for the same card.
- [ ] A missing clip is silent, shows `no audio`, and makes no network request.
- [ ] Changing item, advancing or leaving a screen stops playback immediately.
- [ ] Playback runs offline from the card and leaves the TLS heap block free.
- [ ] Kanji and radicals show no audio hint.

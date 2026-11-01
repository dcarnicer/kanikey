# Fixes from the 2026-11-01 review

Every item below was found and reproduced in the 2026-11-01 review of the firmware at commit `2115903`: code paths were read, the framework sources were checked and the device captures in `tools/screenshots/out` were used. Line numbers are from that commit; a task may correct small offsets but must not widen its file list. Wave `0a` tasks touch only `wk/`, `ota/`, `platform/` or `board/` and run in parallel; wave `0b` tasks touch `app/App.cpp` and run one after another. `0a+0b` means the task has both parts and its `App.cpp` part joins wave `0b`.

| ID | Title | Priority | Size | Depends on | Wave | Status |
|---|---|---|---|---|---|---|
| FIX-01 | OTA cannot follow GitHub's redirect | P0 | S | none | 0a | todo |
| FIX-02 | Times shown in UTC after every sync | P0 | S | none | 0b | todo |
| FIX-03 | A warning reveals the answer and allows a free retry | P0 | M | none | 0b | todo |
| FIX-04 | Kanji readings are judged the opposite way from WaniKani | P1 | S | none | 0a | todo |
| FIX-05 | The "n" hint makes users fail | P1 | S | none | 0a | todo |
| FIX-06 | Real HTTP read timeout is 1 s | P1 | S | none | 0a | todo |
| FIX-07 | Setup AP password derivable from the beacon | P1 | S | none | 0a | todo |
| FIX-08 | Review batches are capped at 20 and always the same items | P1 | M | none | 0a+0b | todo |
| FIX-09 | After a wrong first part, the failed part is asked again immediately | P1 | S | none | 0a | todo |
| FIX-10 | A completed review is not in the outbox until the next Enter | P1 | S | none | 0b | todo |
| FIX-11 | Lesson start errors are silent | P1 | M | none | 0a+0b | todo |
| FIX-12 | Deep sleep during the lesson quiz | P2 | S | none | 0b | todo |
| FIX-13 | A full outbox drops reviews silently | P2 | S | none | 0b | todo |
| FIX-14 | Fixed 32 KB body cap with no fallback | P2 | M | none | 0a | todo |
| FIX-15 | Typo tolerance uses the typed length | P2 | S | none | 0a | todo |
| FIX-16 | Capitalised romaji in the meaning field is not recognised as a reading | P2 | S | none | 0a | todo |
| FIX-17 | IME lacks common romaji aliases | P2 | S | none | 0a | todo |
| FIX-18 | `/level_progressions` parsed into a full DOM | P2 | M | none | 0a | todo |
| FIX-19 | Station setup page has no authentication | P2 | S | FIX-07 | 0a | todo |
| FIX-20 | OTA marked valid after only 10 s of loop | P2 | S | none | 0b | todo |
| FIX-21 | LittleFS formats on any mount failure | P3 | S | none | 0b | todo |
| FIX-22 | Outbox save rename order | P3 | S | none | 0a | todo |
| FIX-23 | Small UX bugs | P3 | S | none | 0b | todo |
| FIX-24 | Latent robustness | P3 | S | none | 0a+0b | todo |

### FIX-01 — OTA cannot follow GitHub's redirect

- **Priority:** P0
- **Size:** S
- **Depends on:** none
- **Touches:** `firmware/src/platform/Ota.cpp` (`kHeaderLineMax` :27, `request()` header loop :206-220), new `firmware/src/ota/HttpHeader.h` + `.cpp`, `firmware/test/test_wk/test_wk.cpp`, `docs/ota.md`
- **Settings:** none

**Problem / goal.** `kHeaderLineMax` is 512 and the header loop at `Ota.cpp:206-220` does `if (line.length() > kHeaderLineMax) break;` (:212) before it reads `Location` (:216-219), so the whole header block is abandoned at the first long line. Verified 2026-11-01 with curl: `releases/latest/download/kanikey-firmware.txt` → 302 (Location 93 chars) → `releases/download/v0.1.0/…` → 302 with a 934-char Location to `release-assets.githubusercontent.com` (signed URL); `github.com` also sends a ~4 KB CSP header. `location` therefore stays empty and `request()` returns `FetchStatus::Http` (:228-231), so the manifest check and every image download fail.

**Behaviour.** The update check follows both redirects and reaches the manifest. Long headers that are not `Location` are read and discarded, never stored. A `Location` longer than 4096 chars is treated as `FetchStatus::Http`, and the redirect chain still stops after `kMaxRedirects` (4). The failure text on the Firmware screen does not change. Devices already on v0.1.0 cannot use this fix over the air and need one USB flash.

**Design.**
- New pure helper in `ota/`: `struct ResponseHeaders { std::string location; long contentLength = -1; }` and `bool applyHeaderLine(std::string_view line, ResponseHeaders* out)` — trim CR/LF and spaces, compare the name case-insensitively, store `Location` (cap 4096), delegate `Content-Length` to the existing `parseContentLength()` (:107).
- The helper never signals "stop" on length: unknown or over-long headers are consumed and dropped. Bound the loop in the caller (e.g. 64 lines / 16 KB) so a hostile server cannot spin.
- Replace :206-220 with the helper; keep `kMaxRedirects` and `resolveUrl()` (:221-226).
- Read each line with a bounded reader: `readStringUntil('\n')` allocates the whole line, so a 4 KB CSP is a transient ~4 KB `String`; note it in the code and do not copy it into `ResponseHeaders`.

**Tests.** Native, in `test_wk.cpp` (it already includes `ota/Manifest.h`): `header_location_after_long_csp`, `header_location_before_long_csp`, `header_long_csp_after_location`, `header_crlf_trimmed`, `header_name_case_insensitive`, `header_location_4096_accepted`, `header_location_over_4096_rejected`, `header_content_length_parsed`. Hardware: **Settings › Firmware › Check now** on a v0.1.0 device shows the 302 → 302 → 200 chain in the serial log and a manifest version; then install it.

**Docs.** `docs/ota.md`: add a "Redirects" paragraph (GitHub redirects to a signed asset host; the device follows up to 4) and a note that a device on v0.1.0 must be flashed once over USB because its manifest check fails. Update the ROADMAP row.

**Acceptance.**
- [ ] The header loop never aborts on a long line; `Location` is kept up to 4096 chars.
- [ ] Non-`Location` long headers are discarded without being stored.
- [ ] The new helper lives in `firmware/src/ota/` with no Arduino headers.
- [ ] The named native tests pass in `pio test -e native`.
- [ ] `docs/ota.md` states the USB-flash requirement for v0.1.0 devices.

### FIX-02 — Times shown in UTC after every sync

- **Priority:** P0
- **Size:** S
- **Depends on:** none
- **Touches:** `firmware/src/app/App.cpp` `startSync()` :2725-2726 (`configTime`), `localHm()` :2576-2586
- **Settings:** none

**Problem / goal.** `App.cpp:2725-2726` calls `kk::settingsApplyTimeZone()` and then `configTime(0, 0, "pool.ntp.org", "time.cloudflare.com")`. arduino-esp32's `configTime` ends with `setTimeZone(-gmtOffset, daylight)`, which does `setenv("TZ", "UTC0…")` (framework `cores/esp32/esp32-hal-time.c`), so the POSIX rule applied a line earlier is overwritten. Evidence: `tools/screenshots/out/home-final.log` synced at 1793277601 = 12:40 UTC while the capture shows "Synced 12:40" and local time (CET) was 13:40. It affects the Synced clock (`HomeScreen.cpp:20`), Next review (`App.cpp:293`) and the level date (`localDate()`, `App.cpp:2590`).

**Behaviour.** After a sync every time on screen is local time for the configured zone: Home "Synced HH:MM", the next-review time, the level date and the session summary. A timezone change in Settings applies at the next sync without a reboot.

**Design.**
- Use `configTzTime(kk::settingsTimeZoneRule().c_str(), "pool.ntp.org", "time.cloudflare.com")` — it calls `setTz(rule)` and starts SNTP in one step.
- Keep `kk::settingsApplyTimeZone()` (:2725) or drop it; if the core version's `configTzTime` is missing, keep `configTime` and call `settingsApplyTimeZone()` **after** it (verify the core version).
- `App.cpp:4831` (setup finish) keeps applying the rule; `settingsTimeZoneRule()` is at `DeviceSettings.cpp:90`.
- `grep` confirms `App.cpp:2726` is the only `configTime`/`configTzTime` call in `firmware/src/`.

**Tests.** No native test (NVS + lwIP SNTP + libc `TZ`). Hardware: set `Europe/Berlin`, sync, and check that Home "Synced HH:MM", Next review and the level date equal local time; repeat with `America/New_York` and a POSIX rule without DST.

**Docs.** `docs/ux.md` §5.11 (sync) if it states the time; ROADMAP row.

**Acceptance.**
- [ ] No `configTime(0, 0, …)` call remains; the stored POSIX rule survives the SNTP start.
- [ ] Home "Synced HH:MM" equals local time after a sync on hardware.
- [ ] Next review and the level date are local.
- [ ] A timezone change applies at the next sync.

### FIX-03 — A warning reveals the answer and allows a free retry

- **Priority:** P0
- **Size:** M
- **Depends on:** none
- **Touches:** `firmware/src/app/ReviewScreens.cpp` `drawReviewScreen()` feedback branch :118-196, `firmware/src/app/LessonScreens.cpp` quiz feedback :277-346, `firmware/src/app/App.cpp` `showReview()` :886-901, `advanceFeedback()` :1376-1381, `reviewKey()` :1522-1542, `lessonQuizKey()` :1804-1830, `firmware/src/wk/AnswerChecker.cpp` :194
- **Settings:** none

**Problem / goal.** `ReviewScreens.cpp:118-190` draws the typed and answer rows for `Warning` as well as for `Incorrect`; `App.cpp:898` always sets `view.answer = correctAnswer(...)`; `Tab` on feedback (`App.cpp:1531-1536`) opens the full detail card even on a warning; `advanceFeedback()` (`App.cpp:1376-1381`) clears the input. Exploit: in a meaning question, type the reading in romaji → "That is the reading. Type the meaning" (`AnswerChecker.cpp:202-203`) plus the meaning on screen, then retry with no penalty. The lesson quiz has the same shape (`LessonScreens.cpp:277-346`, `App.cpp:1792`).

**Behaviour.** On `Warning` the feedback screen shows only the `CHECK` pill and the message — no typed row, no answer row. `Tab` does nothing and the hint bar reads `Enter: edit  Esc: pause`. `Enter` returns to the same question with the typed text intact (meaning string and IME raw), so the user can fix it and resubmit. The lesson quiz behaves the same with `Enter: edit  Esc: card`. A strict-mode typo warning says "Close. Check the spelling" and does not name the expected answer.

**Design.**
- `ReviewScreens.cpp`: in the feedback branch, when `view.status == Warning`, draw the pill and `view.message` only; skip the "You typed"/"Answer" and "Y"/"A" blocks. Add a `warning` flag (or reuse `status`) for the hint bar at :196.
- `App.cpp showReview()`: set `view.answer` only for `Correct`/`Incorrect`; leave it empty on a warning so nothing can leak through the detail card either.
- `reviewKey()`: ignore `Tab` while `gCheck.status == Warning`.
- `advanceFeedback()`: in the warning branch drop `clearReviewInput()` (:1378); keep `gFeedback = false` and `gDirty = true` so the question redraws with `reviewInput()`/`gIme.raw()`.
- Lesson quiz: `lessonQuizKey()` (:1813-1819) must not call `clearLessonQuizInput()` on a warning; keep the quiz input (`LessonQuiz::submit()` :15, `advance()` :22).
- `AnswerChecker.cpp:194`: message without the candidate, e.g. `"Close. Check the spelling"`; the final typo policy belongs to REV-02.

**Tests.** Native: `strict_typo_warning_does_not_name_answer` (the message contains no candidate text), `warning_status_unchanged` for the existing warning cases. Hardware: in a meaning question type `ichi` → only the pill and the message appear, `Tab` does nothing, `Enter` brings the text back editable; the same in the lesson quiz; a reading typed as romaji no longer shows the meaning.

**Docs.** `docs/ux.md` §5.5 (Feedback, warning state and hint bar) and §5.10 (Lesson quiz); note the decision in `docs/decisions.md` if REV-02 has not landed yet.

**Acceptance.**
- [ ] A warning screen shows no typed row and no answer row.
- [ ] `Tab` is ignored on a warning and the hint bar says `Enter: edit  Esc: pause`.
- [ ] `Enter` returns to the question with the typed text kept (meaning and IME raw).
- [ ] The lesson quiz keeps its input on a warning.
- [ ] The strict typo message does not contain the expected answer.

### FIX-04 — Kanji readings are judged the opposite way from WaniKani

- **Priority:** P1
- **Size:** S
- **Depends on:** none
- **Touches:** `firmware/src/wk/AnswerChecker.cpp` `matchesReading()` :100-125, reading branch of `checkAnswer()` :215-233; `firmware/src/wk/Types.h` :12, :20-25; `firmware/test/test_wk/test_wk.cpp`
- **Settings:** none

**Problem / goal.** `matchesReading()` skips readings with `accepted_answer=false` (`AnswerChecker.cpp:104`, via `isAccepted()` :18-21), so 人 + "hito" (kun'yomi, not accepted) is `Incorrect` where WaniKani shakes without a penalty. In the other direction, an accepted but non-primary reading (人 + "nin") becomes `Warning` in strict mode (`AnswerChecker.cpp:225-227`) where WaniKani says Correct. The app always passes `strict = true` (`App.cpp:1358-1360`).

**Behaviour.** Any reading with `accepted_answer=true` is Correct, primary or not. A reading that exists on the subject but is not accepted is a Warning that names the expected type when known: "WaniKani wants the on'yomi" / "…the kun'yomi" / "…the nanori", otherwise "That reading is not accepted for this item". No match stays Incorrect. Vocabulary and kana vocabulary keep their current behaviour.

**Design.**
- Keep `matchesReading()` for accepted readings; add a lookup that also matches `accepted=false` readings and reports the `ReadingType` (`wk/Types.h:12`: `Unknown, Onyomi, Kunyomi, Nanori`).
- In `checkAnswer()`: accepted match → `correct()` (delete the `!primary && strict` warning at :225-227); else non-accepted match → `warning(typeHint)`, else `incorrect("Incorrect reading")`.
- Keep the `isAccepted()` fallback (when no reading is marked accepted, all count) at :18-21.
- `stemOnly` ("Check the okurigana") stays before the new checks.

**Tests.** `kanji_rejected_kun_yomi_is_warning` (人 hito), `kanji_accepted_non_primary_is_correct` (人 nin), `kanji_primary_is_correct` (人 jin), `kanji_kunyomi_primary_rejects_onyomi_with_warning` (a kanji whose accepted readings are kun'yomi and whose on'yomi has `accepted_answer=false`), `kanji_rejected_reading_names_the_type`.

**Docs.** `docs/api-sync.md` §2 (subject readings) if it describes the rule; `docs/decisions.md` entry for the reading policy.

**Acceptance.**
- [ ] 人 + `hito` is a Warning with a type hint, not Incorrect.
- [ ] 人 + `nin` is Correct in strict mode.
- [ ] A kanji that wants its kun'yomi answers a non-accepted on'yomi with a Warning naming the kun'yomi.
- [ ] Vocabulary behaviour is unchanged (existing tests still pass).

### FIX-05 — The "n" hint makes users fail

- **Priority:** P1
- **Size:** S
- **Depends on:** none
- **Touches:** `firmware/src/wk/AnswerChecker.cpp` `nConversionFix()` :138-160, `checkAnswer()` :230-231; `firmware/test/test_wk/test_wk.cpp` :189-190
- **Settings:** none

**Problem / goal.** `AnswerChecker.cpp:231` says "Try nn before a vowel or y", but the IME (`RomajiIme.cpp:71-95`, same as WanaKana) turns `konya` into こにゃ → Incorrect, which is counted and sent to WaniKani; `kon'ya` → こんや is correct. The fix table at `AnswerChecker.cpp:138-147` maps にあ/にい/にう/にえ/にお, which the IME never produces: `tani` → たに, `kinen` → きねん, `genan` → げなん are all Incorrect with no hint.

**Behaviour.** Typing `konya`, `kinyoubi`, `tani`, `kinen` or `genan` gives a Warning: "Use n' before a vowel or y". Following the hint (`kon'ya`) is Correct. The warning is not counted and not sent to WaniKani.

**Design.**
- Replace `nConversionFix()` with a generator: for each kana position, if it is な/に/ぬ/ね/の, build a variant with ん + あ/い/う/え/お; if it starts にゃ/にゅ/にょ, build んや/んゆ/んよ.
- Match each variant against accepted readings with the existing `matchesReading()`; stop at the first match. Bound the work (input ≤ 64 chars, at most two variants per position).
- Message text: `"Use n' before a vowel or y"`.
- `RomajiIme.cpp` is read-only for this task; the IME stays WanaKana-compatible.

**Tests.** Update `test_wk.cpp:189-190` (it asserts the old `nn` text). Add `n_hint_konya`, `n_hint_kinyoubi`, `n_hint_tani`, `n_hint_kinen`, `n_hint_genan`, `n_apostrophe_is_correct` (今夜 + `kon'ya` → Correct), `n_hint_message_text`.

**Docs.** `docs/ux.md` §5.4 (reading input) with the `n'` rule; `docs/api-sync.md` if it documents reading input.

**Acceptance.**
- [ ] The warning text says "Use n' before a vowel or y".
- [ ] The five listed cases return Warning, not Incorrect.
- [ ] `kon'ya` for 今夜 is Correct.
- [ ] The updated `test_wk.cpp` case passes.

### FIX-06 — Real HTTP read timeout is 1 s

- **Priority:** P1
- **Size:** S
- **Depends on:** none
- **Touches:** `firmware/src/platform/WkClient.cpp` `request()` :82-115 (client setup, :114), :139-144; `firmware/src/platform/Ota.cpp` :155-176
- **Settings:** none

**Problem / goal.** `WkClient.cpp:114` calls `client.setTimeout(timeoutMs / 1000)` before `connect()`. `WiFiClientSecure::setTimeout` sets its own `_timeout` and the socket options only when the socket exists, so `Stream::_timeout`, used by `readStringUntil`, stays at 1000 ms (framework `libraries/WiFiClientSecure`). If WaniKani needs more than 1 s to answer, `httpCode` stays 0: `POST /reviews` goes Ambiguous → reconcile → resend → 422 and is shown as rejected although it was recorded; `PUT /start` fails silently. There is no `setHandshakeTimeout`, so the default is 120 s and `Esc` cannot cancel.

**Behaviour.** A slow server is waited for up to the request's `timeoutMs`; a stalled handshake gives up after 15 s with the existing TLS error line. `Esc` returns within a bounded time.

**Design.**
- Before `connect()`: `client.setHandshakeTimeout(15)` (verify the method on this core version).
- After a successful `connect()`: `static_cast<Stream&>(client).setTimeout(timeoutMs)` for reads and `client.setTimeout(timeoutMs / 1000)` for socket options.
- Same treatment in `Ota.cpp` for `WiFiClientSecure` (:162) and `WiFiClient` (:166), with a 15 s handshake timeout; the manifest and image reads use the same bounded read timeout.
- Keep the existing `millis()` deadline checks in `request()`.

**Tests.** No native test (Arduino framework classes). Hardware: a blackholed or throttled endpoint returns `httpCode 0` only after `timeoutMs`, not after 1 s; a `POST /reviews` on a slow link is not resent; the serial `[http]` timing shows the longer read.

**Docs.** `docs/api-sync.md` §6.3 (why not retry blindly) — note the read timeout and the 15 s handshake bound.

**Acceptance.**
- [ ] `Stream`'s read timeout is set to `timeoutMs` after connect in `WkClient.cpp`.
- [ ] `setHandshakeTimeout(15)` is set before connect in `WkClient.cpp` and `Ota.cpp`.
- [ ] `Ota.cpp` has the same post-connect timeout setup.
- [ ] A slow response no longer yields `httpCode 0` after 1 s on hardware.

### FIX-07 — Setup AP password derivable from the beacon

- **Priority:** P1
- **Size:** S
- **Depends on:** none
- **Touches:** `firmware/src/platform/SetupPortal.cpp` `beginAp()` :134-183 (password :166-168), `poll()` :235-260; `firmware/src/platform/SetupPortal.h` :60; new `firmware/src/wk/RandomCode.h` + `.cpp`; `firmware/test/test_wk/test_wk.cpp`; `docs/ux.md`, `docs/setup.md`
- **Settings:** none

**Problem / goal.** `SetupPortal.cpp:166-168` builds the password as `"kani"` + SoftAP MAC bytes 2-5. The SoftAP MAC is the broadcast BSSID, so anyone nearby can derive the key, join the AP and read the plain-HTTP `POST /wifi` (the home Wi-Fi password). There is no idle timeout: `lastActivity_` (`SetupPortal.h:60`) is set by `touch()` but never enforced. `docs/ux.md` §4.1 already promises a random 8-10 character key on screen.

**Behaviour.** Each portal start shows a new random 10-character key on the device screen next to the SSID; the AP uses it. After 15 minutes without a request the portal closes and the device returns to the boot state ("Setup pending"). The SSID stays `KaniKey-XXXX`.

**Design.**
- New pure helper `wk/RandomCode.h/.cpp` (in `wk/` so the native env compiles it; `platformio.ini` `[env:native]` only builds `wk/` and `ota/`): `void makeRandomCode(uint32_t (*rnd)(), const char* alphabet, size_t length, char* out, size_t n)`. The AP key uses an unambiguous alphabet (no `0/O/1/I/l`, e.g. `ABCDEFGHJKLMNPQRSTUVWXYZ23456789`), 10 characters, one call per `beginAp()`; FIX-19 reuses the helper with digits. `esp_random()` is already used in `board/BoardCardputerAdv.cpp:311`.
- Call it at `:166` instead of the MAC derivation; keep `esp_read_mac` for the SSID only.
- `poll()`: `constexpr uint32_t kApIdleMs = 15u * 60u * 1000u;` if active and `millis() - lastActivity_ > kApIdleMs`, `end(false)` and let the existing boot path take over (the AP hooks `onSwitchToStation`/`onFinish` at `SetupPortal.h:22-34` stay for the normal exits).
- The setup screen already draws `apPassword()`; verify it is visible with the key and the URL.

**Tests.** Native: `ap_password_is_10_chars`, `ap_password_uses_alphabet`, `ap_password_no_confusables`, `ap_password_changes_between_starts` (deterministic with an injected `rnd`). Hardware: the key on screen joins the AP; a second portal start shows a different key; after 15 idle minutes the AP disappears and the device is back at boot.

**Docs.** `docs/ux.md` §4.1 and §5.2 (key on screen, 15-minute close); `docs/setup.md`.

**Acceptance.**
- [ ] The AP password no longer derives from the MAC.
- [ ] A new 10-character key per portal start, shown on screen.
- [ ] The portal closes after 15 minutes idle and returns to boot.
- [ ] The generator is a pure helper with native tests.

### FIX-08 — Review batches are capped at 20 and always the same items

- **Priority:** P1
- **Size:** M
- **Depends on:** none
- **Touches:** `firmware/src/wk/SummaryParser.h` :12, `firmware/src/wk/SummaryParser.cpp` :163-190; `firmware/src/platform/WkProbe.cpp` :11-14, :44; `firmware/src/platform/WkReview.cpp` :26-38; `firmware/src/app/App.cpp` `runBatch()` :772-790, `pruneBatchToSummary()` :334-347, `startReview()` :1438-1500; `firmware/test/test_wk/test_wk.cpp`
- **Settings:** none (reads the existing Review batch size setting, `App.cpp:2167-2171`)

**Problem / goal.** `SummaryParser.h:12` defaults `maxIdsPerBucket` to 20 and `WkProbe.cpp:13` uses the default; `WkReview.cpp:33-34` fetches `min(limit, ids.size())`. With 409 due on the owner's account, the "available now" bucket keeps 20 ids, so the Review batch 30 setting never yields more than 20, shuffling and `lower_levels_first` act only inside those 20, and `pruneBatchToSummary()` (`App.cpp:334-347`) drops cached items outside them.

**Behaviour.** The batch is chosen from all due ids. With the shuffled order the batch is a random sample of the due set, so consecutive sessions differ. Batch 10/20/30 keeps its meaning. The Home count and the summary stay consistent with the cached batch.

**Design.**
- Raise `maxIdsPerBucket` to 1000 (4 B per id ≈ 4 KB) and pass it explicitly at `WkProbe.cpp:44`.
- `runBatch()`: `gSummary.dueSubjectIds(now, 1000)`; for the shuffled order, `std::shuffle` with a per-batch seed and take `reviewLimit`; pass those ids to `fetchReviewBatch()` (it slices to `want` already, `WkReview.cpp:33`). `lower_levels_first` keeps the current slice until REV-08 decides the sampling.
- Check the summary body against the 32 KB cap (`WkClient.cpp:22`) with FIX-14; 1000 ids should stay well below it (verify with a measured body).
- `pruneBatchToSummary()` already uses `dueSubjectIds(now, 1000)` — keep them in step.

**Tests.** `summary_parser_keeps_1000_ids`, `summary_parser_respects_lower_max`, `summary_due_ids_returns_more_than_20`, `summary_parser_1000_ids_stays_under_32k` (body size assertion). Hardware: on the owner's account (409 due) two shuffled sessions of 30 show different first items and 30 items each.

**Docs.** `docs/api-sync.md` §3.2 (summary buckets) and §5.4 (review session); ROADMAP row.

**Acceptance.**
- [ ] The summary keeps up to 1000 ids per bucket.
- [ ] Batch 30 yields 30 items with 409 due.
- [ ] Shuffled batches differ between sessions.
- [ ] The named native tests pass.

### FIX-09 — After a wrong first part, the failed part is asked again immediately

- **Priority:** P1
- **Size:** S
- **Depends on:** none
- **Touches:** `firmware/src/wk/ReviewQueue.cpp` `answer()` :95-163 (:151-153), `requeueFront()` :86-94; `firmware/test/test_wk/test_wk.cpp`
- **Settings:** none

**Problem / goal.** `ReviewQueue.cpp:150-153`: after a wrong meaning the queue switches to the reading (:126-129); a correct reading then leaves `allDone` false, so `setPart(item)` asks the meaning again right after the feedback showed it. The comment at :150 says the item should come back later.

**Behaviour.** After a wrong part and a correct other part, the item returns `kRequeueGap` (4) positions later with the failed part pending and both attempted flags reset. POST counts stay as today (`incorrectMeaning`/`incorrectReading`).

**Design.**
- In the Correct branch, before `setPart(item)`: if the other required part is pending and was already attempted (`meaningAttempted` / `readingAttempted`), call `requeueFront()` (it pops, `prepareItem()` resets the attempted flags, reinserts at `min(kRequeueGap, size)`) and set `out.requeued = true`.
- Otherwise keep `setPart(item)`.
- No change to `AnswerOutcome` fields.

**Tests.** `wrong_meaning_then_correct_reading_requeues` (meaning-first), `wrong_reading_then_correct_meaning_requeues` (reading-first), `requeued_item_returns_after_four`, `both_parts_correct_completes` (no requeue).

**Docs.** `docs/api-sync.md` §5.4 (review session); `docs/ux.md` §4.2 if it describes the requeue.

**Acceptance.**
- [ ] The failed part is not asked immediately after the other part is answered.
- [ ] The item returns after four positions.
- [ ] Both orders are covered by tests.
- [ ] The incorrect counters are unchanged.

### FIX-10 — A completed review is not in the outbox until the next Enter

- **Priority:** P1
- **Size:** S
- **Depends on:** none
- **Touches:** `firmware/src/app/App.cpp` `checkCurrent()` :1348-1375, `advanceFeedback()` :1376-1397, `endReview()` :1399-1420, `queueReview()` :1146-1161, `markCacheCompleted()` :501
- **Settings:** none

**Problem / goal.** `App.cpp:1365-1371` removes a completed item from the cache (`markCacheCompleted()` at :1370) but `queueReview()` only runs in `advanceFeedback()` (:1383-1386) or `endReview()` (:1400-1403). Power loss, a crash or **Pause › Settings › Firmware › Install** on the feedback screen loses the answer.

**Behaviour.** A completed review is written to the outbox and saved as soon as it completes, before the feedback is shown. Sending still happens on `Enter`, on session end or on the next flush. The item shows in the Outbox screen immediately.

**Design.**
- Split `queueReview()` into `bool queueReviewLocal(const kk::SessionItem&)` (`outboxAdd()` + `saveOutbox()`, no network; false when full) and `void sendQueuedReview(uint32_t seq)` (`sendOutboxItem()`).
- `checkCurrent()`: when `o.completed`, call `queueReviewLocal()` and keep the returned seq in a `gPendingSeq`; set `gNeedsSubmit = true`.
- `advanceFeedback()`/`endReview()`: send `gPendingSeq` if set and clear it; never add a second record.
- A full outbox is FIX-13's refusal, not a silent drop here.

**Tests.** No new native test (the ordering is in `App.cpp`); existing outbox tests must still pass. Hardware: complete a review, hold power on the feedback screen, reboot → the Outbox screen lists it and the flush sends it (201). Repeat with **Firmware › Install** on the feedback screen.

**Docs.** `docs/api-sync.md` §6.1 (record) and §5.4; `docs/ux.md` §5.13 (outbox).

**Acceptance.**
- [ ] A completed review is in the outbox before the feedback is drawn.
- [ ] `Enter` sends that record, it is not added twice.
- [ ] Reboot on the feedback screen keeps the review.
- [ ] Pause → Firmware → Install on the feedback screen keeps the review.

### FIX-11 — Lesson start errors are silent

- **Priority:** P1
- **Size:** M
- **Depends on:** none
- **Touches:** `firmware/src/app/App.cpp` `startCurrentLesson()` :1976-2006; `firmware/src/platform/WkClient.cpp` :139-144; `firmware/src/wk/Summary.h`/`Summary.cpp` (new pure helper); `firmware/test/test_wk/test_wk.cpp`
- **Settings:** none

**Problem / goal.** In `startCurrentLesson()` (`App.cpp:1976-2006`) any non-2xx other than 401/403 — offline, 5xx, 422, timeout 0 — shows nothing, and the started item stays in the list so it can be started again → 422, again silent. The summary lesson buckets are not updated (only `gLessons`), so the count comes back after a reboot. `WkClient.cpp:139-144` sends `Content-Type`/`Content-Length` only for `POST`, so the `PUT` body (`started_at`) is probably ignored.

**Behaviour.** `PUT /assignments/{id}/start` carries the body headers. A start that fails shows "Could not start lesson" with the reason and `Enter: retry  Esc: home`; the item stays and can be retried. A 422 counts as "already started": the card advances and the summary is updated. A started subject disappears from `gSummary` lesson buckets and from the cached snapshot.

**Design.**
- `WkClient.cpp:139`: `if (method == "POST" || method == "PUT")` for the two headers.
- `startCurrentLesson()`: treat 2xx and 422 as success (`gLessons--`, advance, `saveCacheSnapshot()`); 401/403 keep `setSyncIssue()`; everything else draws the error state with the code and keeps the item.
- New pure helper `bool Summary::removeLessonSubject(uint32_t id)` that drops the id from every lesson bucket and decrements `count`; call it on success.
- Keep the change small: LES-03 rebuilds the flow and must stay compatible.

**Tests.** Native: `summary_remove_lesson_subject_updates_count`, `summary_remove_lesson_subject_keeps_other_buckets`, `summary_remove_unknown_subject_is_noop`. Hardware: airplane mode → the error screen appears and `Enter` retries; after a successful start the card is not offered again and the lesson count stays down after a reboot.

**Docs.** `docs/api-sync.md` §3.7 (`PUT /assignments/{id}/start`), `docs/ux.md` §5.8/§5.9; ROADMAP row.

**Acceptance.**
- [ ] `PUT` sends `Content-Type` and `Content-Length`.
- [ ] A failed start is visible, with retry, and the item is not lost.
- [ ] 422 is treated as already started.
- [ ] The summary lesson bucket and the snapshot are updated on success.

### FIX-12 — Deep sleep during the lesson quiz

- **Priority:** P2
- **Size:** S
- **Depends on:** none
- **Touches:** `firmware/src/app/App.cpp` idle-sleep condition :4979-4985
- **Settings:** none

**Problem / goal.** The sleep condition at `App.cpp:4979-4985` lists `gLessonActive` and `gLessonListActive` but not `gLessonQuizActive`, and `startLessonQuiz()` sets `gLessonActive = false` (`App.cpp:1748-1749`). Two idle minutes on battery during the quiz reboot the device and lose the lesson batch.

**Behaviour.** The device never sleeps while the lesson quiz is open, same as during a review. `ARCH-04` removes this class of bug later.

**Design.**
- Add `!gLessonQuizActive` to the condition at :4979.
- Check the other modal flags in the same condition for a missing sibling (`gLessonEmptyActive`, `gOutboxScreen`, …) and report, do not fix unrelated ones.

**Tests.** Hardware: start a lesson quiz on battery, wait past **Settings › Sleep** → no sleep; the same with the lesson list and the lesson card.

**Docs.** None beyond the ROADMAP row.

**Acceptance.**
- [ ] `gLessonQuizActive` blocks idle sleep.
- [ ] No sleep during the quiz on hardware.
- [ ] No unrelated state added.

### FIX-13 — A full outbox drops reviews silently

- **Priority:** P2
- **Size:** S
- **Depends on:** none
- **Touches:** `firmware/src/app/App.cpp` `queueReview()` :1146-1161 (:1153), `startReview()` :1438-1500; `firmware/src/app/HomeScreen.h`/`.cpp` (status line :119-122); `firmware/src/wk/Outbox.h` :35
- **Settings:** none

**Problem / goal.** `App.cpp:1152-1155` only logs when `outboxAdd()` returns 0; `wk/Outbox.h:35` sets `kOutboxMax = 128` (≈4 offline sessions of 30). A review completed with a full outbox is lost after the feedback screen.

**Behaviour.** Starting a session with fewer free slots than the batch size shows "Outbox full" with `Enter: sync  Esc: home` instead of starting. Home shows "Outbox nearly full" in the status line above 75% (96 of 128). A completed review can never be dropped without a visible message.

**Design.**
- `size_t outboxFreeSlots()` = `kOutboxMax - gOutbox.size()`: rejected, stuck, sending and ambiguous items occupy slots too.
- `startReview()`: after `reviewLimit` is known (:1453), if `outboxFreeSlots() < reviewLimit`, draw the blocking screen and return before `runBatch()`.
- `HomeView`: add `bool outboxNearlyFull`; `HomeScreen.cpp:119-122` adds the text to the status line without pushing out the key hints.
- FIX-10's local queue still checks `outboxAdd()` and reports if it fails.

**Tests.** No new native test (`outboxPendingCount()` is already covered). Hardware (dev build): fill the outbox to 128, start a session → the refusal screen; at 96 pending, Home shows "Outbox nearly full"; sync frees slots and the session starts.

**Docs.** `docs/ux.md` §5.3 (Home status) and §5.13 (outbox); `docs/api-sync.md` §6.4 (limitations).

**Acceptance.**
- [ ] A session cannot start when free slots are below the batch size.
- [ ] Home shows "Outbox nearly full" above 75%.
- [ ] No review is dropped without a visible message.

### FIX-14 — Fixed 32 KB body cap with no fallback

- **Priority:** P2
- **Size:** M
- **Depends on:** none
- **Touches:** `firmware/src/platform/WkClient.cpp` :22 (`gBodyBuffer`), `request()` truncation :170-230; `firmware/src/platform/WkReview.cpp` :63-90; `firmware/src/platform/WkLessons.cpp` :60-90; `firmware/src/platform/WkProbe.cpp` :44; `docs/hardware.md`
- **Settings:** none

**Problem / goal.** `WkClient.cpp:22` fixes the shared body buffer at 32 KB. A big summary (thousands of ids) or a 5-vocabulary subject page with long context sentences can truncate (`r.truncated` at :182, :204, :213), retry, truncate again and fail the step.

**Behaviour.** A truncated subject page is retried with smaller batches (5 → 2 → 1) instead of failing the sync. The summary either parses streaming under the cap or, when truncation is reported, retries once with a larger cap only if the heap allows, otherwise reports a clear error.

**Design.**
- `WkReview.cpp:67-69`: on a truncated page, re-request that page with 2 subjects, then 1; merge the results in order.
- `WkLessons.cpp:63-65`: same for 3 → 1.
- Summary (`WkProbe.cpp:44`, already streaming via `wkGetBodyRetry`): on truncation retry once with a larger cap when `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` leaves room after `gFreeCanvasForRequest`, else keep the current error.
- Log the measured body sizes (summary, 5-subject page) and record them in `docs/hardware.md`.

**Tests.** Native: `summary_large_body_parses` (FIX-08 covers the id count), `summary_body_over_cap_reports_truncated`. Hardware: measure `/summary` and a 5-subject page on the owner's account; force a small cap in a dev build to see the 5 → 2 → 1 path.

**Docs.** `docs/hardware.md` (new measured sizes); `docs/api-sync.md` §3.2/§3.4 (body sizes).

**Acceptance.**
- [ ] A truncated subject page falls back to 2, then 1 subject.
- [ ] Lessons use the same fallback.
- [ ] The summary path has a defined truncation behaviour.
- [ ] Measured sizes are in `docs/hardware.md`.

### FIX-15 — Typo tolerance uses the typed length

- **Priority:** P2
- **Size:** S
- **Depends on:** none
- **Touches:** `firmware/src/wk/AnswerChecker.cpp` `closeMeaning()` :63-83 (:81-82), `firmware/src/wk/Text.h` :184-189, `firmware/test/test_wk/test_wk.cpp`
- **Settings:** none

**Problem / goal.** `closeMeaning()` calls `typoTolerance(normalized.size())` on the **input** (`AnswerChecker.cpp:81`), so tolerance scales with what was typed, not with the answer. 一 + "none" is "close" to "One"; 手 + "handle" is "close" to "Hand".

**Behaviour.** Tolerance comes from each candidate answer's length: a 3-letter answer allows no typo, a 5-letter answer one. 一 + "none" and 手 + "handle" are Incorrect, while a one-character typo in a long answer is still "close" (shown as Correct or Warning according to the REV-02 typo policy).

**Design.**
- In `closeMeaning()`, move the check into `consider()`: keep the candidate with the smallest distance that satisfies `d <= typoTolerance(n.size())`; return it as `closest`.
- Update `typoTolerance()` (`Text.h:184-189`) to the WaniKani curve — ≤3 → 0, ≤5 → 1, ≤7 → 2, else `2 + len/7` — currently it is ≤8 → 2, else 3. The curve is medium confidence: **verify** against WaniKani before changing the boundary.
- Keep the `normalized.size() < 4` early return (:80) or drop it once the candidate rule is in; do not keep both gates silently.

**Tests.** `typo_tolerance_boundaries` (3/4/5/7/8), `short_answer_long_input_not_close` (一/none, 手/handle), `long_answer_small_typo_close` (Composition/"compositon"), `two_typos_in_three_letters_not_close`, `exact_match_unaffected`.

**Docs.** `docs/decisions.md` entry for the typo policy (shared with REV-02).

**Acceptance.**
- [ ] Tolerance is computed from the candidate length.
- [ ] 一 + "none" and 手 + "handle" are Incorrect.
- [ ] Boundary cases 3/4/5/7/8 are tested.
- [ ] The curve is verified against WaniKani or the task reports it could not be.

### FIX-16 — Capitalised romaji in the meaning field is not recognised as a reading

- **Priority:** P2
- **Size:** S
- **Depends on:** none
- **Touches:** `firmware/src/wk/AnswerChecker.cpp` `normalizedReading()` :33-42; `firmware/test/test_wk/test_wk.cpp`
- **Settings:** none

**Problem / goal.** `normalizedReading()` does not lowercase before `romajiToHiragana()`, which only matches lowercase table entries and leaves other ASCII bytes as they are. In the meaning field "Ichi" becomes `Iち` and is not a reading → Incorrect, while "ichi" becomes いち → Warning "That is the reading. Type the meaning".

**Behaviour.** Capitalised and mixed-case romaji are recognised the same as lowercase, in both the meaning and the reading field.

**Design.**
- Lowercase ASCII A-Z in `normalizedReading()` before `romajiToHiragana()`; UTF-8 kana bytes are ≥ 0x80 and stay untouched.
- Optionally do the same inside `romajiToHiragana()` (`RomajiIme.cpp:65`) so the IME is case-insensitive too; if so, add the IME test there.

**Tests.** `meaning_field_capitalised_romaji_is_reading_warning` ("Ichi"), `reading_field_capitalised_romaji_is_correct` ("Ichi"), `mixed_case_kana_untouched`, `uppercase_ascii_meaning_unchanged` ("EAT" still Correct).

**Docs.** None beyond the ROADMAP row.

**Acceptance.**
- [ ] "Ichi" in the meaning field gives the reading warning, not Incorrect.
- [ ] "Ichi" in the reading field is judged like "ichi".
- [ ] Existing meaning normalisation tests still pass.

### FIX-17 — IME lacks common romaji aliases

- **Priority:** P2
- **Size:** S
- **Depends on:** none
- **Touches:** `firmware/src/wk/RomajiIme.cpp` `kTable` :17-55, `firmware/test/test_wk/test_wk.cpp`
- **Settings:** none

**Problem / goal.** `kTable` (`RomajiIme.cpp:17-55`) has `si/ti/tu/hu/zi/tya` but not `sya/syu/syo`, `zya/zyu/zyo`, `jya/jyu/jyo`, `cya/cyu/cyo`, `she/che/je`, `xtsu/ltsu`. 写真 typed as "syasin" becomes "sやしん" and the checker answers "Type the reading in kana".

**Behaviour.** Every alias below produces the same kana as its Hepburn form, so "syasin" → しゃしん and is judged normally.

**Design.** Add to `kTable` (longest match wins, so order is free): `sya/syu/syo` → しゃ/しゅ/しょ; `zya/zyu/zyo` and `jya/jyu/jyo` → じゃ/じゅ/じょ; `cya/cyu/cyo` → ちゃ/ちゅ/ちょ; `she` → しぇ, `che` → ちぇ, `je` → じぇ; `xtsu`/`ltsu` → っ. Keep the existing `dya/dyu/dyo`, `tya/tyu/tyo`, `ja/ju/jo`.

**Tests.** `ime_alias_sya_row`, `ime_alias_zya_row`, `ime_alias_jya_row`, `ime_alias_cya_row`, `ime_alias_she_che_je`, `ime_alias_xtsu_ltsu`, `ime_syasin_is_shashin`, plus a check that the Hepburn forms still map identically.

**Docs.** None beyond the ROADMAP row.

**Acceptance.**
- [ ] Every alias in the brief maps to the expected kana.
- [ ] "syasin" gives しゃしん.
- [ ] Existing IME tests still pass.

### FIX-18 — `/level_progressions` parsed into a full DOM

- **Priority:** P2
- **Size:** M
- **Depends on:** none
- **Touches:** `firmware/src/platform/WkLevelProgress.cpp` `fetchLevelProgress()` :32-45; `firmware/src/wk/LevelProgress.cpp` `parseLevelProgressions()` :35, `LevelProgress.h` :25; `firmware/test/test_wk/test_wk.cpp`
- **Settings:** none

**Problem / goal.** `WkLevelProgress.cpp:34` uses `wkGetJsonRetry("/v2/level_progressions", token, 32768, 12000)`, which builds a full `JsonValue` DOM for every progression before `parseLevelProgressions()` keeps one entry. Estimate (not measured): ~1.8 KB heap per progression → ~110 KB at level 60, more with resets. `bad_alloc` aborts (no exceptions) and the sync runs at boot, so high-level users can hit a crash loop; more than ~80 progressions also exceed the 32 KB body.

**Behaviour.** Level progress is read with constant memory regardless of the number of progressions; the level card shows the same values as today. A truncated or malformed body reports the existing "no matching progression" path.

**Design.**
- New pure `bool parseLevelProgressionsText(std::string_view body, uint8_t currentLevel, LevelProgress* out)` in `wk/LevelProgress.cpp`, using `wk/JsonStream.h` (SAX sink) or a `SummaryParser`-style reader; keep only the entry whose `level` equals `currentLevel` and reset `out` per attempt.
- `WkLevelProgress.cpp`: replace `wkGetJsonRetry` with `wkGetBodyRetry` (`WkJson.h:44-52`) and a trampoline like `WkProbe.cpp:11-14`; keep the two `fetchCount()` requests (:52-66).
- Delete the DOM entry point only if nothing else uses it (`grep parseLevelProgressions`).

**Tests.** `level_progressions_60_levels_keeps_current`, `level_progressions_missing_current_keeps_defaults`, `level_progressions_malformed_body`, `level_progressions_truncated_body`, `level_progressions_60_levels_memory_constant` (synthetic 60-level body).

**Docs.** `docs/api-sync.md` §3.9 (`/level_progressions`); ROADMAP row.

**Acceptance.**
- [ ] No DOM is built for `/level_progressions`.
- [ ] A synthetic 60-level body parses with the current entry only.
- [ ] The level card values are unchanged.
- [ ] The named native tests pass.

### FIX-19 — Station setup page has no authentication

- **Priority:** P2
- **Size:** S
- **Depends on:** FIX-07 (`wk/RandomCode`)
- **Touches:** `firmware/src/platform/SetupPortal.cpp` `startServer()` :209, `registerRoutes()` :224-233, `handleWifi()` :398, `handleToken()`, `handleTime()`, `handlePack()`, `handleFinish()`, `sendPage()`; `firmware/src/platform/SetupPortal.h`; `firmware/src/app/SetupScreen.cpp` (show the code); `firmware/test/test_wk/test_wk.cpp`
- **Settings:** none

**Problem / goal.** `SetupPortal.cpp:225-230` registers `/wifi`, `/token`, `/time` and `/finish` without any check: while the portal is open, any LAN host — or a cross-site form POST from the user's browser — can call them. `/time` sets the clock used for `created_at` on reviews.

**Behaviour.** The station page first asks for the 6-digit code shown on the device screen. A correct code unlocks the page for that browser for the rest of the portal session; every POST without that unlock gets 403 and the code form again. The code is new per portal start. The AP page (192.168.4.1) is already protected by the WPA2 key (FIX-07) and keeps working as today.

**Design.**
- Generate the code in `startServer()` with `makeRandomCode()` (FIX-07) over `0123456789`; store `sessionCode_[7]` and a random 16-char `sessionCookie_`.
- `POST /unlock` with a visible `code` field (the user types it from the device screen): on match, set `Set-Cookie: kk=<sessionCookie_>; HttpOnly; SameSite=Strict; Path=/` and redirect to the page; after 5 wrong codes, regenerate the code and show the new one on the device.
- `bool unlocked()` checks the `Cookie` header (`server_->collectHeaders`); every state-changing handler (`/wifi`, `/token`, `/time`, `/pack`, `/finish`) returns 403 with the unlock form when it fails. The page must never contain the code itself (a LAN host could read it with a GET).
- `SameSite=Strict` also blocks cross-site form posts from the user's own browser.
- Show the code on the device next to the URL in `SetupScreen.cpp`.

**Tests.** Native: `random_code_digits_only`, `random_code_length` (pure helper with an injected `rnd`). Hardware: `curl -X POST /wifi` without the cookie → 403; unlocking with the on-screen code → the form works; a GET of the page does not contain the code; a second portal start rejects the old cookie.

**Docs.** `docs/setup.md` and `docs/ux.md` §5.2 (code on screen); `docs/api-sync.md` not needed.

**Acceptance.**
- [ ] Every state-changing route on the station page requires the unlock cookie.
- [ ] The code is shown only on the device screen, never in the served page or the URL.
- [ ] A wrong code, a missing cookie or an old session returns 403.
- [ ] The cookie is `HttpOnly; SameSite=Strict`.

### FIX-20 — OTA marked valid after only 10 s of loop

- **Priority:** P2
- **Size:** S
- **Depends on:** none
- **Touches:** `firmware/src/app/App.cpp` `firmwareTick()` :3335-3344, `firmwareBeforeSleep()` :3346-3352, rollback check :3288-3306; `firmware/src/platform/WkClient.cpp`/`.h` (success flag); `firmware/src/platform/Ota.cpp` `saveFailedVersion()` :545, `Ota.h` :91
- **Settings:** none

**Problem / goal.** `firmwareTick()` (`App.cpp:3335-3344`) calls `kk::ota::markValid()` after 10 s of `loop()`, and `firmwareBeforeSleep()` (:3346-3352) does it on any sleep. An image that boots but cannot do TLS is kept; powering off within 10 s of the first boot rolls back and blacklists that version for good (`App.cpp:3302-3304` writes `failedVersion`).

**Behaviour.** A new image is marked valid after the first successful API request or 10 minutes of loop, whichever comes first. A version is blacklisted only after two failed first boots of the same version; the first rollback offers it again.

**Design.**
- Add `bool wkHadApiSuccess()` in `WkClient.cpp`, set on any 2xx status line; `firmwareTick()` marks valid when it is true or `now - gOtaLoopAt >= 600000` (10 min).
- `firmwareBeforeSleep()` uses the same rule instead of marking unconditionally.
- Blacklist: keep a fail count for the rolled-back version in NVS (`fwfailed` + a count key ≤15 chars); on rollback, count 1 → keep the offer (no `failedVersion`), count 2 → `saveFailedVersion()` and stop offering; clear the count when that version later boots and reaches valid.
- Extract the decision as a pure helper in `ota/` (e.g. `bool shouldBlacklist(prevVersion, prevCount, rolledBackVersion)`) so it is testable natively.

**Tests.** Native: `ota_first_rollback_not_blacklisted`, `ota_second_rollback_blacklists`, `ota_other_version_resets_count`. Hardware: install an image that cannot do TLS → it is not marked valid and rolls back; after the second failed boot it is no longer offered; a good image marks valid on the first API request before 10 minutes.

**Docs.** `docs/ota.md` (verification window and the two-strike rule); `docs/api-sync.md` not needed.

**Acceptance.**
- [ ] `markValid()` runs after the first API success or 10 minutes, not after 10 s.
- [ ] Sleep applies the same rule.
- [ ] Two failed first boots are needed to blacklist a version.
- [ ] The policy has native tests.

### FIX-21 — LittleFS formats on any mount failure

- **Priority:** P3
- **Size:** S
- **Depends on:** none
- **Touches:** `firmware/src/app/App.cpp` setup :4852-4860, new state flag + screen + key handling in the main loop
- **Settings:** none

**Problem / goal.** `App.cpp:4852` calls `LittleFS.begin(true)`, which silently formats on any mount failure and wipes unsent reviews.

**Behaviour.** The device mounts without formatting first. On failure it shows "Storage unreadable" / "Pending reviews cannot be read." / `Enter: format  Esc: continue`; `Enter` formats and loads the outbox, `Esc` continues without storage (no outbox, no cache) and the device says so. No sync or review runs before the choice.

**Design.**
- `LittleFS.begin(false)`; on failure set `gFsFormatPrompt = true` and skip `outboxLoad()`/cache load.
- New screen drawn from the boot state, using the existing loading/error helpers; `Enter` → `LittleFS.begin(true)`, then `kk::outboxLoad(gOutbox)` and `saveCacheSnapshot()` guards; `Esc` → continue with `gFsOk = false` and make `saveOutbox()`/cache writes no-ops.
- Block `startSync()` and `startReview()` while the prompt is up.

**Tests.** Hardware (dev build): corrupt or unmount the LittleFS partition → the prompt appears, the outbox is untouched until `Enter`, and `Esc` continues without storage; a normal boot shows no prompt.

**Docs.** `docs/ux.md` §5.15 (errors and empty states); `docs/api-sync.md` §7.2 (atomic writes).

**Acceptance.**
- [ ] No format on the first mount attempt.
- [ ] The choice is explicit and explained on screen.
- [ ] `Esc` continues without storage and says so.
- [ ] A normal boot shows no prompt.

### FIX-22 — Outbox save rename order

- **Priority:** P3
- **Size:** S
- **Depends on:** none
- **Touches:** `firmware/src/platform/OutboxStore.cpp` save :110-140 (:132-134)
- **Settings:** none

**Problem / goal.** `OutboxStore.cpp:132-134` renames main → `.bak`, then tmp → main. A power loss between the two renames restores the previous state and drops the just-queued review. LittleFS `rename` replaces the destination atomically.

**Behaviour.** A save either keeps the previous outbox or installs the new one; the just-queued review is never lost.

**Design.**
- If `kPath` exists, copy it to `kBak` (open both, 512 B chunks, check the byte count), close, then `LittleFS.rename(kTmp, kPath)` — the rename replaces the old file in one step.
- Keep the load fallback to `.bak` unchanged.
- Keep the existing header/CRC write (`put32` at :122) and the tmp-file cleanup on failure.

**Tests.** Native round-trip tests unchanged. Hardware: cut power during a save in a dev build (add a delay between copy and rename) → the outbox loads with either the old or the new record, never a missing one.

**Docs.** `docs/api-sync.md` §7.2 (atomic writes).

**Acceptance.**
- [ ] `main` is copied to `.bak` before `tmp` replaces `main`.
- [ ] A power cut between the two steps keeps the previous outbox.
- [ ] The loader still falls back to `.bak`.

### FIX-23 — Small UX bugs

- **Priority:** P3
- **Size:** S
- **Depends on:** none
- **Touches:** `firmware/src/app/App.cpp` key handler :2493-2500, `startReview()` :1438-1500 (:1453-1459, :1466, :1477, :1496-1499), `startLessons`/lesson error :1919, `drawStatus()` :4250-4260, main loop; `firmware/src/app/HomeScreen.cpp`
- **Settings:** none

**Problem / goal.** (a) `Enter` on Home with 0 due runs a network flush and ends on "Batch: no due subject ids" (`App.cpp:2495-2497` → `startReview()` → `runBatch()` :786). (b) "Fetching reviews… 20 due items" uses the batch size, not the due count (`App.cpp:1453-1459`). (c) "Could not load reviews" and "No lessons available" are drawn with `drawLoading()` and no key hint (`App.cpp:1466`, `:1919`). (d) "Starting session…" stays on screen when the queue is empty (`App.cpp:1477`, `:1496-1499` returns without `gDirty`). (e) Home battery % never refreshes because `drawStatus()` returns early on Home (`App.cpp:4250-4255`).

**Behaviour.** (a) `Enter` with 0 due shows "No reviews now · next at HH:MM" with `Enter: sync  Esc: home` and makes no network call. (b) The subtitle shows the real due count. (c) The two errors are real states with `Enter: retry  Esc: home`. (d) An empty queue shows the empty state instead of "Starting session…". (e) Home redraws its status line (battery, clock) at least every 30 s.

**Design.**
- (a) In the Home `Enter` handler, check `gSummary.dueReviews(effectiveNow())` first; if 0, open the existing lessons-style empty state with the next review time from `gSummary.nextReviewAt()`.
- (b) Count the due ids (`gSummary.dueReviews(now)`) for the subtitle.
- (c) Replace the two `drawLoading()` calls with a state flag and a drawn screen, following `docs/ux.md` §5.15.
- (d) When `gQueue.empty()`, set `gDirty` and show the empty state instead of returning silently.
- (e) Let the main loop mark `gDirty` on a 30 s timer while `gShowHome`, or draw the status line from the loop instead of `drawStatus()`.

**Tests.** Hardware, one check per sub-bug: 0 due + `Enter` → no request, empty state with the next time; the subtitle shows the due count; a failed review fetch and a failed lesson fetch show the key hints; an empty queue leaves no "Starting session…"; Home battery changes when the charger is plugged in.

**Docs.** `docs/ux.md` §5.3 (Home), §5.15 (errors and empty states); ROADMAP row.

**Acceptance.**
- [ ] 0 due on Home does not start a network review fetch.
- [ ] The fetch subtitle uses the due count.
- [ ] Both load errors name their keys.
- [ ] An empty queue leaves the empty state, not "Starting session…".
- [ ] Home status refreshes without a keypress.

### FIX-24 — Latent robustness

- **Priority:** P3
- **Size:** S
- **Depends on:** none
- **Touches:** `firmware/src/board/BoardCardputerAdv.cpp` `push()` :69-78; `firmware/src/platform/GlyphPack.cpp` `open()` :41-90; `firmware/src/app/App.cpp` asset upload :4347-4400
- **Settings:** none

**Problem / goal.** (a) In `push()` (`BoardCardputerAdv.cpp:69-78`) the overflow path advances the head, decrements the count, then writes the "events lost" marker at `(gQueueHead + gQueueCount) % kQueueLen` (:77) — after the decrement that is the slot just dropped, so the marker is never delivered. (b) `GlyphPack::open()` (`GlyphPack.cpp:41-90`) never checks the header CRC-32 (field at offset 24, over table + payload per `tools/glyph-pack/build.py:197-214`). (c) The Wi-Fi asset upload (`App.cpp:4359-4388`) ignores `f.write()` results and writes in place, so an interrupted upload leaves a broken pack.

**Behaviour.** A key-queue overflow delivers the lost-events marker as the newest event. A pack with a bad CRC is rejected with "pack: bad crc" instead of drawing garbage. An interrupted asset upload leaves the previous file intact.

**Design.**
- (a) After the head++/count--, write the marker at `(gQueueHead + gQueueCount) % kQueueLen` and increment `gQueueCount` back to `kQueueLen`, so the marker is the tail event. The main loop already handles `Key::None` (`App.cpp:4930`).
- (b) Add a CRC check in `open()` after the size checks: stream `[tableOffset_, dataOffset_ + dataLength_)` in 1 KB chunks with `wk/Crc32.h` and compare to the header field at offset 24; fail with "pack: bad crc". Measure the time on hardware and, if it exceeds ~1 s, move it behind the existing pack check instead of the boot path (verify).
- (c) Upload to `path + ".tmp"`, check every `f.write()` returns `n` (on short write: 500, remove the tmp, keep the old file), then close, remove the old file and rename the tmp over it. Close `gPack` only after a successful rename.

**Tests.** No native test (Arduino board, SD and `LittleFS` classes). Hardware/serial: hold a key through a key storm → the "events lost" marker reaches the loop (log it in a dev build); flip one byte in a pack payload → "pack: bad crc" and the old pack is kept on SD; cut power during a Wi-Fi upload → the previous pack still opens.

**Docs.** `docs/hardware.md` (keyboard queue and pack verify timing); `docs/setup.md` (asset upload is atomic).

**Acceptance.**
- [ ] The overflow marker is delivered as the newest event.
- [ ] `GlyphPack::open()` verifies the stored CRC-32.
- [ ] The asset upload writes to a tmp file and checks every write.
- [ ] An interrupted upload leaves the previous file intact.

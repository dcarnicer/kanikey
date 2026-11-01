# Settings menu and registry

Decision D3 turns Settings into a sectioned menu backed by the ARCH-02 registry, with every default faithful to WaniKani or to today's behaviour. Today the screen is 13 flat rows built in `drawSettings()` and driven by a `switch (gSettingsIndex)`; this file replaces that with named sections, one table per section, a reset that keeps the account, and a `?` overlay so no key stays invisible. Kakehashi's `store.ts` was used only as a reference list of options, filtered for a keyboard device. CFG-01 and CFG-02 touch the same Settings controller file, so they land on one branch (`spec/CFG-settings-menu`), tables first.

| ID | Title | Priority | Size | Depends on | Wave | Status |
|---|---|---|---|---|---|---|
| CFG-01 | Sectioned settings menu | P1 | M | ARCH-01, ARCH-02 | 2 | todo |
| CFG-02 | Settings registry of record | P1 | M | ARCH-02 | 2 | todo |
| CFG-03 | Reset settings to defaults | P2 | S | CFG-02 | 2 | todo |
| CFG-04 | Key help overlay | P2 | S | CFG-01 | 2 | todo |

### CFG-01 — Sectioned settings menu

- **Priority:** P1
- **Size:** M
- **Depends on:** ARCH-01, ARCH-02
- **Touches:** the Settings controller file created by ARCH-01 (today in `firmware/src/app/App.cpp`: state `:189-194`, `kSettingsRows` `:2074`, `setSettingsNote()` `:2076-2079`, `openSettings()` `:2081-2089`, `closeSettings()` `:2090-2106`, `settingsAction()` `:2133-2223`, `settingsKey()` `:2225-2301`, `drawSettings()` `:3140-3183`, `onKey()` `:2395-2439`, `draw()` `:4166-4194`), `firmware/src/app/SettingsScreen.{h,cpp}`, `firmware/src/app/FirmwareScreen.cpp` and `firmwareKey()` (`App.cpp:3661-3703`)
- **Settings:** reads every entry added by CFG-02 (R1–R12, L1–L6, S1–S5, Y1–Y3, P1–P6, M1–M7) plus the Account and System actions

**Problem / goal.** Settings is one flat list of 13 rows (`drawSettings()`, `App.cpp:3140-3183`) driven by `switch (gSettingsIndex)` in `settingsAction()` (`:2133`) and `settingsKey()` (`:2225`). Enter cycles a hardcoded preset array and there is no level between "menu" and "change value"; the bottom line only carries transient notes. CFG-02 grows the list to ~40 rows, which no longer fits one screen. Worse, the destructive rows are reachable from Pause › Settings mid-review: only Setup portal checks `gReviewActive` (`:2187-2189`), while Clear cache (`:2211-2216`), Sign out (`:2217-2220`) and Firmware install (`:3687-3700`) do not.

**Behaviour.** The top level lists the eight sections: `Reviews`, `Lessons`, `Study`, `Sync & data`, `Display & power`, `Reminders & sound`, `Account`, `System`, each with a short right-aligned summary (for example `12 settings`, or the Account token state). `Up`/`Down` move, `Enter` opens a section, `Esc` from a section returns to the top level, `Esc` at the top level closes Settings to its origin (Home or Pause, as `closeSettings()` does today). Inside a section each row shows the label left and the current value right; `Up`/`Down` move, `Enter` or `Right` selects the next choice, `Left` the previous, wrapping at both ends; `Esc` goes up one level and never closes in one step. The bottom line shows the selected row's help text from the registry; a transient note (`setSettingsNote()`) replaces it until the selection moves. The 7-row window and scroll thumb stay as they are. While `gReviewActive` (`App.cpp:105`, set at `:1500`), the rows `Setup portal`, `Firmware › Install update`, `Sign out`, `Clear cache` and `Reset settings` stay focusable but are drawn faint, show `End the review session first` on the help line and do nothing on `Enter`; the `Firmware` row itself stays enabled so `Check now` is reachable. Opening Setup portal still closes Settings. Section values are always drawn from the registry, so a row with an invalid stored value shows the default.

**Design.**
- Replace `gSettingsIndex` (`App.cpp:193`) with `gSettingsSection` (`-1` = top level) and `gSettingsRow`; keep `gSettingsOrigin` and `gSettingsNote`.
- Build the top level from the registry's section list and a section from its entries; no label, choice or default string stays in `drawSettings()` or `settingsAction()`.
- Extend `SettingsView` (`SettingsScreen.h:19-24`) with `title`, `help` and a per-row `disabled`; reuse the existing geometry (rows at `42 + row * 11`, thumb `SettingsScreen.cpp:55-64`) and draw the help line at `y=123` when `note` is empty (`:66-69`).
- Dispatch actions by the entry's action field instead of `case 0..12`; keep `setSettingsNote()` for confirmations such as `Cache cleared; next boot is cold`.
- One `settingsRowLocked(entry)` helper decides the review lock; `firmwareKey()` uses the same helper for the Install row and draws the reason in its note.
- The serial back paths that hardcode `gSettingsIndex = 9` (`App.cpp:3673`, `:4702`) select the Firmware entry by id.
- Cost: two ints and the existing row vector; no new heap or buffers.

**Tests.** Native: none of its own — the menu state lives in the app controller and `pio test -e native` compiles only `src/wk` and `src/ota` (`firmware/platformio.ini:24-26`); it relies on CFG-02's `test_settings_cycle_next_prev_wraps`. Hardware with `tools/device.py` and `tools/shot.py`: `settings_sections_open_and_esc_back` (Home → Esc → Reviews → Esc → Esc), `settings_cycle_both_ways` (`Enter` and `Right` forward, `Left` back, wrap at both ends), `settings_review_lock` (Pause → Settings, all five rows inert with the note), `settings_scroll_thumb` (a section with more than 7 rows), `settings_origin_return` (close from Pause returns to Pause).

**Docs.** `docs/ux.md` §5.12 (rewrite the rows and the key list), §3.1 key table (`Enter`/`Left`/`Right`/`Esc` in Settings), `docs/decisions.md` new dated entry superseding the row list in "2026-10-28 — Pause and settings are local-only", `ROADMAP.md` "Where we are" and the SETUP-05 line.

**Acceptance.**
- [ ] The top level lists the eight sections with a summary; `Enter` opens one and `Esc` pops exactly one level.
- [ ] `Up`/`Down` move, `Enter`/`Right` select the next value, `Left` the previous, both wrapping; the value is written to NVS at once.
- [ ] The bottom line shows the registry help of the focused row, and a transient note until the selection moves.
- [ ] During a review, the five listed rows are faint, show `End the review session first` and change nothing; `Firmware › Check now` still works.
- [ ] The scroll thumb still appears past 7 rows and no label/value pair is clipped at 240 px.
- [ ] Closing from Pause returns to Pause, from Home to Home.

### CFG-02 — Settings registry of record

- **Priority:** P1
- **Size:** M
- **Depends on:** ARCH-02
- **Touches:** `firmware/src/wk/SettingsRegistry.cpp` (the tables; ARCH-02 creates it with today's entries and `wk/SettingsModel` with the types and functions) and `firmware/src/platform/DeviceSettings.{h,cpp}` for storage: keys `:13-21`, defaults `:23-31`, load `:33-54`, `saveInt()` `:56-62`, setters `:114-173`), `firmware/test/test_wk/test_wk.cpp`
- **Settings:** adds R2–R12 except R4 (migrated), L2–L6, S1–S5, Y1–Y3, P5–P6, M1–M7; keeps R1, R4, L1 and P1–P4 under their existing keys

**Problem / goal.** Settings are scattered across typed getters (`DeviceSettings.h:23-38`, `.cpp:13-31`) and hardcoded preset arrays and labels inside `settingsAction()` (`App.cpp:2133-2223`) and `drawSettings()` (`:3144-3162`). Nothing states label, choices, default, storage key and owner in one place: `order` means meaning-first while `revsize` means review batch, and `order` collides with the new review-order setting. The ~30 options in this brief would repeat that pattern, and a wrong default or a 16-character NVS key would only show up on the device.

**Behaviour.** The registry is invisible to the user except through CFG-01; its contract is the tables below. Defaults are WaniKani's where WaniKani has the preference, otherwise today's behaviour. Existing keys keep their names, so a device upgrading from v0.1.0 keeps brightness, timings, batch sizes and meaning-first. A value change writes NVS once and applies without a reboot; nothing here calls WaniKani (`docs/decisions.md`, 2026-10-28). `50` in R1 stays selectable but is only usable once FIX-08 lands.

**Design.**
- One `constexpr` table per section using ARCH-02's `SettingDef` (`wk/SettingsModel.h`); the `id` field is the ID column below (`R1`…`M7`; actions get descriptive ids such as `sync.now`). Each entry carries label, choices as (value, label) pairs, default value, NVS key, live flag, owner task (comment) and a one-line help string. Section order is the menu order CFG-01 renders.
- Keep the existing typed getters (`settingsBrightness()` etc.) as the read API; new settings use ARCH-02's generic `settingsGetInt(nvsKey)` / `settingsSetInt(nvsKey, value)` (same `Preferences` namespace `kanikey`, same in-RAM cache). Every key stores the choice's int value, as today's keys do (seconds, brightness level, batch size), booleans as 0/1 — never a choice index, so reordering or adding choices never changes a stored meaning.
- Validated by construction and by tests: keys unique, `<= 15` characters, `[a-z0-9._-]`; default inside the choice list; every entry has help text of at most 38 characters (one Font0 line in 228 px, the help line CFG-01 draws at `y=123`; also fits the 48-byte `gSettingsNote`, `App.cpp:220`).
- R4 migration: read `rv.first`; if absent, map the stored `order` int (`DeviceSettings.cpp:21,46`) to Meaning (1) or Reading (0). Write `rv.first` on the next change and leave `order` in place.
- R1 keeps `revsize` and the 5..50 clamp (`DeviceSettings.cpp:151-157`); the menu offers 10/20/30/50. L1 keeps `lessonsz` and the WaniKani clamp in `startLessons()` (`App.cpp:1890-1892`).
- Actions have no NVS key and no default; CFG-01 dispatches them.
- Pure tables live under `firmware/src/wk/` so the native env compiles them (`firmware/platformio.ini:24-26`).
- Flash/RAM: static const tables only, no heap, no library — inside the 250 KB round budget.

The tables below are the registry (one per section; `When` is `live`, or `live (next session)` where the value is sampled when a session starts):

**Reviews**

| ID | Label | Choices | Default | NVS key | When | Owner |
|---|---|---|---|---|---|---|
| R1 | Batch size | 10 / 20 / 30 / 50 | 20 | `revsize` (existing) | live (next session) | CFG-02 (existing; 50 needs FIX-08) |
| R2 | Order | Account preference / Random / Lowest level first / Current level first / SRS ascending / SRS descending / Oldest available first | Account preference | `rv.order` | live (next session) | REV-08 |
| R3 | Type order | Off / Radical→Kanji→Vocab | Off | `rv.types` | live (next session) | REV-08 |
| R4 | First question | Meaning / Reading / Random | Meaning | `rv.first` (migrates `order`) | live (next session) | CFG-02 |
| R5 | Back-to-back | On / Off | On (today) | `rv.b2b` | live (next session) | REV-09 |
| R6 | Typos | WaniKani (accept close) / Strict (retry) | WaniKani | `rv.typos` | live (next session) | REV-02 |
| R7 | Wrong answer actions | Off / On | Off | `rv.wrong` | live (next session) | REV-04 |
| R8 | Auto-advance on correct | Off / 0.5 s / 1 s / 2 s | Off | `rv.advance` | live (next session) | REV-06 |
| R9 | Details on wrong | Off / On | Off | `rv.details` | live (next session) | REV-07 |
| R10 | Header info | SRS+accuracy / Minimal | SRS+accuracy | `rv.header` | live | REV-12 |
| R11 | Prioritize critical | Off / On | Off | `rv.critical` | live (next session) | STUDY-02 |
| R12 | Submit reviews | At session end / Every 5 / Immediately | At session end | `rv.submit` | live (next session) | SPEED-01 |

**Lessons**

| ID | Label | Choices | Default | NVS key | When | Owner |
|---|---|---|---|---|---|---|
| L1 | Batch size | 3 / 5 / 10 | 5 | `lessonsz` (existing) | live (next batch) | CFG-02 (clamped to `lessons_batch_size`) |
| L2 | Order | WaniKani / Radicals→Kanji→Vocab / Interleave types | WaniKani | `ls.order` | live (next batch) | LES-04 |
| L3 | Lesson quiz | Required / Skip | Required | `ls.quiz` | live (next batch) | LES-02 |
| L4 | Apprentice limit warning | Off / 100 / 150 / 200 | Off | `ls.appr` | live | LES-05 |
| L5 | Due reviews warning | Off / 50 / 100 / 200 | Off | `ls.due` | live | LES-05 |
| L6 | Daily lesson limit | Off / 5 / 10 / 15 / 20 | Off | `ls.daily` | live | LES-05 |

**Study**

| ID | Label | Choices | Default | NVS key | When | Owner |
|---|---|---|---|---|---|---|
| S1 | Critical below | 60 / 70 / 75 / 80 / 90% | 75% | `st.critpct` | live | STUDY-01 |
| S2 | Critical minimum reviews | 3 / 5 / 10 | 5 | `st.critmin` | live | STUDY-01 |
| S3 | Recent lessons | Apprentice / 24 h / 3 d / 7 d | Apprentice | `st.recent` | live | STUDY-03 |
| S4 | Recent mistakes | 24 h / 3 d / 7 d | 24 h | `st.mistakes` | live | STUDY-03 |
| S5 | Practice size | 10 / 20 | 10 | `st.practice` | live | STUDY-03 |

**Sync & data**

| ID | Label | Choices | Default | NVS key | When | Owner |
|---|---|---|---|---|---|---|
| Y1 | Auto sync on wake | Quick / Full / Off | Quick | `sy.wake` | live | SPEED-02 |
| Y2 | Full sync every | 1 h / 6 h / 24 h | 6 h | `sy.full` | live | SPEED-02 |
| Y3 | Prefetch reviews | On / Off | On | `sy.prefetch` | live | SPEED-04 |
| — | Sync now | action | — | — | — | CFG-01 calls `startSync()` (`App.cpp:2691`) |
| — | Outbox | action | — | — | — | CFG-02 opens the list even with nothing to show (drop the guard in `openOutbox()`, `App.cpp:1288-1289`; the empty state already exists in `OutboxScreen.cpp:54-63`) |
| — | Clear cache | action | — | — | — | CFG-01 (`kk::cacheClearSnapshot()` / `kk::cacheClearSubjects()`, `App.cpp:2212-2214`) |

**Display & power**

| ID | Label | Choices | Default | NVS key | When | Owner |
|---|---|---|---|---|---|---|
| P1 | Brightness | 64 / 128 / 192 / 255 | 128 | `bright` (existing) | live | CFG-02 |
| P2 | Dim after | Off / 15 s / 30 s / 60 s | 30 s | `dim` (existing) | live | CFG-02 |
| P3 | Blank after | Off / 30 s / 60 s / 120 s | 60 s | `blank` (existing) | live | CFG-02 |
| P4 | Sleep after | Off / 120 s / 300 s / 600 s | 120 s | `sleep` (existing) | live | CFG-02 |
| P5 | Light sleep | On / Off | On | `dp.lightsleep` | live | SPEED-06 |
| P6 | Sleep during review | Never / After 10 min | Never | `dp.revsleep` | live | SPEED-06 |

**Reminders & sound**

| ID | Label | Choices | Default | NVS key | When | Owner |
|---|---|---|---|---|---|---|
| M1 | Reminder | Off / ≥1 due / ≥10 due / ≥25 due / ≥50 due | Off | `rm.remind` | live | REM-02 |
| M2 | Quiet hours | Off / 22-08 / 23-07 | 22-08 | `rm.quiet` | live | REM-02 |
| M3 | Daily reminder | Off / 08:00 / 12:00 / 18:00 / 20:00 | Off | `rm.time` | live | REM-02 |
| M4 | Sounds | Off / On | Off | `rm.sound` | live | REM-03 |
| M5 | Volume | Low / Medium / High | Medium | `rm.volume` | live | REM-03 |
| M6 | Autoplay audio | Off / On | Off | `rm.autoplay` | live | AUD-03 |
| M7 | Voice | Female / Male | Female | `rm.voice` | live | AUD-03 |

**Account**

| ID | Label | Choices | Default | NVS key | When | Owner |
|---|---|---|---|---|---|---|
| — | Setup portal | Wi-Fi, token, time | action | — | — | CFG-01 (existing `startSetupPortal()`, locked in review `App.cpp:2187-2189`) |
| — | Sign out | Shows the number of unsent reviews and offers to sync first | action | — | — | CFG-02 (count from `kk::outboxPendingCount()`, `wk/Outbox.h:47`; `drawSignOutScreen()` `ModalScreens.cpp:81-107` becomes a view with `pending` and a `Sync first` row) |

**System**

| ID | Label | Choices | Default | NVS key | When | Owner |
|---|---|---|---|---|---|---|
| — | Firmware | running version / update badge, `Check now`, `Install update` | action | — | — | CFG-01 (`firmwareKey()`, `App.cpp:3661-3703`) |
| — | About | version, slot, build, pack, cache, sync, Wi-Fi, token, zone, heap | action | — | — | CFG-02 (`drawAbout()`, `App.cpp:3706-3757`) |
| — | Reset settings | see CFG-03 | action | — | — | CFG-03 |

**Tests.** Native cases in `firmware/test/test_wk/test_wk.cpp`: `test_settings_keys_unique_and_short`, `test_settings_defaults_are_in_choices`, `test_settings_sections_cover_every_entry`, `test_settings_review_defaults_match_wanikani`, `test_settings_lesson_defaults`, `test_settings_cycle_next_prev_wraps`, `test_settings_meaning_first_migration`, `test_settings_help_fits_one_line`, `test_settings_actions_have_no_key`. Hardware: change one value per section, power-cycle and confirm it stuck; a fresh NVS shows the Default column; R4 keeps the stored meaning-first after an upgrade from v0.1.0 (`About`/review first question).

**Docs.** `docs/decisions.md` new dated entry (D3 registry, key scheme, R4 migration, defaults), `docs/ux.md` §5.12 (default table and per-section rows), `ROADMAP.md` "Where we are" and the SETUP-05 line. `docs/api-sync.md` unchanged: R2 `Account preference` still means `reviews_presentation_order` as parsed in `wk/User.cpp:35-42`.

**Acceptance.**
- [ ] Every row of the eight tables exists in the firmware with exactly that key, choice list and default.
- [ ] All keys are unique, ≤ 15 characters, match `[a-z0-9._-]`; the six existing keys keep their names.
- [ ] The existing getters keep working and a stored `order = 1` reads back as R4 `Meaning`.
- [ ] Changing a value writes NVS once, applies without a reboot and sends no request to WaniKani.
- [ ] `pio test -e native` passes with the new cases and `pio run -e cardputer-adv` stays inside the flash budget.

### CFG-03 — Reset settings to defaults

- **Priority:** P2
- **Size:** S
- **Depends on:** CFG-02
- **Touches:** the Settings controller (today `settingsAction()` `App.cpp:2133-2223` and `settingsKey()` `:2225-2301`), `firmware/src/platform/DeviceSettings.{h,cpp}` (`:33-54`, `:114-173`), `firmware/test/test_wk/test_wk.cpp`
- **Settings:** none of its own; clears every key listed in CFG-02

**Problem / goal.** There is no way back to the shipped defaults; a user who has changed dozens of values must find each one. The obvious shortcut is wrong: sign-out deliberately keeps Wi-Fi but deletes token, cache and outbox (`App.cpp:2240-2280`), and `Preferences::clear()` on the `kanikey` namespace would delete the token (`DevToken.cpp:8,13-19`) and the time-zone rule and name (`DeviceSettings.cpp:13-14`), which share that namespace.

**Behaviour.** `System › Reset settings` shows `Reset settings? Enter again, Esc cancels` in amber. A second `Enter` erases the setting keys, redraws every row with its CFG-02 default, applies brightness and the sleep timers at once, sets the note `Settings reset` in green and does not restart. `Esc`, `Up`, `Down` or leaving Settings cancels the prompt. Token, Wi-Fi, the time zone, the cached batch on SD and the outbox in LittleFS are untouched, so the next boot still shows Home offline. During a review the row is locked by CFG-01, so no session state has to be reloaded.

**Design.**
- Add (proposed) `settingsResetAll()` to `platform/DeviceSettings`: iterate the CFG-02 keys and call `Preferences::remove(key)` — never `clear()` — then set the `DisplaySettings` static cache's `loaded` flag back to false (`DeviceSettings.cpp:33-36`) so the getters return the defaults immediately; invalidate or rebuild anything else that caches a value.
- Reuse the two-step confirm pattern of Firmware install (`firmwareKey()`, `App.cpp:3687-3693`) with one `gResetConfirm` flag cleared on move, `Esc` and in `closeSettings()` (`:2090`).
- After the erase call `tgc::board::setBrightness(kk::settingsBrightness())`, as the brightness row does (`:2138`).
- Leave `token`, `tzrule`, `tzname`, LittleFS `/state/outbox.bin` and SD `/kanikey/cache/*.bin` alone; no change to any on-disk format.

**Tests.** Native: `test_settings_reset_covers_every_key` (the reset list equals the registry keys, minus `token`/`tzrule`/`tzname`). Hardware with `tools/device.py`: `settings_reset_twice_confirms`, `settings_reset_esc_cancels`, `settings_reset_keeps_account` (token, Wi-Fi and zone still in `About`; cached Home after a power cycle; outbox count unchanged), `settings_reset_applies_brightness`, `settings_reset_locked_in_review`.

**Docs.** `docs/ux.md` §5.12 (Reset settings row and confirmation), `ROADMAP.md` SETUP-05 line. `docs/decisions.md` only if the reset semantics differ from the CFG-02 entry.

**Acceptance.**
- [ ] `System › Reset settings` needs a second `Enter`; `Esc` or any move cancels without a write.
- [ ] After a reset every row shows the CFG-02 default and the device does not restart.
- [ ] Token, Wi-Fi, time zone, cached batch and outbox survive the reset and a power cycle.
- [ ] Brightness and the display timers follow the defaults immediately.
- [ ] The row cannot be triggered while a review is active.

### CFG-04 — Key help overlay

- **Priority:** P2
- **Size:** S
- **Depends on:** CFG-01
- **Touches:** `firmware/src/app/App.cpp` (`gShift` `:558`, `updateModifier()` `:588-596`, `onKey()` `:2395-2439`, `draw()` `:4166-4194`) or the controller files ARCH-01 creates, `firmware/src/app/ModalScreens.{h,cpp}` (next to `drawSignOutScreen()`, `.cpp:81-107`), `firmware/src/app/HomeScreen.cpp` (`:117-126`), `firmware/test/test_wk/test_wk.cpp`
- **Settings:** none

**Problem / goal.** Shortcuts are invisible. Home's hint shows three actions (`HomeScreen.cpp:117-126`) while `Esc` opens Settings (`App.cpp:2491-2494`), and the hint drops more entries when the outbox needs attention; `docs/ux.md` §1 principle 3 requires that hidden shortcuts be listed in the screen's help. No such help exists. `?` is also non-obvious: `/` is the Right arrow (`ui/KeyInput.h:68-74`), so the key arrives as `Key::Right`, not `Key::Other`, with `x=12, y=3` and `gShift` set (`App.cpp:558,588-596`); `cardputerCharAt(12, 3, true)` returns `'?'` (`KeyInput.h:119-124`).

**Behaviour.** `Shift` + `/` opens a help overlay on any screen that has a key table. The title is the screen name (`home`, `settings`, `reviews`, `lessons`, `outbox`, `firmware`); each line is a key chord left and its action right, at most 7 lines; the bottom line reads `Esc: close`. `Esc` closes and nothing else reaches the screen underneath; the overlay is not offered while the setup portal, a low-battery prompt or the sign-out confirm owns the keyboard. In an answer field `?` still opens the overlay — it is not a valid answer character — and closing returns to the same item. Home's hint becomes `Enter: reviews  L  S  Esc: menu` and, when the outbox needs attention, `O: outbox  Enter  S  Esc: menu`, with the existing `d.textWidth() > 224` fallbacks behind them.

**Design.**
- `struct HelpEntry { const char* keys; const char* action; };` plus one static table per screen next to that screen's key handler, and a `helpFor(screen)` returning table and count; no dynamic allocation.
- `gHelpActive` and `gHelpScreen` in the app; intercept in `onKey()` after `updateModifier()` (`:2395-2396`) and after the setup-portal branch: `e.down && gShift && e.x == 12 && e.y == 3` (the `/` key), before any screen handler. Do not match on `e.key == Key::Other`; plain `/` must keep meaning Right.
- `drawHelpScreen(d, view)` in `ModalScreens.cpp`, called from `draw()` at the same point as `gSignOutConfirm` (`:4175-4177`), above the screen's own frame.
- Home hint: extend the existing string and width-fallback chain (`HomeScreen.cpp:117-126`), keeping `O: outbox` first when it applies.
- No NVS, no new buffers beyond the flag and the screen id.

**Tests.** Native: `test_help_question_mark_is_shift_slash` (assert `cardputerKeyAt(12,3) == Key::Right`, `cardputerCharAt(12,3,true) == '?'`, `cardputerCharAt(12,3,false) == '/'`), `test_help_tables_are_short` (≤ 7 rows per screen, no duplicate chord). Hardware with `tools/shot.py`: `help_opens_on_home`, `help_opens_on_settings`, `help_opens_on_feedback`, `help_esc_closes_only`, `home_hint_fits` (both hint variants ≤ 224 px), `plain_slash_still_right` (paging in lesson/detail is unchanged).

**Docs.** `docs/ux.md` §1 principle 3 (no invisible shortcut, point at `?`), §3.1 key table (add `?` and the Home `Esc: menu` hint), new §3.5 "Help overlay" (chord list per screen), §5.1 Home hint, `ROADMAP.md` "Where we are".

**Acceptance.**
- [ ] `Shift` + `/` opens the overlay on Home, Settings, review feedback, lesson card/quiz, Outbox and Firmware; `Esc` closes it.
- [ ] The overlay lists only keys that work on that screen and never exceeds 7 rows or 224 px.
- [ ] `?` is detected from grid position `12,3` plus `gShift`; plain `/` still acts as Right.
- [ ] No key pressed while the overlay is open reaches the screen underneath.
- [ ] Home shows `Esc: menu` in the hint, in both the normal and outbox variants, without clipping.
- [ ] The setup portal, low-battery prompt and sign-out confirm are unaffected.

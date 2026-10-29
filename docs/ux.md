# KaniKey — UX, keyboard and screens

> Design, 2026-10-25. Mockups are at 240×135: individual ones are shown at 4×
> and the contact sheet at 2×. Geometry and text sizes are the 1× ones; the
> upscale is nearest-neighbour so as not to misrepresent the pixel grid.

![Screens](screens/overview.png)

---

## 1. Principles

1. **The keyboard is the interface.** Typing an answer isn't an odd mode: it's
   the main action. The rest of the app is navigated with arrows and Enter.
2. **One focus per screen.** In the review, the item and the answer. On home,
   the reviews. In the lesson, the mnemonic. Nothing competes.
3. **The bottom bar always says what the keys do.** There are no invisible
   shortcuts. If a key changes meaning, the bar changes.
4. **Color + shape + word.** Correct isn't just green; it has ✓ and the word
   `CORRECT`. Incorrect isn't just red; it has ✗ and `NOT QUITE`.
5. **Offline is stated.** One status line carries the most urgent state, such
   as `Offline - 2h old` or `Outbox: 4 pending`; outbox pending/attention
   replaces the offline/stale/vacation/level line. A level-up notice is shown
   after sync/outbox/offline and before vacation and level details. Never a
   generic error.
6. **Short texts.** The screen is 240×135; long sentences wrap onto several
   lines or move to a detail screen.

---

## 2. Visual language

### 2.1 Palette

| Token | Hex | Use |
|---|---|---|
| `bg` | `#0B0D10` | Background |
| `surface` | `#151A22` | Panels, rows, cards |
| `surface-hi` | `#202733` | Answer field, pills, hints |
| `line` | `#33404F` | 1 px borders, separators |
| `text` | `#F4F7FB` | Main text, kanji |
| `text-dim` | `#9BA8BA` | Secondary text, labels |
| `text-faint` | `#647184` | Metadata, help |
| `accent` | `#6C8CFF` | Focus, progress, `MEANING` |
| `accent-hi` | `#9DB2FF` | Accent on black |
| `brand` | `#FF7A59` | Brand, logo, details |
| `reading` | `#5EEAD4` | Readings, `READING` mode |
| `ok` | `#6EE7A8` | Correct |
| `ok-bg` | `#123326` | Correct background |
| `err` | `#FF6B6B` | Wrong |
| `err-bg` | `#351A1C` | Wrong background |
| `warn` | `#FBBF24` | Warnings, offline, low battery |
| `gold` | `#F5C451` | Level |

The palette is its own: warm black, indigo and coral. WaniKani's colors,
illustrations and components are not copied.

### 2.2 Typography

| Role | Size | Use |
|---|---|---|
| Display | 32 px | Big numbers: reviews, accuracy, time |
| Prompt | 64 / 48 / 32 px | Kanji, radical, vocabulary word |
| Answer | 20 px | Written answer, meaning/reading |
| Title | 18 px | Lesson title, main meaning |
| Body | 14 px | Sentences, mnemonics, values |
| Caption | 10 px | Labels, header, pills |
| Micro | 9 px | Metadata that isn't essential |

The UI uses **Noto Sans JP** (OFL) for Latin and kana; kanji/vocabulary come
from the visual pack. In the firmware, the UI font is pre-rendered as in
Tangocho (`UiFont.h`), so there's no rasterization on the device.

### 2.3 Grid

- Base unit: 4 px.
- Margins: 4 px.
- Header: 16 px (title + 2 px progress or a 1 px line).
- Hint bar: 12 px.
- Pills: 13 px high.
- Answer field: 32 px high.
- List rows: 18–20 px.
- Square corners; no shadows; 1 px borders.

### 2.4 Components

- **Header:** title or brand on the left; Wi-Fi icon, battery and a value on
  the right; 2 px progress bar at the bottom in the review.
- **Pill:** short uppercase label (`MEANING`, `READING`, `CORRECT`,
  `KANJI · L5`) with a `surface-hi` background and semantic color.
- **Input:** `surface-hi` rectangle with a `line` border; 20 px text; 2 px
  caret in `accent`; 2 px `accent` border when the keyboard is in text mode.
- **Keycap:** 11 px square with a border; the key in 8 px; the action next to
  it in 9–10 px.
- **Progress:** 2–3 px bar; `surface-hi` track, `accent` fill.
- **Feedback:** pill + icon + word; never color alone.
- **List row:** `surface` background, `accent` focus, 10 px label, 9 px value,
  chevron.

---

## 3. Keyboard

### 3.1 Physical map

The Cardputer ADV has 4 rows × 14 keys. The main layer is US; Shift gives
uppercase and symbols; Fn gives function.

```
  `  1  2  3  4  5  6  7  8  9  0  -  =  del
 tab q  w  e  r  t  y  u  i  o  p  [  ]  \
 fn  sh a  s  d  f  g  h  j  k  l  ;  '  enter
 ctrl opt alt z  x  c  v  b  n  m  ,  .  /  space
```

| Key | Normal layer | Shift | Fn |
|---|---|---|---|
| `` ` `` | `` ` `` | `~` | `Esc` |
| `1`–`0` | digits | `!@#$%^&*()` | `F1`–`F10` |
| `-` / `=` | `-` / `=` | `_` / `+` | `F11` / `F12` |
| `del` | Backspace | Backspace | Delete |
| `;` | `;` | `:` | `↑` |
| `,` | `,` | `<` | `←` |
| `.` | `.` | `>` | `↓` |
| `/` | `/` | `?` | `→` |
| `'` | `'` | `"` | — |
| `[` `]` `\` | literals | `{ } \|` | — |

### 3.2 Two modes

| Mode | When | What `; , . /` do | What the letters do |
|---|---|---|---|
| **Navigation** | Menus, home, feedback, lessons | Arrows (without Fn) | Context shortcuts (`L`, `O`, `S` on Home; `Q` on a lesson; `D` in Outbox) |
| **Text** | Answer field active | Characters `; , . /` | They type |

In text mode, the arrows are still available with `Fn + ; , . /`. The field is
marked with an `accent` border and caret; the bottom bar reads
`Enter: check` / `Esc: quit`.

When an answer ends (Enter) or is cancelled (Esc), it returns to navigation.
There's no need to press a button to “enter” the field: when a question opens,
the field is active.

### 3.3 Global shortcuts

| Key | Action |
|---|---|
| `Enter` | Check answer / continue / open / start lesson / discard in Outbox |
| `Esc` | Back / cancel / pause session |
| `Tab` | Item detail on review feedback |
| `↑` `↓` | Move focus; in text, `Fn` + arrows |
| `←` `→` | Previous/next page in lessons and item detail |
| `Space` | Next page in lessons and item detail; space in text |
| `L` | Lessons (home) |
| `O` | Outbox of rejected/stuck reviews (home; ignored when none need attention) |
| `S` | Sync (home) |
| `Q` | Optional local quiz on a lesson card |
| `D` | Discard the selected rejected/stuck review (Outbox) |
| `G0` | Short press wakes; long press is the future power action (not the pause menu) |

`Esc` from Home opens Settings; in a review it opens Pause → Settings. `Tab` on
review feedback opens the item card; `Tab` is the dev-build debug shortcut only
on Home.

### 3.4 Repeat and modifiers

- Only Backspace and the arrows repeat (400 ms delay, 120 ms rate).
- Letters don't repeat: this avoids accidental answers from holding a key down.
- Shift, Ctrl, Opt and Alt are tracked as state; on their own they don't
  produce a character (Shift changes the layer of the next one).
- If the TCA8418 loses events (overflow), everything is released and a discreet
  notice is shown; a key never gets “stuck”.

---

## 4. Flows

### 4.1 First setup

```
Boot
 └─ Wi-Fi saved? ──no──► Setup
                          ├─ 1. Temporary WPA2 network + QR/URL
                          ├─ 2. Portal: Wi-Fi + token
                          ├─ 3. GET /user: user/level
                          ├─ 4. Time (NTP or browser)
                          ├─ 5. Visual pack? ──no──► SD instructions
                          └─ 6. First sync (user/summary/level/SRS counts)
```

- The portal opens for first setup and can be reopened from Settings; it closes
  when setup finishes.
- The temporary network uses WPA2 with a random 8–10 character key visible on
  screen. It's not an open network.
- The token is pasted in the portal; it's validated with `GET /user` before
  saving.
- If the user cancels, the device stays in “Setup pending” and doesn't sync.

### 4.2 Daily review

```
Home ──Enter──► Sync (if due) ──► Review
                                   ├─ Meaning: type, Enter
                                   ├─ Reading: romaji→kana, Enter
                                   ├─ correct: brief feedback, Enter
                                   ├─ wrong: feedback + requeue
                                   └─ completed: POST /reviews
                                         ├─ ok: updates assignment
                                         └─ no network: outbox
                                   ──► Summary
```

- The session freezes when it starts: items added later don't appear until the
  next one.
- If the user exits, the position and the outbox are saved; on return, it
  resumes.
- The order is stable-random per session, mixing types; items answered wrong
  come back 3–5 positions later.

### 4.3 Offline

```
Home (Offline) ──► Review with cache ──► completed to outbox
                                         │
                                    when network returns
                                         │
                                    Sync outbox (1 by 1)
```

- Home shows the cached state or the pending/attention outbox count in the
  single status line, whichever is more urgent (for example `Offline - 2h old`
  or `Outbox: 4 pending`).
- If an item has no cached subject, it isn't offered. If no reviewable item
  remains, the session does not start; there is no separate “needs a
  connection” line yet.
- When the network returns, the outbox is sent before the summary is
  refreshed.

### 4.4 Lessons

```
Home ──L──► Lessons ──Enter──► Lesson card
                               ├─ Space: next page
                               ├─ ←→: pages
                               ├─ Tab: components/notes
                               └─ Enter: Start lesson
                                     └─ PUT /assignments/{id}/start
```

- The card shows meaning(s), reading(s), mnemonic and components.
- “Start lesson” requires a network in v1. The card can still be studied and
  the local quiz used offline; `Enter` attempts `PUT /start` and fails without
  a connection (the current screen does not yet show `Needs connection`).
- After starting, the assignment moves to the review queue at stage 1.

### 4.5 Conflicts and errors

| Situation | What the user sees | What the app does |
|---|---|---|
| Invalid token (401) | `Auth: token rejected` on Home; `Token rejected` on Boot | Arms the shared auth gate; Boot/Sync Enter opens setup |
| No permission (403) | `Auth: missing permissions` / `Missing permissions` | Endpoint-specific; Boot/Sync Enter offers the token page |
| Rate limit (429) | `Rate limited; retry in 42s` | Shared gate waits for `RateLimit-Reset`; no immediate retry |
| No network | `Offline - 2h old` + cache | Continues with the cache |
| 422 on submit | `WaniKani rejected this review (HTTP 422)` in Outbox | Marks it rejected; keeps it for `O: outbox` |
| 409 on submit | `Conflict (409); it may have changed` | Marks it rejected; keeps it for `O: outbox` |
| 50 failed attempts | `Not sent after N attempts` | Marks it stuck; keeps it for `O: outbox` |
| SD removed | `No SD card` | Retry; a session cannot fetch subjects |
| Missing glyph | `no glyph` + `check SD` | Draws the explicit state; does not fake a character |
| Unreliable clock | `Clock not set` | Requests sync; if not, `created_at` is omitted |

---

## 5. Screens

### 5.1 Boot

- Black background, `KaniKey` in coral, thin progress bar.
- States: `Mounting SD…`, `Reading cache…`, `Wi-Fi…`. Sync progress has its
  own screen (§5.11).
- If something critical fails, a single sentence and an action:
  - `No SD card · Insert a FAT32 card` / `Enter retry`.
  - `Visual pack missing · See docs/setup.md`.
- It doesn't show technical logs.

### 5.2 Setup

- Header `WELCOME`.
- Large `KaniKey`, subtitle `an unofficial WaniKani companion`.
- Steps: `Wi-Fi`, `API token`, `Clock`, `First sync`, with the current one
  marked.
- In step 1: list of networks, `Enter` to choose, text for the password.
- In step 2: portal URL and temporary network key; the phone does the rest.
  The token can also be pasted with the keyboard.
- In step 4: progress bar and `Cancel`.
- In step 5: if the visual pack is missing, a screen with:
  `Copy glyphs.kgp to /kanikey/ on the SD card` and `Enter: check again`.

### 5.3 Home

![Home](screens/home.png)

- Header: `KaniKey` (16 px brand), `Synced 14:47` (8 px, dim), battery.
- `LEVEL 12` in gold and the username next to it (12 px).
- Due reviews (`28`) at 32 px and the `REVIEWS DUE` label at 8 px.
- Lessons (`5`) at 32 px and `LESSONS` at 8 px.
- `Next review 18:00` at 8 px, with a right-aligned value.
- SRS distribution: five segments at y=100 (3 px high, 224 px wide) in
  apprentice/guru/master/enlightened/burned colors, with `A 42`, `G 18`,
  `M 9`, `E 4`, `B 2` and 4 px swatches below. The counts come from the last
  sync and are cached; if they are missing, the line reads
  `SRS: sync for counts`.
- The single status line at y=117 shows the most urgent state after the
  auth/rate/server errors above. If the outbox is not empty it shows
  `Outbox: N pending` in amber or `Outbox: N need attention` in red at 8 px,
  replacing the offline/stale/vacation/level line. The attention
  count is rejected + stuck; pending does not include either. When items need
  attention, the hint swaps `L: lessons` for `O: outbox` (§5.13).
- Priority of the status line: auth/rate/server error, outbox, offline/stale,
  level-up notice, vacation, level line. A level-up notice keeps priority over
  vacation and level until sign-out.
- Level line: `L8 42% since 03 Feb`, or `L8 42%` when the date is unavailable.
  The percentage is the rounded `passed kanji * 100 / total kanji` for the
  level (0 when the total is unknown); passed means Guru or later (SRS stages
  5-9). When the counts could not be fetched, the existing `L8 since 03 Feb`
  line is kept.
- Hint: `Enter: reviews  L: lessons  S: sync` (8 px). With rejected or stuck
  items the hint becomes `O: outbox  Enter: reviews  S: sync`, dropping entries
  from the middle when it would not fit; `O: outbox` is kept. `Esc` still opens
  Settings; the hint bar only shows the three primary actions. The M0 debug
  screen and the `Tab: debug` shortcut only exist in the `cardputer-adv-dev`
  build; the shortcut is no longer advertised on Home.
- States:
  - **Sync:** `S` starts the same sync as Settings › `Sync now`; the Sync
    screen (§5.11) replaces Home while a sync runs.
  - **Offline:** when no outbox item is pending or needs attention, the status
    line in `warn` reads `Offline - 2h old`; after 24 h, `Stale cache - 2d old`.
  - **Empty:** the counters show `0` and `Next review` shows the next time (or
    `all clear`); there is no separate “All caught up” screen or forecast
    detail yet.
  - **Vacation:** `Vacation mode` in amber in the status line when no more
    urgent sync/outbox/offline/level-up message is shown.
  - **Level:** `L8 42% since 03 Feb` (or `L8 42%`) in faint text in the status
    line when nothing more urgent is shown. The counts are cached with the
    level timestamps; without counts it falls back to `L8 since 03 Feb`.
  - **Level up:** after a sync raises the account level, the status line shows
    `Level up! L8 -> L9` in green instead of the level line. It is not shown on
    the first sync without a known previous level, stays for the session and is
    cleared on sign-out, a new token or a level decrease.
  - **Error:** Home shows `Auth: token rejected`, `Auth: missing permissions`,
  `Rate limited; retry later` or `WaniKani error; retry`. Boot shows distinct
  `Token rejected` / `Missing permissions` / `Rate limited` / `WaniKani error`
  screens; `Enter` retries or opens setup. `Esc` → Settings → `Sync now` retries.
- **Low battery:** below 20% on battery, starting reviews or lessons asks for
  confirmation; USB power skips the prompt.
- No side menus or cards covering the numbers are shown.

### 5.4 Review

![Meaning review](screens/review-meaning.png)
![Reading review](screens/review-reading.png)

- Header `REVIEWS`, `SRS 3 86%`, `12/48`, battery; 2 px progress bar.
- Left panel 88×88:
  - 64 px kanji/radical, centered.
  - Pill `KANJI · L5` or `RADICAL · L3` below.
  - Vocabulary: the word scales to 64/32/20 px to fit the 78×80 panel; if none
    fits, the prompt shows the missing-glyph state.
  - Radical without Unicode: bitmap from the visual pack.
- Right panel:
  - Pill `MEANING` (indigo) or `READING` (teal).
  - Instruction: `Type the meaning` / `Type the reading`.
  - 32 px field with the answer; caret.
  - In reading, below: `romaji: tabe` in gray; the field shows `たべ`.
  - `Tab: details` is shown on feedback; the question bar only has check/pause.
- Bar: `Enter check`, `Esc quit` (which opens Pause).
- The field is active as soon as the question appears. Nothing needs to be
  pressed.
- If the answer is long, the text scrolls horizontally; the caret stays
  visible. It doesn't wrap to two lines so as not to change the height.
- `Esc` in a review opens `Pause` (Continue / End session / Settings).
  Holding G0 remains the future power action.
- Idle: the panel dims at 30 s, blanks at 60 s and the device deep-sleeps at
  2 min when no review is active and it is on battery. On a blank panel the
  first key only wakes it.

**Implementation note (M5).** The live build draws the 64 px prompt, 32 px
reading and 20 px feedback from the SD visual pack. Vocabulary words composed
of several characters are laid out from individual glyphs. Radicals use their
96 px bitmap when the subject has no Unicode character; a missing glyph shows
an explicit `no glyph` state. The bottom bar says
`Enter check` / `Esc quit`; `Esc` opens the Pause menu (Continue / End session /
Settings), so quitting is deliberate. `Tab` on feedback opens the detail card
(§5.6). The screen is driven by the same review queue and answer checker as the
final design. The session order follows the account's
`reviews_presentation_order`: shuffled, or lower levels first with the shuffle
kept inside each level.

### 5.5 Feedback

![Correct feedback](screens/review-correct.png)
![Incorrect feedback](screens/review-wrong.png)

- The left panel doesn't change; the right one turns into feedback.
- Correct:
  - `CORRECT` pill in green + ✓.
  - `You typed` and the answer in gray.
  - `Meaning` / `Reading` with the correct answer in green/teal.
  - `Enter next`, `Tab details`.
- Wrong:
  - `NOT QUITE` pill in red + ✗.
  - `You typed` and the user's answer.
  - `Correct` with the correct answer.
  - `Comes back later in this session`.
- Feedback doesn't last a fixed time: it waits for `Enter` (or for the user to
  start typing the next one, if auto-advance is enabled).
- `Tab` opens the full card: meanings, readings, mnemonic and components.
- **Implementation note (2026-10-28).** After a correct answer, the free lines
  under the answer show the user's meaning or reading note when one exists.
  Wrong answers keep the mistake message. `Tab` opens the full detail card
  described in §5.6.

### 5.6 Item detail

- Header `ITEM DETAIL` with the page counter; type and level sit in the pill.
- Character/radical on the left; meanings and readings on the right.
- Pages with `←` `→` or `Space`:
  1. Meanings, readings and synonyms.
  2. Meaning mnemonic + hint.
  3. Reading mnemonic + hint.
  4. Meaning/reading notes.
  5. Components (radicals/kanji) with their meanings.
  6. Vocabulary: parts of speech + context sentences (when present).
  7. Related subjects: similar/amalgamation counts and cached meanings (when
     present).
- If the mnemonic isn't cached: `No mnemonic cached.`
- `Esc` or `Tab` returns to the review without losing the written answer.

**Implementation note (2026-10-28).** `Tab` on feedback opens the live card.
The page count depends on the subject: five base pages, plus vocabulary and
related pages when they apply. `Space`/`←`/`→` page and `Tab`/`Esc` returns to
feedback.

### 5.7 Session summary

![Summary](screens/summary.png)

- Header `SESSION`.
- Three columns at x=8/84/164 with 32 px values and 8 px labels:
  - `REVIEWS`: completed reviews.
  - `ACCURACY`: final correct answers / (final answers + wrong attempts),
    rounded. Each required meaning/reading part counts once.
  - `TIME`: elapsed wall-clock time, e.g. `42s`, `12m`, `1h 5m`.
- Divider, then:
  - `8 items moved up` in green, or `No stage changes` in faint. A successful
    `POST /reviews` counts only when its new stage is above the stage recorded
    when the review was queued; the response carries the new stage, not the old
    one.
  - `3 mistakes` on the left and the outbox state on the right: `4 pending` in
    amber, `1 need attention` in red (rejected + stuck), or `All reviews sent`
    in faint.
  - The item line combines two sources. The stage comes from the last
    successful `POST /reviews` (`gStageChange`; its previous stage is the one
    tracked in the pending assignment), while the lifetime accuracy/history
    comes from the last completed item (`gCompletedItem`). They can differ when
    that item's POST is still pending. Examples: `SRS 3 -> 4  86%`,
    `SRS 3  86%`, or `History 18 reviews, 89%`.
- `Enter: home`.

**Implementation note (2026-10-28).** The values use the built-in 32 px font
and fall back to 16 px if one would cross its column. Accuracy and duration
live in native-tested `wk/SessionStats.h`; the session start is a `millis()`
timestamp and the final duration is latched when the summary opens. It is not
persisted. Offline completions do not count as stage changes because there is
no POST response; they still become `gCompletedItem` and can update the
history/accuracy side of the item line.

### 5.8 Lessons

- `LESSONS` with a list of available items: character, type, level, main
  meaning.
- `↑↓` moves, `Enter` opens, `Esc` goes back.
- Each row: `休 · KANJI · L5 · rest`.
- If there are no lessons: `No lessons now` + the next review time from
  `/summary`; the next unlock is not known to v1.
- The list is sorted by level and `lesson_position`.

**Implementation note (2026-10-28).** The list and picker are live: a scrolling
list of up to the configured batch, sorted by level and `lesson_position`
within the first 10 summary candidates. Opening a card and paging does not call
`PUT /start`; only `Enter` on the card starts the lesson. Component meanings,
hints and the user's meaning/reading notes are appended to the paged card after
the mnemonic.

### 5.9 Lesson card

![Lesson](screens/lesson.png)

- Header `LESSON`: item position `2/5` and mnemonic page `p1/3` are
  right-aligned on the same row with an 8 px gap. This screen has no
  Wi-Fi/battery icons.
- A live 2 px progress bar at y=14 shows the completed lesson items; the card
  starts at y=20.
- Left panel: character/radical scaled to fit the 74×64 panel (64 px preferred).
- Right: pill `KANJI · L5`, main meaning in the title font, readings in teal at
  32 px (20 px fallback), `person + tree` in gray.
- Mnemonic in a 3-line, 10 px panel; `Space` turns the page. The page counter
  stays in the header so it never covers the third line.
- `Q quiz` opens the optional local quiz. `Enter start` always attempts the
  `PUT`; offline it fails and the screen currently has no separate
  `Needs connection` state.
- Bottom hint: `Space p  Q quiz` left and `Enter start  Esc list` right at
  y=125, with a gap between them.
- On start, `drawLoading` shows the English main meaning as its subtitle; the
  built-in font has no Japanese glyphs, so the character is not used there.
  Then a brief progress animation and the next card; `Esc` returns to the
  list. The card itself only reads until `Enter`.

### 5.10 Lesson quiz

- `Q` on a lesson card opens an optional local quiz; it is not required before
  starting the lesson.
- Header `LESSON QUIZ`, `1/2` (or `1/1` for radicals).
- Meaning first, then reading when the subject has one.
- Typed answers use the same romaji-to-kana input and answer checker as reviews.
- Wrong answers show `NOT QUITE`, the correct answer and are retried; a mistake
  count is kept. Warnings show `CHECK` and retry the same part.
- Completion shows `Practice complete`, the mistake count and
  `No /start sent.`; `Enter` returns to the card.
- `Esc` also returns to the card. The quiz never calls `PUT /start`; starting
  the lesson remains `Enter` on the card.

### 5.11 Sync

![Sync](screens/sync.png)

- A dedicated screen replaces Home while a sync runs.
- Six rows: `Wi-Fi`, `Outbox`, `Account`, `Summary`, `Level`, `SRS`.
- The `Outbox` row shows pending and attention separately (`2 attention, 4
  pending`), or `N need attention` / `N pending`; `none` before a flush and
  `sent` when both are zero afterwards.
- Each row shows a name, a value and a status: `ok` in green, `..` while
  active, `!` for a warning, `X` for a failure and `--` while pending.
- Progress bar `0/6` … `6/6`; the title reads `Syncing…`, `Synced` or
  `Sync failed`.
- `Esc` cancels at the next step boundary. Completed requests are kept, and
  the device returns to Home (or to boot with `Sync cancelled` if there is no
  cache).
- A failed Account or Summary request stops the later steps and leaves the
  screen on `Sync failed` with the error line. `Enter` retries; for a token or
  permission error `Enter` opens setup. `Esc` goes back.
- A failed Level or SRS request is not a blocker: the sync completes and keeps
  the previous cached data. It shows `!` when the step reports a warning; a
  still-valid cached Level value can remain `ok`.
- The rows are drawn between the blocking TLS requests, so a cancel pressed
  during a request takes effect when it finishes.

### 5.12 Settings

![Settings](screens/settings.png)

- Open with `Esc` from Home, or `Esc` in a review → Pause → Settings.
- Implemented rows: `Brightness`, `Dim after`, `Blank after`, `Sleep after`,
  `Review batch`, `Order`, `Lesson batch`, `Setup portal`, `Sync now`, `About`,
  `Clear cache`, `Sign out`.
- `Brightness` cycles 64/128/192/255; the timings cycle presets and `off`.
  Review batch cycles 10/20/30, lesson batch 3/5/10, and order toggles
  meaning-first / reading-first. All are stored in NVS and applied without a
  reboot. WaniKani's `reviews_presentation_order` is applied automatically and
  is not a device setting.
- The list scrolls when there are more rows than fit (7 visible, scroll bar).
- `Setup portal` reopens the Wi-Fi/token/time page; it is disabled during a
  review so the session is not lost. `Sync now` starts a manual sync.
- About shows the firmware version, build date, pack status, cached batch,
  last sync, Wi-Fi, whether a token is configured, time zone and heap.
- `↑↓` moves, `Enter` changes, `Esc` goes back.
- `Clear cache` removes the snapshot/subjects for the next boot; `Sign out`
  confirms, then removes the token, cache and pending outbox and keeps Wi-Fi.
- Not yet: licenses and attributions and strict-answer options.

### 5.13 Outbox

- Open with `O` from Home when at least one review was rejected or gave up; the
  key is ignored when nothing needs attention.
- Header `OUTBOX` and `N need attention`; `rejected` and `stuck` items are
  listed. Pending, sending and ambiguous items are not.
- Up to seven visible rows scroll with `↑↓`. A row shows the subject's English
  main meaning and level (for example `day L1`) when it is still in the cached
  batch, otherwise `assignment <id>` (or `subject <id>`). The reason is
  right-aligned: `HTTP 422`, `HTTP 409`, `rejected` when the code is unknown, or
  `gave up` for a stuck item.
- `Enter: review` or `D: discard` opens the confirmation; `Esc: back` returns
  to Home.
- Confirmation shows the same identifier and the reason. Rejected items keep
  `WaniKani rejected this review (HTTP 422).`, `Conflict (409); it may have
  changed.` or the generic fallback; stuck items show
  `Not sent after N attempts.` Both add the attempt count and the created age
  (`just now`, `2h`, `3d`, or `age unknown` when `created_at` is 0 or the clock
  is untrusted). It also warns that dismissing only removes the local unsent
  review and does not change WaniKani.
- `Enter: discard` removes exactly that sequence, saves the outbox and returns
  to the list, or to Home when no rejected/stuck items remain. `Esc: cancel`
  returns to the list. Pending, sending and ambiguous items are never removed
  here, and the cache/assignments are not touched. If the outbox file cannot be
  written, the item stays and the screen shows `Could not save outbox`.

### 5.14 Stats (phase 2)

- Current level and level progress.
- SRS distribution detail with bars and counts (Home already shows the bar).
- Session and global accuracy (if cached).
- Streak and reviews from the last 7 days.
- `Esc` goes back.

### 5.15 Errors and empty states

- **Not configured:** Boot `Token needed` or `Wi-Fi needed` + `Enter: setup`.
- **No SD:** `No SD card` + `Insert a FAT32 card with /kanikey/glyphs.kgp` +
  `Enter: retry`.
- **Visual pack:** `Pack missing` + `Copy glyphs.kgp to /kanikey/ on the SD
  card` + `Enter: check again`.
- **No network:** Home status `Offline - ...` with cached data and `S: sync`;
  without a cache, Boot `Sync failed` with the network line + `Enter: retry`.
- **Token:** Home `Auth: token rejected`; Boot `Token rejected` + `Enter:
  setup`.
- **Permissions:** Home `Auth: missing permissions`; Boot `Missing permissions`
  + `Enter: setup`.
- **Rate limit:** Home `Rate limited; retry in 42s`; Boot `Rate limited` +
  `Retry in 42s`.
- **Server:** Home `WaniKani error; retry`; Boot `WaniKani error` + `Enter:
  retry`.
- **Empty:** the counters show `0` and `Next review` shows the next time (or
  `all clear`). A separate caught-up/empty-session screen is not implemented.

Error screens never show stack traces, URLs with a token, or JSON.

---

## 6. Accessibility

- **Contrast:** main text on background > 15:1; secondary text > 7:1;
  semantic colors are accompanied by ✓/✗ and a word.
- **Color blindness:** green and red aren't the only signal; the shape (pill +
  icon) and the text change.
- **Size:** minimum 9 px for non-essential metadata; 10 px for anything that
  has to be read; 20 px for the answer.
- **Keyboard:** everything can be done without touching the screen; there are
  no gestures.
- **Movement:** only the caret and progress; no animations that block.
- **Language:** English. Meanings/readings are always WaniKani's (English).
- **Long text:** it paginates; the font is never reduced below 9 px.

---

## 7. Copy and tone

- Short, direct, without WaniKani jargon.
- “Correct” / “Not quite”, not “Well done!” or “You got it wrong”.
- “Reviews”, “Lessons”, “Meaning”, “Reading”, “Item”, “Session”.
- “Offline - 2h old” and “Outbox: 4 pending”, not “Network error”.
- “Comes back later in this session”, not “Incorrect. It will be repeated”.
- Disclaimer in Setup and About:
  `KaniKey is an unofficial third-party app for WaniKani. Not affiliated with
  WaniKani or Tofugu LLC.`

---

## 8. Mockups

| File | Screen |
|---|---|
| `screens/home.png` | Home with reviews, lessons and SRS distribution |
| `screens/review-meaning.png` | Meaning question |
| `screens/review-reading.png` | Reading question with romaji |
| `screens/review-correct.png` | Correct feedback |
| `screens/review-wrong.png` | Incorrect feedback |
| `screens/lesson.png` | Lesson card |
| `screens/summary.png` | Session summary |
| `screens/sync.png` | Synchronization |
| `screens/settings.png` | Settings |
| `screens/setup.png` | First boot |
| `screens/overview.png` | Contact sheet of all |

To regenerate them:

```sh
python3 tools/mockups/render.py
```

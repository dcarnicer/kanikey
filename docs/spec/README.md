# KaniKey improvement spec — round 2

Status: **draft, 2026-11-01**. This folder is the working specification for the
round of improvements after `v0.1.0`. It is written so that each task can be
handed to a subagent on its own: every task says what is wrong or missing, what
to build, which files to touch, how to test it and when it is done.

Sources:

- The code review of 2026-11-01 (firmware, `wk/`, platform/OTA, real-device
  captures in `tools/screenshots/out`). Every bug listed in
  [01-fixes.md](01-fixes.md) was verified against the code, the framework
  source or the hardware logs.
- An inventory of [Kakehashi](https://github.com/Portego-00/Kakehashi)
  (MIT) settings and features, read from its source. No Kakehashi code is
  copied; only product ideas. If code is ever copied, follow
  [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).
- The owner's decisions below.

The existing [ROADMAP.md](../../ROADMAP.md) stays the status snapshot; when a
task here lands, update its row there too.

---

## 1. Decisions for this round

| ID | Decision |
|---|---|
| D1 | **Keep the current UI font.** Latin UI text stays on M5GFX `Font0` (6×8 GLCD) with `Font2`/`Font4` for big values. The mockups in `docs/screens` must be brought in line with the device (RGB332 palette, GLCD font), not the other way round. |
| D2 | **Lessons follow WaniKani's batch flow.** Browse every card of the batch, take a quiz on the batch, then start the whole batch with one confirmed action. `Enter` on a card is no longer an irreversible `PUT /start`. |
| D3 | **A configurable app.** Settings becomes a sectioned menu backed by a settings registry. Every new behaviour lands behind a setting whose default is faithful to WaniKani (or to the current KaniKey behaviour when WaniKani has no equivalent). |
| D4 | **Kakehashi blocks in scope:** review options, critical items and extra study, statistics, reminders and audio. Practice modes never write to WaniKani. |
| D5 | **English** for docs, code comments and UI strings, as in the rest of the repo. |
| D6 | **Delegated implementation.** Tasks are implemented by subagents (by default DeepSeek v4.1 flash through opencode, see §7) and reviewed before merge. |

Decisions that change an earlier entry of [docs/decisions.md](../decisions.md)
must add a new dated entry there when the task lands (for example the typo
policy in REV-02 and the lesson flow in LES-01).

---

## 2. Global constraints

Every task must respect these. A task that cannot should stop and report why.

**Hardware and memory**

- ESP32-S3FN8, **no PSRAM**. Internal heap after boot is ~140 KB free; a TLS
  handshake needs one large contiguous block (~40 KB). The 8-bit canvas
  (32,400 B) is released around each request (`gFreeCanvasForRequest`) for that
  reason. New features must not keep large buffers alive across network calls.
- Flash: the app slot is 1,835,008 B and the release image is ~1.46 MB
  (79.6%). Budget for this whole round: **≤ 250 KB** of extra flash. Any task
  adding a library must state its flash and RAM cost.
- Display 240×135, RGB332 canvas. Colours come from `firmware/src/ui/Theme.h`;
  do not add colours outside it without updating it.
- Loop task stack is 24 KiB. No large arrays on the stack.

**Product rules**

- No WaniKani content in the repository (meanings, readings, mnemonics, images,
  audio). Content is fetched with the user's token and stored on the device.
- WaniKani is the SRS. KaniKey never invents SRS state; practice modes are
  local and never call `POST /reviews` or `PUT /start`.
- Every blocking operation longer than ~1 s shows progress and can be
  cancelled with `Esc`, or says why it cannot.
- The bottom hint bar names the keys that work on that screen (ux.md §1.3).
  Hidden shortcuts are allowed only when the hint bar has no room, and then
  they must be listed in the screen's help (`?`, see CFG-04).
- Settings are stored in NVS through `platform/DeviceSettings`. NVS keys are at
  most 15 characters. Writes happen only when the user changes a value.

**Code rules**

- Pure logic goes in `firmware/src/wk/` (or a new pure module) with native
  tests in `firmware/test/test_wk/`. No Arduino headers there.
- Views in `firmware/src/app/*Screen.cpp` stay pure: they receive a view struct
  and draw; no globals, no `present()`.
- Follow the surrounding style: naming, comment density, `snprintf` into fixed
  buffers, no exceptions.
- Do not change on-disk formats (`KKCA`/`KKCS`, outbox, glyph pack) without
  bumping their version and keeping the old file readable or explicitly
  discarded.

---

## 3. Document map

| File | Prefix | Tasks | Contents |
|---|---|---:|---|
| [00-architecture.md](00-architecture.md) | ARCH | 4 | Prerequisites: split `App.cpp`, settings registry, local stores shared by later tasks, screen state machine |
| [01-fixes.md](01-fixes.md) | FIX | 24 | Verified bugs from the review (3 × P0) |
| [02-speed.md](02-speed.md) | SPEED | 7 | Faster open → review → close loop: submission, sync, prefetch, keep-alive, sleep |
| [03-settings.md](03-settings.md) | CFG | 4 | The sectioned Settings menu and the registry of every setting (R, L, S, Y, P, M IDs) |
| [04-reviews.md](04-reviews.md) | REV | 12 | Review session options and feedback |
| [05-lessons.md](05-lessons.md) | LES | 6 | WaniKani-style lesson batch flow |
| [06-study.md](06-study.md) | STUDY | 5 | Critical items and local extra-study modes |
| [07-stats.md](07-stats.md) | STATS | 4 | Forecast, history, streak and progress screens |
| [08-reminders-audio.md](08-reminders-audio.md) | REM, AUD | 6 | Device-local reminders and vocabulary audio |
| [09-docs-onboarding.md](09-docs-onboarding.md) | DOC, ONB | 8 | Mockups, docs alignment and easier pack setup |

[03-settings.md](03-settings.md) (CFG-02) is the single source of truth for setting IDs, labels, choices, defaults and NVS keys. Other files refer to settings by those IDs (`R1`…`M7`); if a task file and CFG-02 disagree, CFG-02 wins and the task file is corrected.

Line references (`App.cpp:1359`) point to commit `2115903`. After ARCH-01 the functions move into controllers but keep their names: locate them by name, not by line.

---

## 4. Task template

Every task in the files above uses this shape. Keep the headings, even when a
section is short.

```markdown
### <ID> — <Title>

- **Priority:** P0 (bug that loses data or blocks a core flow) · P1 · P2 · P3
- **Size:** S (< ~150 lines) · M (< ~500) · L (more; consider splitting)
- **Depends on:** <IDs or "none">
- **Touches:** <files, with the functions or line ranges>
- **Settings:** <CFG setting IDs it adds or reads, or "none">

**Problem / goal.** What is wrong or missing today, with file:line references
and a concrete example.

**Behaviour.** What the user sees and what the device does after the change.
Include screen text, key bindings and edge cases (offline, empty, errors).

**Design.** How to implement it: data structures, functions, state changes.
Name the existing helpers to reuse.

**Tests.** Native tests to add (name the cases), plus the hardware checks that
cannot be automated.

**Docs.** Which docs to update (ux.md section, api-sync.md, decisions.md,
ROADMAP.md row).

**Acceptance.**
- [ ] Checklist items that a reviewer can verify.
```

---

## 5. Execution waves and file ownership

`firmware/src/app/App.cpp` (~5,000 lines) is shared by almost every task, so
parallel subagents would conflict there. The waves keep parallel work on
separate files.

| Wave | Tasks | Parallel? |
|---|---|---|
| 0a | FIX tasks that touch only `wk/`, `ota/` or `platform/` | Yes, one agent per file group |
| 0b | FIX tasks that touch `App.cpp` | Sequential, one agent |
| 1 | ARCH-01 (split `App.cpp`), then ARCH-02 (settings registry) and ARCH-03 (local stores) | ARCH-01 alone; then ARCH-02 ∥ ARCH-03 |
| 2 | CFG, SPEED, REV, LES | Yes, once ARCH-01 has given each its own controller file |
| 3 | STUDY, STATS | Yes |
| 4 | REM, AUD (each starts with a spike) | Yes |
| any | DOC, ONB | Yes; docs only, except ONB-02 |

A task may only edit the files listed in its **Touches** line plus tests and
docs. If it needs another file, it stops and reports instead of editing it.

---

## 6. IDs and status

IDs are stable; never renumber. Status lives in this table and in ROADMAP.md.

| Status | Meaning |
|---|---|
| `todo` | Specified, not started |
| `wip` | A subagent or person is on it (add the branch name) |
| `review` | Implemented, waiting for review / hardware check |
| `done` | Merged and, where needed, checked on hardware |
| `dropped` | Decided against; keep the entry with the reason |

---

## 7. Working with subagents

### 7.1 Branches and commits

- One task (or one tightly coupled group, e.g. FIX-05 + FIX-06) per branch:
  `spec/<ID>-<short-name>`, from `main`.
- Commit messages follow the repo style (`fix: …`, `feat: …`, `docs: …`) and
  mention the task ID.

### 7.2 Running a task with DeepSeek through opencode

Implementation runs use the `build` agent inside the repository:

```sh
opencode run -m opencode-go/deepseek-v4.1-flash --agent build \
  --title "<ID>" "$(cat brief.md)"
```

Read-only research or drafting uses `--agent plan` and captures stdout. The
brief is built from the template in §7.3.

### 7.3 Brief template

```text
You are implementing task <ID> of the KaniKey spec.
Read first: docs/spec/README.md (constraints, §2), docs/spec/<file>.md
(task <ID> and the tasks it depends on), docs/spec/03-settings.md for any
setting ID it mentions, and the files in its Touches line. Line numbers are
from commit 2115903; find functions by name if they have moved.
Work on branch spec/<ID>-<name>. Only edit the files in Touches, the native
tests and the docs listed in the task.
Done means:
  1. cd firmware && pio test -e native        (all pass, new tests included)
  2. pio run -e cardputer-adv                  (builds; report flash/RAM %)
  3. pio run -e cardputer-adv-dev              (builds)
  4. Every Acceptance box can be ticked from the code or the tests.
Do not commit secrets, tokens or WaniKani content. Do not change on-disk
formats unless the task says so.
Report back: files changed, tests added, build numbers, anything you could
not do, and the hardware checks that remain.
```

### 7.4 Review

The reviewer (Claude or the owner) reads the diff against the task, runs the
three commands in §7.3 and does the hardware checks listed in the task with
`tools/device.py` and `tools/shot.py`. Only then the status becomes `done`.

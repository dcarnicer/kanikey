# KaniKey roadmap

Status snapshot: **2026-10-27**. This is the working backlog after the SD
visual pack landed, not the original product plan. The milestone definitions
and UX live in [docs/design.md](docs/design.md), [docs/ux.md](docs/ux.md) and
[docs/api-sync.md](docs/api-sync.md). The radical renderer is implemented and
builds clean, but hardware verification is pending because the test device is
not available.

## Where we are

Working on hardware:

- Home from `/user` + `/summary`, with due reviews, lessons, next review and
  outbox status.
- 20-item review session with typed meaning/reading, romaji-to-kana, answer
  checking, feedback and requeue.
- `POST /v2/reviews` with a persistent LittleFS outbox; the full offline path
  (Wi-Fi off → review → reboot → reconnect → 201) is verified.
- Lessons: five at a time, card with meaning/reading and paged mnemonic,
  `PUT /assignments/{id}/start` returns 200.
- SD visual pack: `KKGP` v1, 6,621 glyphs at 64/32/20 px, 2.9 MB, generated
  locally from the WaniKani kanji cache. Wi-Fi asset upload works.
- Radical prompts render from the pack's 96 px bitmaps; radical-only lessons
  and reviews are no longer filtered out. Missing glyphs show an explicit
  state. Device verification is pending.
- 8-bit RGB332 canvas (32.4 KB), static 32 KB HTTP body buffer, dim/blank/deep
  sleep, screenshot capture.
- 33 native tests pass. GitHub Actions runs gitleaks on `main`.

Current build: **1,741,561 B / 1,835,008 B flash (94.9%)**, **93,208 B /
327,680 B static RAM (28.4%)**. Flash headroom is ~91 KB partly because
`efontJA_16` is still linked (~468 KB).

## Priority summary

| Priority | Workstream | Why | Status |
|---:|---|---|---|
| 1 | Radicals and `efontJA_16` removal | Early levels and lessons are incomplete; frees ~468 KB flash | Partial (renderer done, pack/device pending) |
| 2 | Persistent offline cache | The outbox survives reboot, the review batch does not | Partial |
| 3 | Complete lesson flow | Card + start exists; list, components and quiz do not | Partial |
| 4 | Outbox and API robustness | Rejected items and rate limits have no user path | Partial |
| 5 | Setup, settings and pause | Wi-Fi/token still need a serial cable | Not started |
| 6 | UI polish | Missing states, SRS bar, sync screen, small layout issues | Partial |
| 7 | Technical debt, CI and release | Maintainability and shipping | Ongoing |

## 1. Radicals and remove `efontJA_16` (v1 blocker)

The pack on the card covers kanji, kana and vocabulary, but was generated
without a token, so it has **no radical images**. The firmware now renders
radical entries, includes radical-only lessons/reviews and shows a missing
glyph state, but the pack must be regenerated with the user's token to see the
symbols. `efontJA_16` is still linked for readings/text fallback and keeps
~468 KB of flash occupied. This remains the largest user-facing gap until the
radical pack is generated and checked on hardware.

Tasks:

- Generate a pack with the user's token and radical SVG images
  (`cairosvg`), including the 16-byte radical entries. **Pending:** needs the
  user's token; the generator already supports it.
- Implement `GlyphPack::drawRadical`: decode the native 96 px bitmap and scale
  it to the prompt sizes; use it in review and lesson prompts. **Done** in code.
- Include radical-only lessons and reviews instead of filtering them out by
  `characters`. **Done** in code.
- Add a “visual pack missing” / “glyph missing” state with an actionable
  message instead of a silently blank prompt. **Partial:** items now show
  `no glyph` / `no pack` + `check SD`; a full startup screen still needs work.
- Remove `efontJA_16` references once radicals render; reclaim ~468 KB of
  flash for the remaining features. **Pending:** do it after the radical pack
  and the missing-pack startup screen are verified on hardware.
- Test with a low-level account, where the first lessons are mostly radicals.
  **Pending:** needs the device and a generated radical pack.

Dependency: the radical images are generated locally with the user's own token;
the repository must not contain WaniKani art.

## 2. Persistent offline cache (v1 blocker)

The outbox protects completed reviews, but assignments and subjects live only
in RAM. After a reboot without network there is no batch to review. This does
not yet meet the offline requirement.

Tasks:

- Persist the due assignment list and the subjects needed for a session
  (LittleFS for critical assignment state, SD for the larger subject payload,
  as in the design).
- Restore Home counts and a review batch at boot when Wi-Fi is unavailable.
- Add an explicit `Offline` state, last-sync time and cache age.
- Define invalidation: server `data_updated_at`, token/account change, cache
  version, and a manual sync.
- Keep the outbox flush/reconciliation path working with the cache.
- Test: online fetch → reboot with Wi-Fi off → complete a review → reconnect →
  outbox 201.

## 3. Complete lesson flow (M4)

The card and `PUT /start` work, but this is not yet a full lesson flow.

Tasks:

- Lesson list: available lessons with character, type, level and main meaning;
  sorted by level and `lesson_position`.
- Detail card: components, meaning/reading, mnemonic pages and hints.
- Decide whether the local meaning/reading quiz is required before `PUT
  /start` or optional practice. If required, add the quiz, incorrect-answer
  feedback and completion state.
- Respect the user's lesson batch size and show progress through the batch.
- Handle radical-only subjects once radical images exist.
- Add a “no lessons right now” state with the next unlock if known.

## 4. Outbox and API robustness (finish M3)

Already done: CRC/`.bak`, ambiguous-request reconciliation, offline end-to-end,
Home pending/attention counts. Remaining:

- User-facing dismissal of `Rejected` items (today only the dev `z` command
  clears the outbox).
- 429 handling: wait for `RateLimit-Reset` and back off instead of retrying
  immediately. A 429 currently becomes `Pending` with no delay.
- 401/403: surface “token invalid/expired” as an auth state; do not retry
  forever.
- Retry truncated/parse-failed responses, not just transport errors. The
  subject DOM path still fails the batch on a short body.
- Cap attempts and show attempt/age information for stuck items.
- Keep `api-sync.md` error table and screens in sync with the implementation.

## 5. Setup, settings and pause (M5)

Today Wi-Fi and the token are configured over serial into NVS. That is fine for
development, not for a device someone else can use.

Tasks:

- First-boot setup portal: Wi-Fi scan/password, token entry (masked, never
  logged), timezone and visual-pack check.
- Pause menu while reviewing: Continue / End session / Settings, replacing the
  current direct `Esc` exit.
- Settings: brightness, dim/blank/sleep timings, timezone, About/version.
- Low-battery state and confirmation before starting a long session.
- Manual sync and clear error/retry screens.

## 6. UI polish (M5)

- Home SRS distribution bar.
- Sync screen with progress and a clear offline/error state.
- Missing-pack and missing-glyph states.
- Lesson layout fixes (for example the page counter and hint bar spacing).
- Japanese subtitle in `drawLoading` currently uses a bitmap font and can be
  blank; use the pack or an ASCII message.
- Revisit the 8-bit palette after real use; consider 16-bit only if a streaming
  subject parser frees enough contiguous memory.
- Keep live screenshots out of the repository because they contain the user's
  WaniKani content.

## 7. Technical debt, CI and release

- **Streaming subject parser**: assignments/subjects still build a JSON DOM;
  the summary already streams. A streaming parser would reduce fragmentation,
  permit larger batches and make a 16-bit canvas realistic.
- **Refactor `main.cpp`**: it is ~1,900 lines with many globals. Split screens,
  session state and app services before adding more flows.
- **Dev hooks**: `gDirectReading`, synthetic outbox commands and other serial
  helpers should be behind a development build flag.
- **Tests/CI**: add a PlatformIO workflow (`pio run -e cardputer-adv`,
  `pio test -e native`) alongside gitleaks; add host tests for the pack
  reader/RLE and more outbox/HTTP edge cases.
- **Release**: decide KaniKey's license, finalize third-party notices, version
  the firmware, add OTA/About, and review WaniKani API/branding compliance.
- **Extras later**: audio, stroke order, FSRS practice, forecast, synonyms,
  phone-side pack generation.

## Recommended order

1. Radicals + remove `efontJA_16`.
2. Persistent offline cache and boot-without-network.
3. Lesson list, components and quiz.
4. Outbox dismissal, 429/backoff and auth-invalid state.
5. Setup portal, Pause menu and settings.
6. UI polish, CI build/tests, `main.cpp` refactor, release.

## Definition of done for v1

- Review and lesson Japanese renders entirely from the SD pack; no CJK font in
  flash.
- A session can be fetched, cached, rebooted, completed offline and synced.
- Lessons can be listed, understood (components/quiz) and started.
- Rejected and rate-limited reviews have a visible, recoverable path.
- Wi-Fi, token and time can be configured on the device itself.
- CI builds the firmware, runs the native tests and scans secrets.
- No WaniKani content is distributed in the repository.

# KaniKey — product design and architecture

> **Status:** design approved, 2026-10-25. Firmware M1 is in progress: display,
> keyboard controller, microSD, Wi-Fi, SNTP, authenticated `/user` + `/summary`
> and a live Home screen. The review session is next. Measurements:
> [hardware.md](hardware.md).
>
> **Working name:** KaniKey. It's provisional; the design doesn't depend on it.
>
> KaniKey is an **unofficial** third-party app for WaniKani. It is not affiliated,
> endorsed, or sponsored by WaniKani or Tofugu LLC. WaniKani is a registered
> trademark of Tofugu LLC.

---

## 1. Summary

KaniKey is a pocket review terminal for WaniKani on the **M5Stack Cardputer
ADV**. The idea is simple: turn the device on, see which reviews are due, type
them on the keyboard, and turn it off. No phone, no tabs, no notifications.

The Cardputer ADV is special because it has a real keyboard. WaniKani isn't
reviewed by choosing between four options: you type the meaning in English and
the reading in kana. That makes a two-button console the wrong tool for the job,
and a 56-key keyboard the right one.

KaniKey **does not replace WaniKani**. The SRS, assignments, and progress still
live in the user's account. The device is a local view, a review queue, and a
pleasant place to type. When there's a network, it syncs; when there isn't, it
lets you review from the cache and leaves the results in a queue.

| Decision | Proposal |
|---|---|
| Input | Typed answer, not multiple choice |
| SRS | WaniKani's; KaniKey doesn't reschedule |
| Network | Direct sync against API v2, no own server |
| Offline | Subject/assignment cache + review queue |
| Content | No WaniKani content in the repo; user token and cache |
| Rendering | Pre-rendered glyph pack on the microSD |
| Base | Pruned fork of Tangocho (HAL, rendering, UI, Wi-Fi, OTA) |
| v1 scope | Reviews; lessons in phase 2 |
| Board | Cardputer ADV only |

---

## 2. Goals and non-goals

### 2.1 Goals

1. **Review fast.** From pocket to first item in under 15 seconds (with Wi-Fi
   already configured and a warm cache).
2. **Fidelity to WaniKani.** Same accepted meanings, readings, subject types,
   incorrect-answer counters, and submission semantics.
3. **Keyboard first.** Typing is the main interaction; no hidden menus for
   entering text.
4. **Honest offline.** The screen says whether there's a network, when it last
   synced, and how many reviews are left to send. It never pretends a review
   has been sent.
5. **Readability at 240×135.** One focus per screen; typography and spacing
   measured, not “whatever fits”.
6. **Maintainable.** Pure logic testable on the host; separate screens; no
   putting the API and drawing into `main.cpp`.
7. **Legal and brand respect.** No WaniKani content in the repository, no art,
   no UI, and no using its name as the product name.

### 2.2 Non-goals

- Copying the WaniKani website or official app.
- Implementing a local SRS for reviews (FSRS or another). Scheduling belongs to
  the server. An extra practice mode could use FSRS later, but it isn't the
  product.
- Community, forum, ratings, push notifications, mobile app, web.
- Pronunciation audio, stroke order, or kanji drawing in v1.
- Supporting StickS3, T-Display, or other boards in v1. The HAL would allow it,
  but each board adds testing and layout compromises.
- Multi-account or local profiles.

---

## 3. Why the Cardputer ADV

| Constraint | Design implication |
|---|---|
| 240×135, landscape only | Single column or two zones; no dense tables |
| No PSRAM | No full JSON documents, no CJK fonts in RAM, no large caches |
| 8 MB flash, ~1.75 MB per OTA slot | Heavy content (glyphs, subjects) goes on the microSD |
| 4×14 keyboard with Fn/Shift | Real text; shortcuts must be visible and not steal letters |
| TCA8418 + G0 | Must distinguish navigation mode from text mode |
| Wi-Fi + TLS in ~50 KB | Sequential sync, streaming responses |
| Battery + deep sleep | Radio only turns on to sync; reviewing is local |
| MicroSD FAT32 | Content cache and visual pack; critical progress in LittleFS |

The “mobile app” alternative already exists and works (Kakehashi). What KaniKey
brings is a dedicated, distraction-free device with a keyboard and a battery.
The “StickS3” alternative forces multiple choice or an external keyboard; it
loses the appeal.

---

## 4. User and use cases

### 4.1 Typical user

Someone who already uses WaniKani, is around level 5–30, likes gadgets, and
wants to review in dead moments: the couch, transit, a break at work. They
don't want to look at their phone for “just 20 reviews” because they end up
doing something else.

### 4.2 Main flows

**A. Daily review with network**

1. Turn on the Cardputer (or wake it from sleep).
2. KaniKey shows 28 reviews and 5 lessons; it syncs in the background if it's
   been more than 15 minutes.
3. `Enter` → session. Type the meaning, then the reading.
4. When each item is completed, it is sent to WaniKani. If that fails, it is
   left in the queue.
5. When done: summary, next review time, and “all caught up”.

**B. Review without network**

1. The cache has the items that were due and their subjects.
2. The screen shows `Offline · 18 cached · 4 to sync`.
3. Review the same way; completed reviews are stored in the local queue.
4. When the network returns, it syncs the queue and updates the assignments.

**C. Lessons**

1. `L` → list of available lessons, sorted by level and `lesson_position`.
2. Lesson card: character, meanings, readings, mnemonic, and components.
3. `Enter` → `PUT /assignments/{id}/start`; the assignment moves to the review
   queue.
4. Without a network, the card can be studied, but the lesson can't be started
   (v1).

**D. First-time setup**

1. Wi-Fi: the Cardputer brings up a temporary WPA2 network with a key on screen.
2. The phone opens the portal, saves the Wi-Fi, and pastes the WaniKani token.
3. The portal validates `GET /user`, shows the user/level, and closes the
   network.
4. The Cardputer syncs and checks the visual pack on the microSD.
5. If the pack is missing, it shows how to generate it on the computer and copy
   it.

---

## 5. Scope

### 5.1 MVP (v0.1–v0.3)

| Area | Must have |
|---|---|
| Boot | HAL, display, keyboard, microSD, battery, sleep |
| Setup | Wi-Fi + token + time; temporary portal |
| Home | Level, user, reviews, lessons, next review, SRS distribution |
| Sync | `user`, `summary`, `assignments`, `subjects`, `study_materials` |
| Review | Typed meaning and reading; romaji→kana; feedback; requeueing |
| Submission | `POST /reviews` with incorrect-answer counters |
| Cache | Subjects and assignments on SD/LittleFS; boot without network |
| Offline | Persistent outbox and sending when the network returns |
| Errors | Invalid token, no network, no SD, missing visual pack, 429/5xx |
| Settings | Account, study, sync, device, about |

### 5.2 After the MVP (v0.4)

- Advanced reconciliation of ambiguous submissions and conflicts between
  devices.
- Lessons with mnemonic and `start`.
- Session statistics and accuracy.
- OTA with an HTTPS manifest and SHA-256 (reusing Tangocho).
- Sync screen with progress and cancel.

### 5.3 Later

- Pronunciation audio (ES8311).
- Stroke order with KanjiVG.
- Extra practice with local FSRS (doesn't affect WaniKani).
- User synonyms and notes.
- Review forecast, heatmap, streak.
- Multiple-choice mode to review without a keyboard.
- Multiple accounts.
- Font/visual pack generated from the phone, without a computer.

### 5.4 Out of scope

- Forum, community, ratings, push notifications.
- Copying WaniKani's UI.
- WaniKani content distributed with the firmware.
- Compatibility with other boards in v1.

---

## 6. Product principles

1. **Keyboard first.** If an action needs text, you type it. Shortcuts are shown
   in the bottom bar, not hidden.
2. **The server decides.** WaniKani decides when reviews are due and what SRS
   stage each item is in. KaniKey doesn't invent intervals.
3. **Offline is a state, not an error.** It is shown clearly, reviewing is
   allowed, and it says what is still pending submission.
4. **One decision per screen.** The item, the answer, the result, and the next
   action. No side menus during review.
5. **Content outside the repo.** The firmware carries no WaniKani meanings,
   readings, mnemonics, or art. The visual pack is generated with the user's
   token and stays on their card.
6. **Reuse the hardware, rewrite the product.** The HAL, rendering, base UI,
   Wi-Fi, and OTA come from Tangocho. The review engine is new.
7. **Measure on hardware from day one.** Without PSRAM, memory is a product
   function, not a later optimization.

---

## 7. Main flow

```
                         ┌─────────────┐
                         │    Boot     │
                         └──────┬──────┘
                                │
                 ┌──────────────┴──────────────┐
                 │                             │
          not configured                  configured
                 │                             │
          ┌──────▼──────┐              ┌───────▼────────┐
          │   Setup     │              │     Home       │
          │ Wi-Fi/token │              │ due / lessons  │
          │  /time      │              │ next / sync    │
          └──────┬──────┘              └───┬───┬───┬────┘
                 │                         │   │   │
                 └──────────────►          │   │   └──── Tab ──► Settings
                                      R    │   │ L
                                      ┌────▼───┴────┐
                                      │   Sync      │
                                      │ (only if    │
                                      │  needed)    │
                                      └──────┬──────┘
                                             │
                                      ┌──────▼──────┐
                                      │  Reviews    │
                                      │  queue      │
                                      └──────┬──────┘
                                             │
                               ┌─────────────┴─────────────┐
                               │                           │
                          meaning                     reading
                               │                           │
                               └──────────┬────────────────┘
                                          │
                                   correct?
                                    │        │
                                   yes      no
                                    │        │
                                    │   feedback + requeue
                                    │        │
                                    └──► complete ──► POST /reviews
                                                          │
                                                 network? │
                                                     yes  │   no
                                                          │
                                                   update       outbox
                                                   assignment
                                                          │
                                                          ▼
                                                    Session summary
```

**States of a review item**

```
meaning_pending ──correct──► reading_pending ──correct──► completed
       │                                  │
       └──incorrect──► feedback ──► requeue ┘
```

- A miss increments `incorrect_meaning_answers` or
  `incorrect_reading_answers`, shows the correct answer, and puts the item
  back in the queue a few positions later.
- The item is submitted **once**, when all its parts are correct.
- If the user leaves midway, incomplete items are not sent; they remain
  pending in WaniKani.

---

## 8. Screens

Details are in [ux.md](ux.md). Map:

| Screen | Purpose | Keys | Phase |
|---|---|---|---|
| Boot | Boot, mounting, checks | — | MVP |
| Setup | Wi-Fi, token, time, first sync | Typing, Enter, Esc | MVP |
| Home | Overview and gateway to everything | Enter, L, S, Tab | MVP |
| Sync | Sync progress | Esc | MVP |
| Review | Question and typed answer | Letters, Enter, Esc, Tab | MVP |
| Feedback | Correct/incorrect, correct answer, hint | Enter, Tab | MVP |
| Item detail | Mnemonic, components, notes | ↑↓, Space, Esc | MVP |
| Session summary | Result and next step | Enter, R | MVP |
| Lessons | List of available lessons | ↑↓, Enter, Esc | Phase 2 |
| Lesson card | Teaching before starting | Space, ←→, Enter | Phase 2 |
| Settings | Account, study, sync, device | ↑↓, Enter, Esc | MVP |
| Stats | Level, distribution, accuracy | Esc | Phase 2 |
| Error / empty | What happened and what to do | Enter, Esc | MVP |

![Home](screens/home.png)
![Meaning review](screens/review-meaning.png)
![Correct feedback](screens/review-correct.png)

---

## 9. Architecture

```
                 WaniKani API v2 (HTTPS)
                          │
              ┌───────────▼───────────┐
              │  platform/WkClient    │  HTTP, TLS, streaming JSON
              │  (Arduino/ESP32)      │  rate limit, ETag, retries
              └───────────┬───────────┘
                          │ events / records
              ┌───────────▼───────────┐
              │  wk/ (pure C++)       │  models, answer checker,
              │                       │  romaji→kana, queue, sync
              └───────────┬───────────┘
                          │
              ┌───────────▼───────────┐
              │  store/ (pure C++)    │  subjects, assignments,
              │                       │  outbox, meta, stats
              └───────────┬───────────┘
                          │
              ┌───────────▼───────────┐
              │  app/ (screens)       │  Home, Review, Lesson, Sync...
              └───────────┬───────────┘
                          │
      ┌───────────────────┼───────────────────┐
      │                   │                   │
┌─────▼─────┐      ┌──────▼──────┐     ┌──────▼──────┐
│ board/    │      │ render/     │     │ ui/         │
│ HAL       │      │ glyphs      │     │ theme, list │
│ Cardputer │      │ widgets     │     │ keyboard    │
└───────────┘      └──────┬──────┘     └─────────────┘
                          │
                    ┌─────▼─────┐
                    │ microSD   │  glyph pack + subjects
                    │ LittleFS  │  assignments + outbox
                    └───────────┘
```

### 9.1 Modules

| Module | Responsibility | Arduino? |
|---|---|---|
| `board/` | Display, TCA8418 keyboard, buttons, battery, SD, sleep | Yes |
| `render/` | 4bpp glyphs, 1bpp bitmaps, text, RLE, widgets | Yes (drawing) |
| `ui/` | Theme, layout, ListView, HintBar, KeyInput, texts | Partly pure |
| `app/` | Screens, boot, power, Wi-Fi, settings, portal | Yes |
| `wk/` | Models, answer checker, IME, queue, sync, time | **No** |
| `store/` | Record formats, indexes, atomic writes | **No** |
| `platform/` | HTTP/TLS, Wi-Fi, SD, NVS, time, OTA | Yes |
| `tools/glyph-pack/` | Visual pack generator (Python) | — |
| `tools/screenshots/` | Serial remote for screenshots | — |

Rule: `wk/`, `store/`, and the logic in `ui/` compile and are tested in
PlatformIO's `native` environment without Arduino headers. It's the same
discipline as Tangocho.

### 9.2 State distribution

| State | Where | Why |
|---|---|---|
| Token, Wi-Fi, preferences | NVS | Small, persistent, doesn't depend on the SD |
| Assignments | LittleFS | Critical for knowing what's due; survives the SD being removed |
| Outbox | LittleFS | Critical; written before advancing to the next item |
| Subjects (meanings/readings) | microSD | Large; can be downloaded again |
| Visual pack (glyphs/radicals) | microSD | Large; generated by the user |
| Local statistics | LittleFS | Small; can be rebuilt |
| HTTP cache (ETag/dates) | LittleFS | Small; avoids downloads |

If the card is removed during a session, KaniKey pauses and asks for it to be
inserted. Assignments and the queue remain in LittleFS; progress isn't lost.

---

## 10. Reusing Tangocho

The recommendation is to **fork Tangocho and prune**, not start from scratch or
maintain a shared library from day one. Tangocho has already solved the most
expensive, hardware-specific parts:

- Cardputer ADV HAL with TCA8418, keyboard, SD, battery, and deep sleep.
- 4bpp glyph and 1bpp bitmap rendering, with RLE.
- Theme, widgets, lists, hint bar, and UI font.
- Wi-Fi, portal, TLS with CAs, OTA, atomic storage.
- Serial remote for screenshots and tests.
- Native tests and CI.

What's discarded: the kanji pack format, the multiple-choice quiz, the FSRS SRS,
the deck catalog, and Tangocho's screens. What's adapted: the renderer to read
from a `GlyphSource` instead of a pack, the portal for setup, and the storage for
KaniKey's paths.

Details are in [reuse-tangocho.md](reuse-tangocho.md).

---

## 11. Content and rendering

### 11.1 The problem

WaniKani mixes four subject types:

- **Radicals:** sometimes they have a Unicode character; sometimes just an SVG
  image.
- **Kanji:** a single character.
- **Vocabulary:** a multi-character word.
- **Kana vocabulary:** kana only.

The Cardputer has no PSRAM and the app slot is ~1.75 MB. A full CJK font doesn't
fit; rasterizing on the device is possible but expensive. On the other hand,
WaniKani content can't be distributed with the firmware.

### 11.2 Proposed decision: visual pack on the microSD

A small desktop program, `kanikey-glyphs`, generates a `glyphs.kgp` file for the
card:

1. It requests the subjects up to `subscription.max_level_granted` from the API
   (60 on a paid account, 3 on a free one) using the user's token.
2. It collects all unique characters from kanji, vocabulary, and kana.
3. It renders each character with **Noto Sans JP** (OFL) at 64, 40, and 20 px,
   in 4bpp with RLE.
4. It downloads the radicals' SVG images and rasterizes them locally at 96 px
   (4bpp or 1bpp).
5. It writes an index sorted by codepoint / `subject_id` and the pack to the
   card.

The pack isn't committed or redistributed: it's generated with the user's token,
on their computer, and stays on their card. The firmware only knows how to read
the format.

**Estimated size:** ~2,200 unique kanji + kana + ~500 radicals.

| Part | Approximate size |
|---|---|
| 64 px 4bpp glyphs | 4–5 MB |
| 40 px 4bpp glyphs | 1.5–2 MB |
| 20 px 4bpp glyphs | 0.5–0.7 MB |
| 96 px radicals | 0.5–1 MB |
| Indexes and headers | < 0.2 MB |
| **Total** | **7–9 MB** (with RLE, less) |

It fits easily on any FAT32 microSD. The card is a v1 requirement: without the
visual pack, the item can't be drawn. The app says so clearly and offers a “sync
only” mode in the meantime.

### 11.3 Why not other options

| Option | Problem |
|---|---|
| Full CJK font in flash | Doesn't fit in the slot; software rendering also needs RAM |
| Rasterizing on the device | stb_truetype/nanosvg + cache means more code and more RAM; the result is worse |
| Radical SVGs on the device | Same problem, and the images are WaniKani art that isn't redistributed |
| Universal pack in the repo | Would include WaniKani art/content; not legally clean |
| Printing the radical meaning as text | Loses the symbol, which is what's being reviewed |

The generated-pack option best balances quality, memory, and licensing. In the
future, a generator in the phone's browser could be added so no computer is
needed, or on-device rasterization for missing characters.

### 11.4 Rendering

`render/Renderer` is adapted to an interface:

```cpp
class GlyphSource {
public:
    virtual bool glyph(uint32_t codepoint, uint8_t size, GlyphBitmap* out) = 0;
    virtual bool radical(uint32_t subjectId, uint8_t size, GlyphBitmap* out) = 0;
};
```

- UI glyphs: `UiFont.h` (Latin + kana) generated as in Tangocho.
- Kanji/vocabulary: `GlyphSource` from the SD, with a small cache (16 KB).
- Radicals: 1bpp/4bpp bitmap, drawn in white on black.
- Meaning text: UiFont; Japanese text: glyphs from the pack.
- Text is composed character by character for vocabulary words.

---

## 12. Sync and data

Summary; details are in [api-sync.md](api-sync.md).

- **Authentication:** personal token in `Authorization: Bearer`; revision
  `Wanikani-Revision: 20170710`.
- **Endpoints:** `/user`, `/summary`, `/assignments`, `/subjects`,
  `/study_materials`, `POST /reviews`, `PUT /assignments/{id}/start`.
- **Cache:** `summary` 30–60 min; assignments with `updated_after`; subjects
  immutable per ID; ETag/If-None-Match when possible.
- **Review:** one `POST /reviews` per completed item, with
  `incorrect_meaning_answers` and `incorrect_reading_answers`. The response
  carries the updated assignment (`resources_updated.assignment`), which is
  stored.
- **Offline:** the completed review is written to the outbox **before**
  advancing. When the network returns, it's sent in order, one at a time.
- **Duplicates:** the API has no idempotency key. If a POST is left in doubt
  (timeout), it isn't retried blindly: the assignment is queried and reconciled
  by `available_at`/`data_updated_at`. It's a known risk and is documented in
  the UI.
- **Time:** the API uses UTC. The Cardputer has no battery-backed RTC; the time
  is learned via SNTP or the phone portal. Without reliable time, offline
  reviews are queued without `created_at` and sent with the server's time.

---

## 13. Memory and performance

### 13.1 Budget

| Usage | Estimate |
|---|---|
| Canvas 240×135×2 | 64.8 KB |
| Loop stack | 16 KB |
| TLS (sync only) | ~50 KB |
| Streaming JSON parser | 8 KB |
| Subject record being built | 8 KB |
| Glyph cache | 16 KB |
| UI, strings, lists | ~20 KB |
| **Peak during sync** | **~183 KB** |
| **Free heap target during sync** | **> 35 KB** |
| **Free heap target during review** | **> 80 KB** |

Tangocho measured ~210 KB free at boot on the Cardputer and ~60 KB at the worst
point of a TLS handshake with the canvas and a pack open. KaniKey doesn't open
packs, but it adds a parser and a store; it must be measured from the first
prototype, with a `logNetHeap()` on every connection.

### 13.2 Rules

- **Never** a full JSON document in RAM. The parser reads in chunks and emits
  one subject at a time.
- **Never** a CJK font in RAM. Glyphs from the SD, with a small cache.
- **Never** download all subjects on first boot. Fetch only the IDs that are
  needed (reviews/lessons) and grow the cache.
- **Never** keep the radio on outside a sync.
- **Always** write the outbox before advancing to the next item.
- **Always** measure `heap_caps_get_largest_free_block()` before and after TLS.

### 13.3 Screen performance

- Full frame: 64.8 KB; at 40 MHz SPI ≈ 13 ms push.
- New item: full frame.
- Typing: only the answer field region (partial push).
- Caret: blinks every 500 ms, 2×20 px region.
- No animations that block input. Review must respond to the keyboard in under
  50 ms.

---

## 14. Security, privacy, and legal

### 14.1 Token

- Stored in NVS, in the app's namespace.
- Never written to logs, the full screen, the portal, or backups.
- The account screen shows it masked (`••••••••`) and allows deleting it.
- Signing out erases the token, the assignment cache, and the outbox. If reviews
  are still pending, the user is warned and asked to confirm before discarding
  them.
- NVS isn't encrypted by default. It's an accepted risk for a personal project;
  the token can be revoked in WaniKani. It's documented.

### 14.2 Network

- HTTPS with certificate verification against a set of root CAs, like Tangocho.
  Never `setInsecure()`.
- The setup portal is temporary: a WPA2 network with a random key shown on
  screen, only during setup; it isn't served on the home network.
- There's no telemetry and no own server. Requests go directly to
  `api.wanikani.com` and to WaniKani's CDN for radical images (only in the pack
  generator).

### 14.3 Brand and content

- The product name doesn't include “WaniKani”. The subtitle says “unofficial
  WaniKani companion”.
- The UI is its own: no WaniKani colors, illustrations, mascots, or layouts are
  copied.
- The repository contains no WaniKani meanings, readings, mnemonics, radical
  images, audio, or context sentences. Test fixtures are minimal, hand-written,
  and attributed, as in Tangocho.
- The visual pack is generated with the user's token and stays on their card.
- `subscription.max_level_granted` is respected: no content above the allowed
  level is cached.
- Personal project, non-commercial.

### 14.4 Licenses

| Component | License | Use |
|---|---|---|
| Tangocho (code) | MIT | Fork base; keep the notice |
| Kakehashi (inspiration) | MIT | Flow/answer checker ideas; attribute if code is copied |
| Noto Sans JP | OFL 1.1 | UI glyphs and visual pack |
| KanjiVG (phase 2) | CC BY-SA 3.0 | Stroke order; attribute |
| WaniKani API/content | Tofugu Terms | User token only, personal use |

---

## 15. Testing

### 15.1 Native (no hardware)

- `RomajiIme`: vowels, digraphs, っ, ん, ー, n before a vowel, deletion,
  cursor, uppercase, mixed input.
- `AnswerChecker`: accepted meanings, synonyms, blacklist/whitelist, primary vs
  valid readings, okurigana, katakana→hiragana, meaning/reading confusion,
  `n` error.
- `ReviewQueue`: order, requeueing, counters, completed item, exiting midway.
- `Outbox`: append, compaction, retries, reconciliation, corruption.
- `JsonStream`: WaniKani fixtures with radicals, kanji, vocabulary, kana,
  pagination, errors, `\uXXXX` escapes, invalid UTF-8.
- `Store`: atomic writes, CRC, `.bak` recovery, version migration.
- `SyncPolicy`: what to request, when, ETag, `updated_after`, rate limit.
- `Time`: ISO 8601, UTC/local, `created_at`, unreliable clock.

### 15.2 Hardware

- Keyboard: all keys, Shift, Fn, Ctrl/Opt/Alt, auto-repeat, ghosting.
- Text: type 200 characters in a row without losing events.
- SD: glyph reads, hot removal, FAT32, full card.
- Wi-Fi/TLS: handshake, free heap, 429, invalid certificate, slow DNS.
- Display: sunlight readability, angles, 9–20 px sizes.
- Battery: 30 min review, sync, deep sleep, wake on key.
- Sleep: save outbox and assignments before sleeping.
- OTA: update, rollback, corrupted image.

### 15.3 CI

- `pio run -e cardputer-adv`.
- `pio test -e native`.
- Release build with `KK_VERSION`.
- Automated screenshots with the serial remote, like Tangocho.
- `clang-format`/`clang-tidy` if Tangocho's style is adopted.

### 15.4 Observability

- `KK_LOG` serial log with session, sync, API, and memory events; never the
  token.
- `logNetHeap("sync")` before/after TLS.
- Counters: requests, 429s, bytes, sync time, items, failures.
- Diagnostics screen in Settings › About (version, heap, last sync).

---

## 16. Roadmap

### M0 — Design and bring-up (done 2026-10-25)

- This document, UX, API, reuse, and mockups.
- Decisions accepted.
- M0 firmware on the Cardputer: display, keyboard controller, microSD, Wi-Fi,
  TLS and an authenticated `GET /user` (200).

**Output:** accepted decisions, a repository skeleton and the first hardware
measurements ([hardware.md](hardware.md)).

### M1 — Home and data (in progress)

- Tangocho modules integrated; compiles for Cardputer.
- HAL, display, keyboard, SD, sleep.
- Wi-Fi/token setup (dev path done); time and the production portal next.
- `GET /user` done; `/summary`, assignments and subjects next.
- Home with real data and cache.

**Output:** you turn it on, configure it, and see “28 reviews” from your account.

### M2 — Visual pack and online review

- `kanikey-glyphs` generator and reading the pack from the SD.
- Sync of assignments + subjects for pending reviews.
- Review screen, IME, answer checker, feedback.
- `POST /reviews` and session summary.

**Output:** a full review session against WaniKani.

### M3 — Offline and queue

- Persistent outbox, reconciliation, retries, rate limit.
- Boot without network, subject cache, “offline” state.
- Sync screen with progress.

**Output:** review on the subway and sync when you get out.

### M4 — Lessons

- Lesson list, card with mnemonic/components.
- `PUT /assignments/{id}/start`.
- Optional local practice.

**Output:** full lessons on the device.

### M5 — Polish

- Statistics, full settings, OTA, About.
- Accessibility, contrast, error texts.
- Screenshots and user documentation.

**Output:** shippable v1.0.

### M6 — Extras

- Audio, stroke order, FSRS practice, forecast, synonyms.
- Pack generator from the phone.

---

## 17. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Insufficient memory with TLS + canvas + parser | High | Streaming parser, small cache, measure in M0, free cache before TLS |
| Duplicating a review due to timeout | High | Don't retry blindly; reconcile with the assignment; outbox with state |
| Visual pack hard to generate | Medium | Simple Python tool, clear instructions, “sync only” fallback |
| API changes or rate-limits | Medium | Pinned revision, fixtures, isolated `wk` layer, clear errors |
| Keyboard/IME edge cases | Medium | Exhaustive native tests, visible feedback, allow editing |
| No reliable RTC | Medium | SNTP/portal, last known time, warning and optional `created_at` |
| SD slow, full, or removed | Medium | Cache, atomic writes, pause and clear message |
| Readability on 1.14" | Medium | Life-size mockups, sunlight tests, 9–20 px type |
| Battery with Wi-Fi | Low | Radio only during sync, deep sleep, minimal sync |
| Huge scope | High | Phases; lessons out of the MVP; no WaniKani parity |
| Brand/content | Medium | No art or content in the repo; name without “WaniKani”; review terms |

---

## 18. Open decisions

| # | Decision | Recommendation | Why |
|---|---|---|---|
| 1 | Name | KaniKey (provisional) | Short, keyboard, doesn't include “WaniKani” |
| 2 | Base | Pruned Tangocho fork | The Cardputer HAL is the most expensive part |
| 3 | Visual pack | Python generator + SD | Best quality/memory; no content in the repo |
| 4 | Offline | Enabled by default | It's the point of the device; with reconciliation |
| 5 | Lessons | Phase 2 | The MVP is already big; solid reviews first |
| 6 | UI language | English only | The project is English; WaniKani content is English |
| 7 | Correction | Strict + warnings | Faithful to WaniKani; warnings prevent frustration |
| 8 | Session size | 20 by default, “all” optional | Short sessions on a small device |
| 9 | SD required | Yes in v1 | No glyphs, no review; it's said clearly |
| 10 | License | MIT | Compatible with Tangocho and Kakehashi |

---

## 19. References

- WaniKani API v2, revision 20170710: <https://docs.api.wanikani.com/20170710/>
- WaniKani branding rules: <https://docs.api.wanikani.com/20170710/#branding-rules>
- Tangocho: <https://github.com/dcarnicer/tamagotcho>
  - `docs/architecture.md`, `docs/ux-refresh.md`, `docs/microsd.md`,
    `docs/decisions.md`
- Kakehashi: <https://github.com/Portego-00/Kakehashi>
  - Review flow, answer checker, offline queue, lessons
- M5Stack Cardputer ADV: <https://docs.m5stack.com/en/core/Cardputer-Adv>
- Noto Sans JP (OFL): <https://fonts.google.com/noto/specimen/Noto+Sans+JP>
- KanjiVG (CC BY-SA, phase 2): <https://kanjivg.tagaini.net/>

# KaniKey — what to reuse from Tangocho

> Design, 2026-10-25. Tangocho is taken as the base at
> `33a28f670b07e6c574d98a9c40550a7aac3bf537` (v0.7.0, 2026-10-24), MIT license.
>
> **Implementation note (2026-10-25):** the first firmware milestone copies the
> needed modules (`board/`, `ui/KeyInput.h`, `platform/RootCerts.h`) into this
> repository instead of forking Tangocho's full history. The rest of the reuse
> matrix still applies; a full fork remains an option if the two projects need
> to share hardware fixes.

---

## 1. Why

The Cardputer ADV is already proven on hardware inside Tangocho:

- Display, fixed landscape rotation and back buffer.
- TCA8418 keyboard, events, repeat, letters and arrows.
- microSD FAT32, with its pins, mount and hot removal.
- Wi-Fi, portal, TLS with root CAs, OTA and deep sleep.
- 4bpp glyph rendering, 1bpp bitmaps, RLE, widgets and UI font.
- Serial remote to press keys and read the screen in tests.

That is exactly the part that brings no differential value to KaniKey and that
would cost weeks to debug again. The new product is the review engine, the
WaniKani API, the IME and the screens.

---

## 2. Recommended strategy: pruned fork

1. Create `kanikey` as a fork of Tangocho at the given commit.
2. Keep `upstream` as a remote so hardware fixes can be brought in.
3. Prune in a `bootstrap` branch:
   - out `pack/`, `quiz/`, `srs/`, `catalog/`, `portal/` (except helpers),
     `app/` for reviews and decks;
   - out `tools/pack-builder/` and the packs;
   - in `board/`, `render/`, `ui/`, `platform/`, `tools/screenshots/`,
     `tools/ui-font/`.
4. Rename the `tgc` namespace → `kk` (or keep `tgc` during the transition).
5. Build `cardputer-adv` and run the native tests.
6. From there, build `wk/`, `store/` and the new screens.

A shared library from day one is not recommended: it adds versioning,
submodules and coordination between two projects for a pet project. If Tangocho
and KaniKey stay alive, extracting a `cardputer-kit` later is easier than
maintaining it now.

---

## 3. Reuse matrix

| Tangocho module | What it does | Decision | Changes for KaniKey |
|---|---|---|---|
| `board/Board.h` | HAL: display, buttons, keyboard, SD, battery, sleep | **Stays** | Add text events + modifiers; G0 |
| `board/BoardCardputerAdv.cpp` | TCA8418, SD, deep sleep, battery | **Stays** | Extend the key-to-character mapping; nothing else |
| `board/BoardM5.cpp` | Single StickS3+Cardputer image | **Dropped** | Cardputer only in v1 |
| `render/Renderer` | 4bpp glyphs, text, cache | **Adapted** | `GlyphSource` instead of pack; same primitives |
| `render/Rle` | Glyph compression | **Stays** | Reuse in the visual pack |
| `render/Widgets` | Header, tiles, rows, pills, bars | **Adapted** | New tokens and some component (input, feedback) |
| `render/Blend565`, `GlcdCp437` | Blending and CP437 map | **Stays** | No changes |
| `ui/Theme.h` | Tangocho tokens | **Rewritten** | KaniKey palette and typography |
| `ui/ListView.h` | Focus, window, scroll | **Stays** | No changes |
| `ui/HintBar.h` | Hint bar and keycaps | **Stays** | New texts/keys |
| `ui/KeyInput.h` | TCA8418 → actions map | **Extended** | Normal/Shift/Fn layers, characters, modifiers |
| `ui/TextLayout.h` | Wrap and pagination | **Stays** | Reuse in mnemonics |
| `ui/UiFace.h`, `UiFont.h` | Pre-rendered UI font | **Adapted** | Add 10/14/20 px and needed glyphs |
| `ui/Icons.h` | 1bpp icons | **Adapted** | Check, cross, wifi, sync, SD, key |
| `app/Display` | Canvas, present, rotation | **Stays** | No rotation; partial push for input |
| `app/ScreenKit` | Header, hints, lists, errors | **Adapted** | New 240×135 layout and texts |
| `app/Input` | Hold, keyboard, keyAction | **Rewritten** | Text mode vs navigation |
| `app/Power` | Dim, idle, sleep, battery | **Stays** | Adjust timings and save outbox before sleeping |
| `app/Clock` | Date/time, SNTP | **Adapted** | UTC/ISO 8601, `clock_trusted` |
| `app/Wifi`, `WifiMenu` | Wi-Fi and portal | **Adapted** | Setup-only portal; WPA2 |
| `app/Net` | Network sessions, heap | **Adapted** | `WkClient` instead of deck catalog |
| `app/Firmware` | OTA | **Stays** | Change manifest/URL |
| `app/Remote` | Serial remote for screenshots | **Stays** | Add text keys |
| `platform/Storage` | LittleFS + SD, paths | **Adapted** | `/kanikey/...` paths; assignments/outbox |
| `platform/HomeWifi` | Home Wi-Fi in NVS | **Stays** | No changes |
| `platform/NetFetch` | HTTP/TLS, streaming to sink | **Adapted** | API client with JSON streaming |
| `platform/WifiPortal` | HTTP portal | **Ported, reduced** | Setup: Wi-Fi + token + time + pack; no decks/OTA |
| `platform/Ota` | OTA with manifest | **Stays** | KaniKey manifest |
| `platform/RootCerts` | Root CAs | **Stays** | Add WaniKani's if missing |
| `platform/Psram` | PSRAM detection | **Stays** | Cardputer doesn't have it; don't assume |
| `pack/` | `.tgc` format | **Dropped** | Only the idea of tables + RLE is reused |
| `quiz/` | Multiple-choice questions | **Dropped** | Written answer |
| `srs/` | Local FSRS | **Dropped** | WaniKani rules; optional for extra practice |
| `catalog/` | Deck catalog | **Dropped** | Not applicable |
| `portal/` | Deck portal logic | **Dropped** | KaniKey's portal is for setup |
| `tools/screenshots/` | Screenshots over serial | **Stays** | Adapt scenarios |
| `tools/ui-font/` | UiFont generator | **Stays** | Add sizes/glyphs |
| `tools/pack-builder/` | KANJIDIC2, packs | **Dropped** | New `tools/glyph-pack/` |

---

## 4. What not to reuse (and why)

- **The `.tgc` pack format.** It is designed for kanji cards with
  meanings/readings and distractors. WaniKani has radicals, vocabulary, kana,
  mnemonics, components and assignments. Forcing it would be worse than a new
  format.
- **The multiple-choice quiz.** The charm of the Cardputer is typing. A
  multiple-choice test would be a worse version of Tangocho.
- **FSRS.** WaniKani already has its SRS; running two schedulers creates
  divergence. If there is ever "extra practice", it can be a separate module
  that sends nothing to WaniKani.
- **The deck catalog and its portal.** There are no decks; there is an account.
- **The single StickS3+Cardputer image.** It adds branches and tests; v1 is
  Cardputer only.
- **The WaniKani importer in `tools/pack-builder`.** It only downloads unlocked
  kanji and builds a static `.tgc`; it brings no assignments, vocabulary,
  radicals, reviews or lessons. It serves as a reference for token handling,
  pagination and the `subjects` fields, but KaniKey's sync engine is new.

---

## 5. Hardware lessons that are already paid for

Numbers measured in Tangocho (Cardputer ADV, no PSRAM):

| Item | Data |
|---|---|
| Canvas 240×135×2 | 64.8 KB |
| Free heap at boot | ~210 KB |
| TLS handshake | ~50 KB; minimum seen ~60 KB free with canvas and pack |
| SD mounted | ~13–15 KB of RAM (FatFs, buffers) |
| Full frame | 64.8 KB ≈ 13 ms over SPI at 40 MHz |
| SD pins | SCK 40, MISO 39, MOSI 14, CS 12; `otherCs` 5 for the LoRa Cap |
| Keyboard | TCA8418 on internal I²C, INT on G11 |
| Display | fixed rotation 1, keyboard underneath |
| Battery | G10, 1:2 divider |
| Sleep | wakes on G0 or any key (EXT1 on G11) |
| Clock | no battery-backed RTC; SNTP or portal |

KaniKey must respect those figures. In particular:

- Don't open a Tangocho pack; but the canvas and TLS are still there.
- The JSON parser and the store can't look like a DOM.
- The glyph cache must fit in ~16 KB.
- Sync must free the cache before TLS.
- Sleep must save the outbox and the assignments.

---

## 6. Kakehashi as inspiration

Kakehashi is an iOS/Android WaniKani app, MIT, with a very complete
implementation. Its React Native code cannot be reused in the firmware, but its
product decisions can:

- **Written answer** with romaji-to-kana conversion and text editing.
- **Answer checker** with accepted meanings, synonyms, blacklist/whitelist,
  primary vs valid readings, okurigana and warnings of "n" before a vowel.
- **Stable session:** the list is frozen at the start; new reviews do not
  appear mid-way.
- **Save before advancing:** the result is persisted before the next
  question; the queue is synced afterwards.
- **Lessons** with card, mnemonic and components.
- **SRS stage indicator** after completing an item.
- **Honest offline states** and bounded retries.

If specific code is copied (for example, the normalization table or the checker
tests), Pedro Ortego's MIT notice must be kept. The UI and the art are not
copied.

---

## 7. Licenses and notices

- **Tangocho:** MIT, Copyright (c) 2026 Tamagotcho contributors. The fork must
  keep `LICENSE` and the notice in the copied files.
- **Kakehashi:** MIT, Copyright (c) 2026 Pedro Ortego. Add a notice if code is
  copied; if only ideas are used, mention it in `THIRD_PARTY.md`.
- **Noto Sans JP:** SIL OFL 1.1. Include the license and the notice when
  distributing the visual pack.
- **KanjiVG:** CC BY-SA 3.0, only if stroke order is added.
- **WaniKani:** content © Tofugu LLC; not redistributed. The visual pack is
  generated with the user's token and stays on their card.

---

## 8. Relative effort

| Block | Effort | Note |
|---|---|---|
| Fork + prune + build | S | Mostly deleting and renaming |
| Adapt render to `GlyphSource` | M | Keep the primitives |
| Keyboard with text/layers | M | TCA8418 already works; the character map is missing |
| `wk/` + `store/` + tests | L | The most technical part |
| `WkClient` + JSON streaming | L | Memory and TLS |
| Review screens | M | The UX is already designed |
| Lessons | M | Paginated text and `start` |
| Outbox/reconciliation | M | Careful with duplicates |
| Visual pack (Python) | M | Font, SVG, RLE |
| Polish/OTA/docs | M | Can be iterated |

These are not calendar commitments; it is a way to see where the risk is. The
first week of firmware should be a `GET /user` on the Cardputer with TLS and
heap measurement, before writing screens.

---

## 9. Fork risks

| Risk | Mitigation |
|---|---|
| Dragging deck code along unintentionally | Aggressive pruning; build and clear warnings |
| Divergence from Tangocho | Keep `upstream`; cherry-pick HAL fixes |
| Confusing namespace and names | Rename `tgc` to `kk` in a dedicated branch |
| Tangocho tests that no longer apply | Keep the render/UI ones; delete the pack/quiz/SRS ones |
| License | Keep MIT and notices from the first commit |

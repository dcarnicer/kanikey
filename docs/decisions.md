# Decisions

Short log of design decisions. Status: **accepted** (2026-10-25). Format:
date, decision, why and what it implies.

---

## 2026-10-25 — KaniKey is a companion, not a substitute

**Decision:** KaniKey syncs with WaniKani and shows reviews/lessons. The
SRS, assignments and progress stay in WaniKani.

**Why:** running two schedulers creates divergence and duplicates work. The
value is in the device and in the writing, not in reimplementing WaniKani.

**Implications:** there is no FSRS for reviews; intervals are not invented; the API is
the source of truth.

---

## 2026-10-25 — Written answer, not a multiple-choice test

**Decision:** the user types the meaning and reading. There are no four options in
the main flow.

**Why:** it is the heart of WaniKani and the reason to use a keyboard. A multiple-choice
test would turn KaniKey into a worse Tangocho.

**Implications:** romaji→kana IME, answer checker, text editing and keyboard in two
modes. A "test mode" may come later as accessibility.

---

## 2026-10-25 — Offline by default, with an outbox

**Decision:** you can review without a network; completed reviews are saved in a
persistent outbox and sent when the connection returns.

**Why:** that is the appeal of a pocket device. Kakehashi shows it can be done
without blocking the session.

**Implications:** write before advancing, reconcile ambiguous submissions and show
pending status honestly. The API has no idempotency; you have to be careful
with timeouts.

---

## 2026-10-25 — Cardputer ADV only in v1

**Decision:** the firmware is optimized and tested only for the Cardputer ADV.

**Why:** the keyboard is the product; other boards change the layout and the
tests. Tangocho already maintains several.

**Implications:** the HAL for other boards can be kept in the fork, but support
is not promised. The layout is 240×135 landscape.

---

## 2026-10-25 — Pruned fork of Tangocho

**Decision:** KaniKey starts as a fork of Tangocho at v0.7.0, keeps HAL,
render, base UI, Wi-Fi, SD, OTA and tests, and drops packs/quiz/SRS/catalog.

**Why:** the hardware is already solved and tested; rewriting it is the expensive
and boring part.

**Implications:** keep the MIT notice, an `upstream` remote and an aggressive prune.
If both projects grow, extract a `cardputer-kit` later. The first milestone uses
a source copy of `board/`, `ui/KeyInput.h` and `platform/RootCerts.h` rather than
a full-history fork; that is an implementation detail, not a change of direction.

---

## 2026-10-25 — Visual pack on the microSD

**Decision:** kanji/vocabulary glyphs and radicals are generated on the
computer with `kanikey-glyphs` and stored in `/kanikey/glyphs.kgp` on the SD.

**Why:** a CJK font does not fit in flash; rasterizing on the device wastes
RAM and is more code. The pack gives quality and keeps the firmware simple.

**Implications:** the SD is required to review. The pack is generated with the user's
token and is not redistributed. If a glyph is missing, the item is blocked with a warning.

---

## 2026-10-25 — M1 uses efontJA_16 as a temporary glyph source

**Decision:** the first review screen renders kanji/vocabulary with M5GFX's
`efontJA_16` (16 px, scaled 2–3×) instead of waiting for the visual pack.

**Why:** the Home screen is live and the next visible milestone is a real review.
The font is already in M5GFX, covers ~10,800 Japanese glyphs and adds ~468 KB of
flash; the app still fits its 1.75 MiB slot at 88.6%.

**Implications:** this is a bring-up fallback, not a reversal of the visual-pack
decision. The pack remains the final path for 64 px prompts, radical images and
a smaller flash footprint. Remove the efont reference when the pack lands.

**Decision:** the MVP focuses on reviews. Lessons come later.

**Why:** reviews already require API, cache, IME, answer checker, outbox and
screens. Adding lessons and mnemonics at the same time multiplies the scope.

**Implications:** the home screen can show the lesson counter from the start,
but the lessons screen is postponed.

---

## 2026-10-25 — English-only UI

**Decision:** interface texts, documentation, code and comments are in English.
There is no localization in v1. Meanings/readings are WaniKani's (English).

**Why:** the project is meant to be published and WaniKani content is English;
one language keeps the 240×135 UI and the string budget simple.

**Implications:** `UiStrings` still centralizes every on-device string, but only
the English table is built. Localization can be added later if wanted, but it is
not planned.

---

## 2026-10-25 — Idle dim, blank, then deep sleep

**Decision:** the panel dims after 30 s, blanks after 60 s and the device
deep-sleeps after 2 min when idle. Deep sleep is skipped while a review is
active and on external power; the outbox is already saved to LittleFS.

**Why:** a pocket device must not drain its battery on the Home screen, but a
review session must not disappear just because the user is thinking.

**Implications:** the first key on a blank panel only wakes it. On wake from
deep sleep the device boots and syncs again; the outbox retries pending reviews.

---

## 2026-10-25 — Strict checking with warnings

**Decision:** WaniKani's exact answers are accepted (including user
synonyms and whitelist). Valid non-primary readings, incorrect okurigana
and the `n` error show a warning and allow retrying, without counting
as a failure automatically.

**Why:** it is faithful to WaniKani and avoids punishing valid knowledge for a
keystroke. A "strict" setting can count the warning as a failure.

**Implications:** the answer checker returns a result with a type (`Precise`,
`Imprecise`, `Warning`, `Incorrect`) and the UI decides.

---

## 2026-10-25 — No backend of our own

**Decision:** the Cardputer calls `api.wanikani.com` directly with the user's
token. There is no KaniKey server.

**Why:** less infrastructure, less privacy to give up and less latency.
The API allows CORS and personal tokens.

**Implications:** TLS, rate limit and streaming JSON on the device. The setup
portal is local and temporary.

---

## 2026-10-25 — WPA2 and temporary setup portal

**Decision:** the configuration portal is served on its own WPA2 network, with a
random key on screen, and it shuts down when done. It is not served on the home
network.

**Why:** the token must not travel over an open network. A permanent portal is not
needed because the device does the syncing.

**Implications:** the user configures from the phone once; after that everything is done
with the keyboard. The portal reopens only when requested.

---

## 2026-10-25 — No WaniKani content in the repository

**Decision:** meanings, readings, mnemonics, radical images,
audio and context sentences are not committed. The test fixtures are minimal,
hand-written and attributed.

**Why:** WaniKani's terms and respect for Tofugu's work. The visual
pack is generated with the user's token.

**Implications:** the repo can be published without WaniKani material; setup
includes generating/copying the pack.

---

## 2026-10-25 — Sessions of 20 by default

**Decision:** a review session takes 20 items by default; "all" is an
option.

**Why:** on a small device, a 200-item session is a trap. 20 is
a manageable unit and can be repeated.

**Implications:** the summary offers "review more"; the queue is frozen per session.

---

## 2026-10-25 — Working name KaniKey

**Decision:** the project is called KaniKey until there is a better one.

**Why:** *kani* + *key*, short, evokes keyboard and kanji, and does not include
"WaniKani" in the product name (brand rule).

**Implications:** the name can change before publishing; the documents and the
namespace can keep using `kanikey`.

---

## 2026-10-25 — MIT license (proposed)

**Decision:** the code would be published under MIT, like Tangocho and Kakehashi.

**Why:** compatibility with the fork and with the copied ideas; it is the usual
thing in this kind of project.

**Implications:** keep the Tangocho/Kakehashi notices and the font
licenses. WaniKani content is not licensed with the code.

---

## 2026-10-25 — Audio and stroke order out of v1

**Decision:** they are not implemented in the MVP.

**Why:** they add audio decoding, memory, speaker and extra data. Written
review already justifies the device.

**Implications:** the design leaves room for them (item card, settings, visual
pack) but does not promise them.

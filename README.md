# KaniKey

**A pocket-sized review terminal for WaniKani, built for the M5Stack Cardputer ADV.**

> **Status: firmware preview — offline cache (2026-10-28).** The Cardputer ADV
> shows a live Home from `/user` + `/summary`, fetches a 20-item due batch,
> runs a typed meaning/reading session and submits completed items with
> `POST /v2/reviews` (201). A persistent outbox makes reviews survive a reboot
> and offline sessions. Home, the user and the review batch are now cached too:
> after a reboot without network the device restores them, shows `Offline` and
> can run a cached session. Lessons fetch five at a time, show meaning/reading
> and mnemonic pages, and start with `PUT /assignments/{id}/start` (200).
> Japanese prompts and readings come entirely from the microSD visual pack at
> 64/32/20 px: 7,269 glyphs + 498 radical bitmaps in 3.7 MB, generated locally
> with the user's token. No WaniKani content is in the repository.
> `efontJA_16` has been removed; missing glyphs show an explicit state. First
> boot brings up a WPA2 setup portal (`KaniKey-XXXX`, key on screen): the phone
> saves the Wi-Fi, the device joins it, and the station page validates the
> WaniKani token with `GET /user`, sets the clock/time zone from the browser and
> checks the visual pack. Setup makes no lesson-start calls; validation only reads `/user`. Normal boots
> show setup/sync states; the M0 debug screen is only in the development
> build. The lesson list/components and dismissing rejected outbox items are
> next.
>
> KaniKey is an **unofficial** third-party app for WaniKani. It is not
> affiliated with, endorsed by or sponsored by WaniKani or Tofugu LLC. WaniKani
> is a registered trademark of Tofugu LLC.

![KaniKey screens](docs/screens/overview.png)

## The idea

WaniKani is reviewed by typing: meaning in English and reading in kana. The
Cardputer ADV has a 240×135 screen, Wi-Fi, a microSD card, a battery and, above
all, a real 56-key keyboard. That makes it a much more natural device for
WaniKani than a watch or a two-button console: you open it, the pending review
appears, you type the answer and close it. No phone, no notifications, no
distractions.

KaniKey doesn't replace WaniKani: it is a **review and lesson terminal** that
syncs with the user's account. The SRS still lives in WaniKani.

## What it will do (MVP)

- Sync the summary, the assignments and the subjects that are due.
- Review with **typed answers**: meaning and reading, with romaji-to-kana
  conversion as you type.
- Show the correct answer, the accepted meanings/readings and a short hint when
  you get it wrong; the item comes back later in the same session.
- Send each review to WaniKani as you complete it, with the failure counters the
  API expects.
- **Offline mode**: sessions from the local cache and a queue of pending reviews
  that syncs when the network comes back.
- Lessons (phase 2): meaning card, readings, mnemonic and components; then
  `PUT /assignments/{id}/start`.
- Home screen with pending reviews/lessons, next review time and the breakdown by
  SRS stage.
- Settings, Wi-Fi, time, OTA and clear error states.
- UI in English (WaniKani meanings are in English).

## What it won't do (for now)

- It isn't a copy of the WaniKani website or its interface.
- It doesn't ship WaniKani content in the repository: meanings, readings,
  mnemonics, radical images and audio are requested with the user's personal
  token and stored only on their card.
- It doesn't implement a local SRS for reviews: scheduling is WaniKani's. (An
  extra practice mode with FSRS could come later.)
- It doesn't include a community, forum, app-store reviews, push notifications or
  a mobile app.
- It doesn't support other boards in v1. Tangocho's HAL allows adding them, but
  the design is optimized for the Cardputer ADV.

## Target hardware

| | Cardputer ADV |
|---|---|
| MCU | ESP32-S3FN8, 8 MB flash, **no PSRAM** |
| Display | 1.14" ST7789V2, 240×135, landscape only |
| Keyboard | 56 keys (4×14) behind a TCA8418; G0 separate |
| Storage | microSD FAT32 + LittleFS |
| Network | 2.4 GHz Wi-Fi |
| Battery | yes; deep sleep with wake on keypress |
| Audio | ES8311 codec + speaker (later phase) |

The hardest part —keyboard, display, microSD, Wi-Fi/TLS, sleep and OTA— is
already solved and tested on hardware in
[Tangocho](https://github.com/dcarnicer/tamagotcho). KaniKey reuses that layer.

## Documents

| Document | What it contains |
|---|---|
| [ROADMAP.md](ROADMAP.md) | Current status and prioritized remaining work |
| [docs/design.md](docs/design.md) | Product, scope, architecture, memory, risks and roadmap |
| [docs/ux.md](docs/ux.md) | Visual language, keyboard, flows and screen by screen |
| [docs/api-sync.md](docs/api-sync.md) | WaniKani API, cache, offline queue and data model |
| [docs/reuse-tangocho.md](docs/reuse-tangocho.md) | What is reused from Tangocho and how it is ported |
| [docs/setup.md](docs/setup.md) | Token, visual pack, first boot and troubleshooting |
| [docs/decisions.md](docs/decisions.md) | Short log of accepted decisions |
| [docs/hardware.md](docs/hardware.md) | Measured Cardputer ADV results (heap, SD, Wi-Fi, TLS) |
| [docs/screens/](docs/screens/) | 240×135 mockups (individual at 4×; contact sheet at 2×) |
| `tools/shot.py` | Live canvas capture over USB (not committed; `tools/screenshots/out/`) |
| [firmware/README.md](firmware/README.md) | Current build, flash and serial commands |

The documents, code comments and UI strings are in English, as in Tangocho, so
the project can be published.

The mockups are regenerated with:

```sh
python3 tools/mockups/render.py
```

By default the script reuses the Noto Sans JP font that Tangocho downloads into
`tools/pack-builder/cache/noto/`. If it is somewhere else:

```sh
KANIKEY_FONT=/path/NotoSansJP-Regular.otf python3 tools/mockups/render.py
```

## Secret scanning

GitHub Actions runs [gitleaks](https://github.com/gitleaks/gitleaks) on every
push to `main` and on pull requests, scanning the full history. Locally:

```sh
brew install gitleaks
gitleaks detect --source . --verbose --redact
```

## Name

**KaniKey** is a working name: *kani* (crab, as in WaniKani) + *key*. It doesn't
include "WaniKani" in the product name, as the API's branding rules require. If
the name doesn't stick, it can be changed before release; what matters in the
design doesn't depend on it.

## License

KaniKey's own license is still to be decided. The code adapted from Tangocho
(MIT) and the other third-party components are credited in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). No WaniKani content is
distributed in the repository.

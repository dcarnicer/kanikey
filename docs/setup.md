# KaniKey — setup

> **Implemented 2026-10-28.** First boot starts a WPA2 setup portal on the
> device. The token is validated with `GET /user`; setup makes no lesson or
> review writes. The visual pack is still generated on a computer.

---

## 1. What you need

- M5Stack Cardputer ADV.
- **FAT32** microSD card (exFAT won't work; the Arduino-ESP32 framework doesn't
  support it).
- WaniKani account and a **personal token** with read permissions for
  assignments, subjects, study materials, user and summary, and create
  permission for reviews.
- A computer with Python 3.11+ and [`uv`](https://docs.astral.sh/uv/) to
  generate the visual pack the first time.
- **Noto Sans JP** font (OFL); the generator downloads it or accepts a local
  path.

The card is required for the glyphs. Without a visual pack you can sync,
but not review kanji/vocabulary.

---

## 2. Creating the WaniKani token

1. Go to WaniKani → **Settings → API Tokens**.
2. Create a new token. Suggested name: `KaniKey Cardputer`.
3. Check the permissions:
   - Assignments: read.
   - Subjects: read.
   - Study materials: read.
   - User: read.
   - Summary: read.
   - Reviews: create.
   - (Phase 2: review statistics and reviews, read.)
4. Copy the token. Don't paste it into a chat or commit it.

It can be revoked at any time. KaniKey stores it in NVS and shows it
masked.

---

## 3. Generating the visual pack

The pack contains the glyphs for the characters that appear in the subjects
your account has access to, plus the rasterized radicals. It is generated
locally with your token; it is not uploaded to any server.

```sh
cd tools/glyph-pack
uv run kanikey-glyphs build \
  --token "$WANIKANI_TOKEN" \
  --out /Volumes/KANIKEY/kanikey/glyphs.kgp
```

What it does:

1. `GET /user` to read `level` and `subscription.max_level_granted`.
2. `GET /subjects` by allowed levels (up to 60 if the subscription allows it).
3. Collects the unique characters from kanji, vocabulary and kana.
4. Renders each character with Noto Sans JP at 64, 40 and 20 px, 4bpp + RLE.
5. Downloads the SVG images for the radicals and rasterizes them at 96 px.
6. Writes the index and the pack.

Planned options:

```sh
--max-level N       # limit the pack to one level (default: the allowed one)
--font PATH         # another OFL font
--no-radicals       # glyphs only (for testing)
--check             # compare a pack against current subjects and list missing ones
```

Approximate size: 7–9 MB. On an 8 GB card or larger it's not a problem.

### 3.1 Copying to the card

```
/kanikey/glyphs.kgp
```

The card must be FAT32. If the system doesn't mount it, format it as FAT32
(not exFAT).

---

## 4. First boot

1. Insert the card and turn on the Cardputer.
2. If Wi-Fi or the token is missing, KaniKey brings up a temporary **WPA2**
   network `KaniKey-XXXX`. The screen shows its password and
   `http://192.168.4.1/`.
3. On your phone, connect to that network and open the URL. The page shows the
   visual-pack status and can re-check the card; the browser also sends its
   clock and time zone.
4. Save the SSID and password for your home Wi-Fi. The setup AP closes, the
   Cardputer joins your network and the screen switches to
   `setup · Wi-Fi` with the station address (for example
   `http://192.168.1.23/`).
5. Move the phone to the same home network and open that address. The station
   page asks for the WaniKani token:
   - it is validated with `GET /user` before being stored;
   - a 401/403/TLS error is shown and nothing is saved;
   - the token is never logged or shown back.
6. **Start KaniKey** closes the portal and runs the first sync.
7. Check the visual pack. If it is missing or incomplete:
   `Visual pack missing · Copy glyphs.kgp to /kanikey/`.
8. Go back to the home screen. You should see your reviews and lessons.

The portal only reads `/user` and does not call
`PUT /assignments/{id}/start`, so configuring the device does not move lessons
in WaniKani. The normal first sync after **Start KaniKey** may flush reviews that
were already waiting in the outbox.

If setup is interrupted, press `P` over USB serial (or restart with Wi-Fi/token
missing) to open the portal again.

---

## 5. Daily use

- Turn it on; if it has been more than 15 minutes since the last sync, it syncs.
- `Enter` → reviews.
- Type the meaning; `Enter`.
- Type the reading in romaji; the field shows kana; `Enter`.
- If you get it wrong, read the correct answer and press `Enter`; the item comes back later.
- When you finish, a summary. `Enter` to return to the home screen.
- `L` → lessons (phase 2).
- `Esc` → Settings; `Sync now` starts a manual sync. In a review, `Esc` opens
  Pause (Continue / End session / Settings).
- Settings stores brightness and dim/blank/sleep timings in NVS; `Setup portal`
  reopens the Wi-Fi/token/time page.

---

## 6. Updating the visual pack

You must regenerate it if:

- You level up and new kanji appear (if the pack was generated only up to the
  current level).
- You go from a free account to a subscription (more levels allowed).
- You change the font or the glyph size.

With the pack generated up to `max_level_granted`, level changes should not
require regenerating it. Even so, `--check` lists the missing characters:

```sh
uv run kanikey-glyphs check \
  --pack /Volumes/KANIKEY/kanikey/glyphs.kgp \
  --token "$WANIKANI_TOKEN"
```

If only a few are missing, you can regenerate just the partial pack with `--ids` (planned).

---

## 7. Common problems

| Symptom | Probable cause | Solution |
|---|---|---|
| `No SD card` | Card missing or not FAT32 | Insert/format as FAT32 |
| `Visual pack missing` | `/kanikey/glyphs.kgp` is not there | Copy the pack |
| `Visual pack is missing 休` | Old pack or generated with another level | Regenerate with `--check` |
| `Token rejected` (401) | Token revoked or copied wrong | Create a new one and update it |
| `Missing API permissions` (403) | Missing permissions | Edit the token in WaniKani |
| `Offline` | No Wi-Fi or DNS | Check the network; offline review continues |
| `WaniKani is busy` (429) | Rate limit | Wait; KaniKey retries |
| `Clock not set` | No SNTP and no portal time | Connect once or set the time |
| `Review already changed` (422) | The item was reviewed elsewhere | KaniKey reconciles and refreshes |

---

## 8. Privacy

- The token stays on the Cardputer (NVS). It is not sent to any KaniKey
  server.
- The visual pack is generated on your computer and stays on your card.
- There is no telemetry.
- Logging out deletes the token, assignments and outbox (with confirmation).

---

## 9. Future

- Pack generator in the phone's browser, without a computer.
- On-device rasterization for missing characters.
- Pronunciation audio and stroke order.

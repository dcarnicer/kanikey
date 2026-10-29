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
  assignments, subjects, study materials, review statistics, level progressions,
  user and summary, and create permission for reviews.
- A computer with Python 3.11+ and [`uv`](https://docs.astral.sh/uv/) to
  generate the visual pack the first time.
- **Noto Sans JP** font (OFL); the generator looks in Tangocho's local cache by
  default and accepts `--font PATH`.

The card is required for the glyphs. Without a visual pack you can configure
Wi-Fi and the token through the setup portal, but sync does not run and you
cannot review kanji, vocabulary or radical prompts.

---

## 2. Creating the WaniKani token

1. Go to WaniKani → **Settings → API Tokens**.
2. Create a new token. Suggested name: `KaniKey Cardputer`.
3. Check the permissions:
   - Assignments: read.
   - Subjects: read.
   - Study materials: read.
   - Review statistics: read.
   - Level progressions: read.
   - User: read.
   - Summary: read.
   - Reviews: create.
   - (Phase 2: reviews, read for history.)
4. Copy the token. Don't paste it into a chat or commit it.

It can be revoked at any time. KaniKey stores it in NVS and shows it
masked.

---

## 3. Generating the visual pack

The pack contains the glyphs for the characters that appear in the subjects
your account has access to, plus the rasterized radicals. It is generated
locally with your token; it is not uploaded to any server.

```sh
uv run --with pillow --with cairosvg python3 tools/glyph-pack/build.py \
  --out /Volumes/KANIKEY/kanikey/glyphs.kgp
```

`build.py` reads the token from `WANIKANI_TOKEN`, so export it first (or pass
`--token`). What it does:

1. `GET /user` to read `level` and `subscription.max_level_granted`.
2. `GET /subjects` by allowed levels (up to 60 if the subscription allows it).
3. Collects the unique characters from kanji, vocabulary and kana.
4. Renders each character with Noto Sans JP at 64, 32 and 20 px, 4bpp + RLE.
5. Downloads the SVG images for the radicals and rasterizes them at 96 px.
6. Writes the index and the pack.

Options:

```sh
--chars "食休日本語"  # test pack, no API
--tangocho           # full kanji/kana pack from Tangocho's local cache, no token
--font PATH          # another OFL font
--no-kana            # omit the kana block
--out PATH           # output file
```

Approximate size: 3.7 MB for the token pack on the card (7,269 glyphs + 498
radical bitmaps); the tokenless pack is 2.9 MB. On an 8 GB card or larger it is
not a problem.

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
6. Check the visual pack on the portal page. If it is missing or incomplete:
   `Pack missing · Copy glyphs.kgp to /kanikey/`, then check again. Sync will
   not run without the pack.
7. **Start KaniKey** closes the portal and runs the first sync (`/user`,
   `/summary`, level and SRS counts; the review batch is fetched later).
8. Go back to the home screen. You should see the review/lesson counters; Enter
   on Home fetches the review batch.

The portal only reads `/user` and does not call
`PUT /assignments/{id}/start`, so configuring the device does not move lessons
in WaniKani. The normal first sync after **Start KaniKey** may flush reviews that
were already waiting in the outbox.

If setup is interrupted, press `P` over USB serial (or restart with Wi-Fi/token
missing) to open the portal again.

---

## 5. Daily use

- Turn it on; a configured device syncs on boot (`S` on Home or Settings ›
  `Sync now` runs the same sync manually).
- `Enter` → reviews.
- Type the meaning; `Enter`.
- Type the reading in romaji; the field shows kana; `Enter`.
- If you get it wrong, read the correct answer and press `Enter`; the item comes back later.
- When you finish, a summary. `Enter` to return to the home screen.
- `L` → lessons. `O` opens reviews that were rejected or gave up.
- `Esc` → Settings; `Sync now` starts a manual sync. In a review, `Esc` opens
  Pause (Continue / End session / Settings).
- Settings stores brightness and dim/blank/sleep timings in NVS; `Setup portal`
  reopens the Wi-Fi/token/time page.
- `Settings › Firmware` checks the latest GitHub release and offers an update.
  The install streams into the spare slot and restarts; settings, the outbox
  and the cache survive. If the new image does not start, the previous one
  comes back. See [ota.md](ota.md).

---

## 6. Updating the visual pack

You must regenerate it if:

- You level up and new kanji appear (if the pack was generated only up to the
  current level).
- You go from a free account to a subscription (more levels allowed).
- You change the font or the glyph size.

With the pack generated up to `max_level_granted`, level changes should not
require regenerating it. If a `no glyph` state appears, regenerate the pack;
there is no separate `check`/`--ids` command yet.

```sh
uv run --with pillow --with cairosvg python3 tools/glyph-pack/build.py \
  --out /Volumes/KANIKEY/kanikey/glyphs.kgp
```

---

## 7. Common problems

| Symptom | Probable cause | Solution |
|---|---|---|
| `No SD card` | Card missing or not FAT32 | Insert/format as FAT32 |
| `Pack missing` | `/kanikey/glyphs.kgp` is not there | Copy the pack |
| `no glyph` on an item | Old pack or generated with another level | Regenerate the pack and copy it again |
| `Auth: token rejected` / `Token rejected` (401) | Token revoked or copied wrong | Create a new one and update it; KaniKey blocks API calls until the token changes |
| `Auth: missing permissions` / `Missing permissions` (403) | Missing permissions | Edit the token in WaniKani; other endpoints keep working |
| `Offline - ...` | No Wi-Fi or DNS | Check the network; offline review continues from the cache |
| `Rate limited; retry in Ns` (429) | Rate limit | Wait for the countdown; the shared gate stops all requests until the reset |
| `Clock not set` | No SNTP and no portal time | Connect once or set the time |
| `HTTP 422` in Outbox | The item was reviewed elsewhere or is invalid | KaniKey marks it rejected; open `O: outbox` to inspect and dismiss it |
| `HTTP 409` in Outbox | Conflict; the assignment changed | Inspect it in `O: outbox`; dismissing only removes the local review |
| `gave up` in Outbox | 50 send attempts failed | Inspect the attempts/age in `O: outbox` and dismiss it if it is no longer useful |

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

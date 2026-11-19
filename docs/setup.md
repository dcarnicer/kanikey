# KaniKey — setup

KaniKey is a pocket review terminal for WaniKani on the M5Stack Cardputer ADV.
This guide goes from the box to a finished review session and is written for
someone who has not built the firmware.

> **The visual pack is the one step that still needs a computer.** It is built
> with a script and copied to the microSD card today. A phone-only pack flow is
> planned but is **not** in this firmware yet; see §12.

---

## Quick start

1. **Prepare the card (once).** Use a **FAT32** microSD card and copy the visual
   pack to `/kanikey/glyphs.kgp`. See §3 for the build command. If the card
   already has that file, skip to step 2.
2. **Charge and start.** Connect USB-C, insert the card and turn the device on.
   `No SD card` means the card is missing or not FAT32. `Pack missing` means
   `/kanikey/glyphs.kgp` is not on the card. Fix it and press `Enter`.
3. **Open setup.** With no Wi-Fi or token saved, the device starts the
   `KaniKey-XXXX` setup network automatically; if the screen shows `Token
   needed` or `Wi-Fi needed` instead, press `Enter`. On your phone, join
   `KaniKey-XXXX` with the password on the screen and open
   `http://192.168.4.1/`. Save your home Wi-Fi (2.4 GHz).
4. **Finish on your home network.** Reconnect the phone to the same Wi-Fi, open
   the address shown on the device, enter the 6-digit `Page code`, paste your
   WaniKani token and press **Validate and save**. The time and time zone are
   set automatically from the browser. Check the pack, then press
   **Start KaniKey**.
5. **Wait for the first sync.** When it succeeds, the device opens Home.
   `Enter` starts reviews, `L` opens lessons and `Esc` opens Settings.

---

## 1. What you need

- M5Stack Cardputer ADV, charged over USB-C. You can set it up while charging.
- A **FAT32** microSD card. exFAT is not supported. The pack is about 3.7 MB, so
  almost any card is large enough.
- A **2.4 GHz** Wi-Fi network and its password. The ESP32-S3 inside the
  Cardputer does not join 5 GHz-only networks.
- A WaniKani account.
- A phone or computer with a browser for the two setup pages. A phone is
  enough; no app is installed.
- For the visual pack: a computer with Python 3.11+, `uv` (or `pip`), the
  Noto Sans JP font file and a card reader or SD slot. One-time; see §3.

You do not need a KaniKey account, a KaniKey password or a KaniKey app. The
device talks to WaniKani, to its time servers and to GitHub for firmware
updates; there is no KaniKey server.

Out of the box there is nothing to assemble: charge the device over USB-C,
insert the prepared card and turn it on. The battery is internal and charges
over USB-C; you can use the device while it charges.

---

## 2. Your WaniKani token

KaniKey uses a personal WaniKani API token, not your WaniKani password.

1. Sign in to WaniKani in a browser.
2. Open **Settings → API Tokens** and create a token. A name such as
   `KaniKey Cardputer` makes it easy to recognise later.
3. Select these permissions:
   - **Assignments**: read
   - **Assignments**: start
   - **Subjects**: read
   - **Study materials**: read
   - **Review statistics**: read
   - **Level progressions**: read
   - **User**: read
   - **Summary**: read
   - **Reviews**: create
4. Copy the token and keep it private. Do not paste it into a chat or commit it.

The read permissions let KaniKey show your account and content.
**Assignments: start** is what starts a lesson, and **Reviews: create** is what
submits a completed review. The study-materials, review-statistics and
level-progressions permissions are optional for basic reviews: without them,
notes, critical items and some level and statistics figures are missing, but
the device still reviews. Without **Assignments: start**, lesson cards cannot be
started; without **Reviews: create**, completed reviews cannot be sent.

The token is pasted into a password field, so it is masked in the form. It is
validated with `GET /user` before it is saved, stored in the device's NVS, and
shown only as `configured` (About) or `A token is already stored` (setup page).
It is never shown back, never logged, and only sent from the device to
`api.wanikani.com`.

You can revoke the token in WaniKani at any time. Create a new one and paste it
through **Settings › Setup portal**. There is no KaniKey password to recover.

---

## 3. The visual pack

The firmware has no Japanese font built in. Kanji, kana and radical prompts are
drawn from one file on the card:

```
/kanikey/glyphs.kgp
```

The current firmware uses one combined pack (format `KKGP` v1): glyphs at 64,
32 and 20 px plus radical bitmaps. A full pack built with the account token has
about **7,269 glyphs and 498 radical bitmaps**, roughly **3.7 MB**. A pack
built without a token has glyphs but no radical bitmaps, so image-only radicals
show `no glyph`. Treat the combined file as personal: it mixes font glyphs with
radical images derived from WaniKani, so do not publish or share it.

A future firmware splits this into a font pack (`glyphs.kgp`) and a personal
radical pack (`radicals.kgp`). That split is planned, not current; see §12.

### 3.1 Build the pack (once, on a computer)

From a checkout (or the source ZIP) of this repository:

```sh
WANIKANI_TOKEN=... uv run --with pillow --with cairosvg \
  python3 tools/glyph-pack/build.py --out dist/glyphs.kgp
```

Install `uv` once from <https://docs.astral.sh/uv/> if needed. With plain
Python instead:

```sh
python3 -m pip install pillow cairosvg
WANIKANI_TOKEN=... python3 tools/glyph-pack/build.py --out dist/glyphs.kgp
```

The generator reads `WANIKANI_TOKEN`; `--token` also works. It calls
`GET /user` for the highest level your subscription allows, collects the
characters from `GET /subjects`, renders them with Noto Sans JP and rasterizes
the radical SVG images. The pack stays on your computer and card; nothing is
uploaded to a KaniKey server.

By default the generator looks for Noto Sans JP at
`~/code/tangocho/tools/pack-builder/cache/noto/NotoSansJP-Regular.otf`. If that
file is not there, download Noto Sans JP (OFL) from
<https://fonts.google.com/noto/specimen/Noto+Sans+JP> and add
`--font /path/to/NotoSansJP-Regular.otf`.

### 3.2 Copy it to the card

With the card in a card reader, copy the file to the lowercase `kanikey` folder
in the card's root. The firmware looks for exactly `/kanikey/glyphs.kgp`; do not
rename the file.

macOS, if the card is mounted as `KANIKEY`:

```sh
mkdir -p /Volumes/KANIKEY/kanikey
cp dist/glyphs.kgp /Volumes/KANIKEY/kanikey/glyphs.kgp
```

Replace `KANIKEY` with the name the card actually mounts under. On Windows,
create `kanikey` in the card's root and copy the file as
`D:\kanikey\glyphs.kgp` (use the card's drive letter). On Linux, the path is
usually `/media/<user>/<card>/kanikey/glyphs.kgp`. Eject the card safely before
removing it.

### 3.3 Check the pack on the device

The setup page and **About** show the pack status, for example:

```
7269 glyphs, 498 radicals, 64/32/20 px
```

`pack missing` (or `no pack` when no card is mounted) means the file is missing
or could not be opened. When the card is mounted but the pack cannot be opened,
the boot screen says `Pack missing`. In the setup page press
**Check pack again**. The full CRC of the 3.7 MB file takes a few seconds, so
the new result can appear on the next page load. On boot, `Pack missing` with
`Copy glyphs.kgp to /kanikey/ on the SD card` and `Enter: check again` is the
same problem. A damaged or truncated file reports a `pack: ...` error (for
example `pack: bad crc`); copy it again.

You can configure Wi-Fi and the token before the pack is ready, but reviews and
lessons need it for Japanese text.

### 3.4 When to rebuild it

- A prompt shows `no glyph`: the character or radical is not in the pack.
- You changed subscription and the pack was built for fewer levels than your
  account now allows.
- You changed the font with `--font`.

Normal level-ups do not require a rebuild when the pack was built with your
current `max_level_granted`: the generator included every level your
subscription allows.

### 3.5 Pack updates over Wi-Fi (optional)

Instead of removing the card, you can upload a new pack to a device that already
has a working one. The device must be on Home, connected to the computer by USB
and on the same network. Close the setup portal first; `push` waits for Home and
otherwise restarts the device.

```sh
uv run --with pyserial python3 tools/device.py push \
  dist/glyphs.kgp /kanikey/glyphs.kgp
```

`push` uses `/dev/cu.usbmodem1101` by default; add
`--port /dev/cu.usbmodemXXXX` if your device appears on another port. For the
first install, use the card reader: a device without a pack does not reach Home,
and `push` waits for it. An interrupted upload writes a temporary file and
leaves the previous pack in place. A completed pack is CRC-checked before it
replaces the old one.

---

## 4. First boot

### 4.1 The setup network

1. Insert the card and turn the device on.
2. With the card and pack ready, a missing Wi-Fi or token starts the setup
   network automatically. If the screen shows `Token needed` (`Press Enter for
   setup`) or `Wi-Fi needed` (`Join KaniKey-XXXX on your phone`), the portal has
   stopped or was cancelled: press `Enter` to open it again.
3. The screen switches to `KaniKey` / `setup` and shows:
   - `Join this Wi-Fi`
   - the network name `KaniKey-XXXX`
   - a random 10-character WPA2 password (no `0/O/1/I/l`)
   - `Open http://192.168.4.1/`
4. On your phone, join `KaniKey-XXXX` and enter the password. The network is
   local-only and has no internet; if the phone warns about that, choose to stay
   connected.
5. Open `http://192.168.4.1/`. This first page needs no unlock code: the WPA2
   key already protects it.

The page shows the **Visual pack** status, the **Time** line and a **Home Wi-Fi**
form. Enter your network name and password and press **Save and connect**. The
name field also suggests nearby networks.

If the device cannot join, it brings the setup network back with
`Could not join "<ssid>". Check the password and try again.` Check that the
network is 2.4 GHz and that the password is correct, then try again.

### 4.2 The station page and the 6-digit code

After a successful join, the setup network closes and the device joins your
home Wi-Fi. The screen now reads `setup · Wi-Fi` and shows:

- `Connected`
- your network name
- `Open on this network` and an address such as `http://192.168.1.23/`
- `Page code 123456`
- the token state and `Pack: ...`
- `Enter code on page  Esc: cancel`

Reconnect your phone to the same home Wi-Fi and open that address. The station
page first shows **Unlock** and asks for the 6-digit code on the device screen.

- The code is generated when the station page starts; a new portal start shows
  a new code.
- A correct code sets a session cookie, so the browser stays unlocked while the
  page is open.
- After five wrong codes the device shows a new code and the page says
  `Too many tries. New code on screen.`
- A code or cookie from an earlier portal start is rejected.

The station page then has these sections:

- **Visual pack** — the current status and **Check pack again**.
- **Time** — the clock and time zone are copied from the browser
  automatically; there is nothing to set. The page shows the zone name and the
  resulting clock.
- **WaniKani token** — paste the token and press **Validate and save**. On
  success it shows `Valid token for <username> (level N).` A 401/403 or a
  network/TLS error is shown and nothing is saved. If a token is already
  stored, the page offers to validate or replace it.
- **Change Wi-Fi** — an expandable form for a new network; see §8.
- **Finish** — **Start KaniKey**.

**Start KaniKey** validates the token once more if needed and closes the portal.
The device shows `Setup complete. Starting KaniKey...` and runs the first sync:
Wi-Fi, Outbox, Account, Summary, Level, SRS and Critical rows, then Home. The
first sync can take several seconds.

The pack is not part of the Finish check. If the **Visual pack** section says
`pack missing` or `no pack`, fix the card and press **Check pack again** before
starting.

### 4.3 If setup stops

The portal closes after **15 minutes without a page request**. The setup network
disappears and the device returns to its boot screen. Nothing is corrupted.
`Esc: cancel` on the device closes it immediately as well.

Press `Enter` on `Token needed` or `Wi-Fi needed` to open setup again; if both
Wi-Fi and the token are already saved and the device boots to Home, use
**Settings › Setup portal** instead. A new setup network gets a new password,
and every portal start gets a new `Page code`. Wi-Fi credentials are saved when
you press **Save and connect**, even if the join then fails; the token is saved
only after it validates. If setup stopped after Wi-Fi but before the token,
reopening goes straight to the station page when the saved network is
reachable, or back to the `KaniKey-XXXX` page when it is not.

Setup only reads `GET /user`; it does not start lessons or submit reviews. The
first sync after **Start KaniKey** may send reviews that were already waiting in
the outbox.

---

## 5. Daily use

- Turn the device on. A configured device syncs on boot. `S` on Home or
  **Settings › Sync now** runs a sync manually.
- Home shows the review and lesson counts. `Enter` starts reviews, `L` opens
  lessons, `T` opens stats, `P` opens extra study and `Esc` opens Settings.
- Type the meaning; press `Enter`. Type the reading in romaji; the field shows
  kana; press `Enter`.
- A wrong answer shows the correct answer; press `Enter` to continue. The item
  comes back later in the session.
- `L` opens the lesson list; `Enter` opens the selected card. On the card,
  `Enter` starts that lesson in WaniKani. `Q` on the card opens the optional
  local quiz, which never starts the lesson.
- `Esc` in a review opens Pause: Continue, End session or Settings. At the end
  of a session the summary appears; `Enter` returns to Home.
- On battery the screen dims after 30 s, blanks after 60 s and deep-sleeps after
  2 minutes when no review or lesson is active. A key wakes it; the first key on
  a blank screen only wakes the panel. The timings are under
  **Settings › Dim after / Blank after / Sleep after**.
- Below 20% battery, starting reviews or lessons asks for confirmation. USB
  power skips that prompt.
- `O` on Home opens the Outbox when a review needs attention. See §6.

---

## 6. Offline and the outbox

After one successful sync, the device can work without Wi-Fi:

- Home restores the cached counts and shows `Offline - ...` (or
  `Stale cache - ...` when the cache is more than 24 hours old).
- The last fetched review batch still runs. A new batch, new lessons and fresh
  counts need Wi-Fi.
- Extra study uses cached data and works offline. The lesson quiz makes no
  network call once a lesson card is open.

A completed review is written to the outbox on the device **before** it is sent.
If the request fails, the review stays queued:

- `Outbox: N pending` means reviews are waiting and will be sent on the next
  sync or after a reboot with Wi-Fi.
- `Outbox: N need attention` means a review was rejected (for example
  `HTTP 422` or `HTTP 409`) or gave up after 50 attempts. `O` lists it.
- The Outbox screen shows the reason, attempts and age. Discarding an item
  removes only the local copy; it does **not** change WaniKani.
- If all 128 outbox slots are used, starting a session shows `Outbox full`.
  Sync to flush the pending items, or dismiss rejected/stuck ones.

Settings, the cache and the outbox survive a reboot and a firmware update.

---

## 7. Updating firmware

### 7.1 Over the air (normal)

1. Connect the device to Wi-Fi.
2. Press `Esc` from Home and open **Firmware**.
3. The screen shows the running version and slot and the last check or install
   result. **Check now** checks the latest GitHub release. The device also checks
   once a day after a successful sync.
4. When a newer version is offered, choose **Install update** and confirm. The
   image streams into the spare slot and the device restarts. Settings, the
   cache and the outbox survive.

Keep the device on power and connected during the install. The first boot of a
new image is on probation: if it crashes, resets or loses power before it is
marked valid, the bootloader returns to the previous image. It is marked valid
after the first successful WaniKani request, or after about 10 minutes of
running. Leave it running for a few minutes after the restart. A failed check or
install stays on the screen with its reason and keeps the current image.

### 7.2 One-time USB flash for v0.1.0

A device still running v0.1.0 cannot follow GitHub's redirects, so its update
check fails. Flash it once over USB; later releases update over the air.

Download `kanikey-cardputer-adv.bin` from
<https://github.com/dcarnicer/kanikey/releases/latest>, then either build and
flash from a checkout:

```sh
cd firmware
pio run -e cardputer-adv -t upload --upload-port /dev/cu.usbmodemXXXX
```

or write the released app-only image to the app slot at `0x10000`:

```sh
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX \
  write_flash 0x10000 kanikey-cardputer-adv.bin
```

`esptool.py` comes from `python3 -m pip install esptool`. This USB path is for a
v0.1.0 device, whose active slot is `app0`. Do not use it to move a device that
has already updated itself.

---

## 8. Changing Wi-Fi

1. Press `Esc` from Home and open **Setup portal**. It is disabled during a
   review; end the session first.
2. If the device can join the saved network, the station page opens at its
   address. Enter the `Page code` shown on the device, then open the
   **Change Wi-Fi** section, enter the new network name and password and press
   **Save and reconnect**.
3. Move the phone to the new network, open the address shown on the device and
   enter the new `Page code`.
4. If the device cannot join the old network (the router was replaced or its
   password changed), it falls back to the `KaniKey-XXXX` setup network after
   about 30 seconds. Join it, open `http://192.168.4.1/` and save the new Wi-Fi
   there. Then finish on the station page as in §4.2.

The token, cache and outbox are not affected by a Wi-Fi change.

---

## 9. Problems

| Symptom | What it means | What to do |
|---|---|---|
| `No SD card` / `Insert a FAT32 card with /kanikey/glyphs.kgp` | Card missing, not mounted or not FAT32 | Insert it, format it as FAT32, then press `Enter: retry` |
| `Pack missing` / setup page `pack missing` or `no pack` | `/kanikey/glyphs.kgp` is missing or could not be opened | Copy the pack (§3.2), then press `Enter: check again` or **Check pack again** |
| `pack: bad crc` or another `pack: ...` error | The pack file is damaged, truncated or from another format | Copy the correct pack again; rebuild it if the copy keeps failing |
| `no glyph` on an item | The character or radical is not in the pack | Rebuild the pack with the current token and copy it (§3.4) |
| `Token rejected` / `Auth: token rejected` | HTTP 401: the token was revoked, deleted or mistyped | Create a new token, then **Settings › Setup portal** and paste it |
| `Missing permissions` / `Auth: missing permissions` | HTTP 403: the token lacks a permission | Create a new token with the missing permission and paste it in **Setup portal**; only the affected features stop |
| `Rate limited; retry in Ns` / `Retry in Ns` | HTTP 429 | Wait for the countdown. The shared gate pauses every request until then; do not power-cycle the device repeatedly |
| `Offline - ...` / `Stale cache - ...` | No Wi-Fi or DNS, or an old cache | Cached reviews still work. Check the 2.4 GHz network and press `S` to sync |
| `Sync failed` / `Check Wi-Fi and token` | The first sync could not finish | Check the 2.4 GHz network and the token, then press `Enter: retry`; use **Setup portal** if the token is wrong |
| `Could not join "<ssid>"` | Wrong password or a 5 GHz-only network | Re-enter the password on the setup page; use a 2.4 GHz network |
| Setup network disappeared, or the page no longer loads | The portal closed after 15 idle minutes, or the phone is on mobile data | Press `Enter` on the boot screen's setup prompt, or open **Settings › Setup portal** once Home. Reconnect the phone to the device network or home Wi-Fi |
| `Unlock` asks for a code; `Wrong code` / `Too many tries` | The station page is locked | Type the 6-digit `Page code` from the screen. After five wrong codes, use the new code on screen |
| The clock or time zone is wrong | The browser's clock or zone was wrong, or no sync has run | Reopen **Setup portal** from a phone with the correct time and zone; a sync also sets the clock from the network |
| `Outbox: N pending` | Reviews are queued and not sent yet | They will send on the next sync; press `S` when Wi-Fi is back |
| `Outbox: N need attention` / `HTTP 422` / `HTTP 409` / `gave up` | A review was rejected or could not be sent | Open `O`, read the reason, then discard it if it is no longer useful. Discarding does not change WaniKani |
| `Outbox full` | All 128 outbox slots are used | Sync to flush, or dismiss rejected/stuck items in `O` |
| `Storage unreadable` | The internal storage for the cache/outbox cannot be read | `Enter` formats it, which loses pending reviews and the cache; `Esc` continues without storage (no outbox or cache) |
| Firmware check fails | No Wi-Fi/TLS, or no release is available | Check the network and try **Check now** again; the current image is unchanged |
| The screen is black | The device blanked or deep-slept to save power | Press a key. The first press only wakes the screen |
| Low-battery prompt | The battery is below 20% | Charge it, or connect USB power |

---

## 10. Privacy

- The WaniKani token stays in NVS on the Cardputer. It is not sent to any
  KaniKey server, is never logged and is never shown back.
- There is no KaniKey account, cloud sync or telemetry. The device contacts
  `api.wanikani.com`, its NTP servers and GitHub (firmware updates).
- The visual pack, cache and outbox stay on your card and device. WaniKani
  content is fetched with your token and is not shipped in the repository.
- The setup access point is local and WPA2-protected. The station page is
  locked with the 6-digit code because it is reachable from the whole home
  network. The setup pages themselves use HTTP on the local network, so set up
  on a network you trust.
- **Sign out** removes the token and all cached account data: cache, journal,
  outbox and practice data. It keeps Wi-Fi and the visual pack on the card.

---

## 11. What KaniKey does not do

- It does not replace WaniKani. The SRS schedule lives in WaniKani; the device
  keeps only a local copy of what it has shown and submitted.
- It does not implement a local SRS for reviews. Extra study and the lesson quiz
  are local practice; they never send reviews or start lessons.
- It does not have its own login, password or cloud service. Access is the
  WaniKani API token, which you can revoke and replace in WaniKani.
- It does not ship WaniKani content. Meanings, readings, mnemonics and radical
  images are requested with your token and stored on your card. Audio is not
  played yet (see §12).
- It does not support phones, watches or other boards. This firmware is for the
  Cardputer ADV only.
- It does not provide a forum, community, push notifications, an app store
  listing or a mobile app.
- It is an unofficial third-party app. It is not affiliated with, endorsed by or
  sponsored by WaniKani or Tofugu LLC.

---

## 12. Planned, not in this firmware

These are future plans, not features you can use today:

- **Phone-only pack setup.** The station page would offer **Download pack** when
  the font pack is missing, and an upload form for the personal radical pack.
  The current firmware neither downloads nor uploads packs on the setup page;
  use the card reader or `device.py push` in §3.
- **Split packs.** A publishable font pack (`glyphs.kgp`) and a personal radical
  pack (`radicals.kgp`). The current firmware uses one combined file.
- A pack generator in the phone's browser, without a computer.
- On-device rendering of characters that are not in the pack.
- Vocabulary pronunciation playback and stroke order. The firmware can parse
  pronunciation metadata, but playback is not enabled yet.

---

## Appendix A — Custom pack options

The standard command builds every character your account can use, with Noto
Sans JP. Extra options:

```sh
# Test pack from a character list: no token, no radical bitmaps
uv run --with pillow python3 tools/glyph-pack/build.py \
  --chars "食休日本語" --out dist/glyphs-test.kgp

# Another OFL font (the account token is still needed for the full pack)
WANIKANI_TOKEN=... uv run --with pillow --with cairosvg \
  python3 tools/glyph-pack/build.py \
  --font /path/to/font.otf --out dist/glyphs.kgp
```

| Option | Effect |
|---|---|
| `--font PATH` | Use another OFL font file |
| `--chars "食休日本語"` | Build from this character list; no API, no radicals |
| `--no-kana` | Omit the kana block in a `--chars` or `--tangocho` build |
| `--out PATH` | Write the pack somewhere other than `dist/glyphs.kgp` |
| `--tangocho` | Build from Tangocho's local kanji cache; no token, no radicals |

A pack built without a token has no radical bitmaps; image-only radical prompts
then show `no glyph`.

# KaniKey — setup

KaniKey is a pocket review terminal for WaniKani on the M5Stack Cardputer ADV.
This guide goes from the box to a finished review session and is written for
someone who has not built the firmware.

> **The normal setup path needs no computer.** When the card has no
> `/kanikey/glyphs.kgp`, the station page can fetch the public font pack from
> the release manifest and check its size and SHA-256. The personal radical pack
> can be uploaded from your phone on the same page. A computer is only needed
> for a custom font pack or to build a radical pack from your account; see §3.
> If the current release does not publish a font pack, the page says so and you
> can build it in §3.2 and copy it once instead (§3.3).

---

## Quick start

1. **Prepare the card (once).** Use a **FAT32** microSD card. It can be empty:
   the station page can download the public font pack (when the release
   includes it) and the setup page accepts the personal radical pack. If you
   build packs on a computer, copy them to `/kanikey/glyphs.kgp` (font) and
   `/kanikey/radicals.kgp` (radical); see §3. A v1 combined `glyphs.kgp` with
   radical entries from an older build still works.
2. **Charge and start.** Connect USB-C, insert the card and turn the device on.
   `No SD card` means the card is missing, not mounted or not FAT32. `Pack
   missing` means `/kanikey/glyphs.kgp` did not open; press `Enter` to open
   setup and download or check it (§3.1). A missing `radicals.kgp` only affects
   image-only radical prompts, not text.
3. **Set up Wi-Fi.** With no Wi-Fi and no token saved, the device starts the
   `KaniKey-XXXX` setup network automatically. Join it with the password on the
   screen, open `http://192.168.4.1/` and save your home Wi-Fi (2.4 GHz). If the
   screen shows `Wi-Fi needed` instead, press `Enter` to pick the network on the
   device, or `P` for the phone portal.
4. **Finish on your home network.** Reconnect the phone to the same Wi-Fi, open
   the address shown on the device, enter the 6-digit `Page code`, paste your
   WaniKani token and press **Validate and save**. The time and time zone are
   set automatically from the browser. If the font pack is missing, press
   **Download pack** (station page only) or build and copy the file (§3.2,
   §3.3); then press **Start KaniKey**.
5. **Wait for the first sync.** When it succeeds, the device opens Home.
   `Enter` starts reviews, `L` opens lessons and `Esc` opens Settings.

---

## 1. What you need

- M5Stack Cardputer ADV, charged over USB-C. You can set it up while charging.
- A **FAT32** microSD card. exFAT is not supported. The public font pack is
  about 9 MB and the radical pack is smaller, so almost any card is large
  enough.
- A **2.4 GHz** Wi-Fi network and its password. The ESP32-S3 inside the
  Cardputer does not join 5 GHz-only networks.
- A WaniKani account.
- A phone or computer with a browser for the setup pages: Wi-Fi, the token,
  the browser time zone and the radical pack upload. A phone is enough; no app
  is installed. Once the device is set up, Wi-Fi can also be changed on the
  device.
- For a custom pack only: a computer with Python 3.11+ and Pillow, plus a font
  file (Noto Sans JP is the default). Building the personal radical pack also
  needs `cairosvg` and a WaniKani token. A custom font pack needs a card reader
  (or the pack replacement in §3.6 on a device that already has a working
  font); the radical pack can be uploaded from the setup page. See §3.2.

The public font pack can be downloaded on the device (when the release includes
it) and the personal radical pack can be uploaded from a phone, so a computer is
optional for the main path.

You do not need a KaniKey account, a KaniKey password or a KaniKey app. The
device talks to WaniKani, to its time servers and to GitHub for firmware
updates and the public font pack; there is no KaniKey server.

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

Setup validates the token with `GET /user` only. The first sync exercises the
other permissions; a token that passes setup but lacks one of them shows
`Missing permissions` then.

The token is pasted into a password field, so it is masked in the form. It is
validated with `GET /user` before it is saved, stored in the device's NVS, and
shown only as `configured` (About) or `A token is already stored` (setup page).
It is never shown back, never logged, and only sent from the device to
`api.wanikani.com`.

You can revoke the token in WaniKani at any time. Create a new one and paste it
through **Settings › Setup portal**. There is no KaniKey password to recover.

---

## 3. The visual pack

The firmware has no Japanese font built in. Japanese text and radical prompts
are drawn from two files on the card:

```
/kanikey/glyphs.kgp    font: kana and kanji (the English UI uses the built-in font)
/kanikey/radicals.kgp  personal: the image-only radical bitmaps
```

**Public font pack.** A public `glyphs.kgp` contains glyphs rendered from an OFL
font (Noto Sans JP by default). It has no WaniKani content and can be published
with a release. **Personal radical pack.** `radicals.kgp` is generated from your
own WaniKani account's subject images. It is personal: do not publish or share
it.

Both use the `KKGP` v2 header: glyphs at 64, 32 and 20 px in the font pack, and
radical bitmaps in the personal pack; the body and CRC are the same as v1. A
v1 **combined** `glyphs.kgp` with radical entries from an older build still
opens for both roles, so an existing card keeps working. A font pack can be
built without a token; a radical pack needs one. A missing radical pack leaves
all text working and only changes image-only radical prompts to `no glyph`.

### 3.1 Get the packs from the setup page (no computer)

The setup pages can install both files on a new card when the release provides
the public font pack. **Download pack** is offered on the **station page**
(§4.2), after the device has joined your home Wi-Fi; the `KaniKey-XXXX` setup
network has no internet. **Upload radical pack** works on either setup page
because the file goes directly from your phone to the device.

- **Download pack** appears while `/kanikey/glyphs.kgp` is missing. The device
  reads the `pack font` record from the release manifest, streams the file to
  `/kanikey/glyphs.kgp.tmp` over HTTPS, and checks the exact size and SHA-256
  while it writes. The device shows `Downloading pack` / `Pack 34%`; `Esc`
  cancels. Only a file that passes both checks and opens as a font pack is
  kept. Offline, TLS, HTTP, size and hash failures delete the `.tmp` and leave
  the old file in place; the page shows the reason. A file that downloads but
  is not a font pack is deleted. If the release manifest has no `pack font`
  record, the page says `The release manifest has no font pack`; build a pack in
  §3.2 and copy it as in §3.3.
- **Upload radical pack** takes a `radicals.kgp` built by `build.py` (§3.2) and
  streams the multipart part to `/kanikey/radicals.kgp.tmp`. It accepts only a
  v2 **kind-2** radical pack up to 32 MB, checks the structure and kind, and
  renames it only on success. The CRC is **not** checked on this path. A wrong,
  truncated or v1 combined file is deleted and the page says why. `Esc`
  cancels; the old `radicals.kgp` stays.
- **Check pack again** reports both status lines and, if a pack is missing or
  did not open, re-opens both after the page responds. The full font-pack CRC
  runs then and can take several seconds, so the new result can appear on the
  next page load.

The browser only talks to the device. The device fetches the public font pack
from the release manifest; the radical pack never leaves your phone and the
device. No WaniKani token is involved in either transfer.

The setup-page transfers are new. Their hardware checks and any known limits
are listed in [docs/hardware.md](hardware.md).

### 3.2 Build the packs (optional, on a computer)

The setup page is the normal path for the public font pack and the radical
pack when the release includes the font pack. Build packs yourself only for a
custom font, an offline copy, or a radical pack for your account. From a
checkout (or the source ZIP) of this repository, the simplest path is a
font-only split build (no token; fixed kana + JIS X 0208 set):

```sh
uv run --with pillow python3 tools/glyph-pack/build.py --split dist
```

For the personal radical pack, add the token and `cairosvg`:

```sh
WANIKANI_TOKEN=... uv run --with pillow --with cairosvg \
  python3 tools/glyph-pack/build.py --split dist
```

Replace `...` with your WaniKani token.

The token command writes `dist/glyphs.kgp` and `dist/radicals.kgp`. Without a
token the font pack is still written and the tool prints
`no token: radicals.kgp skipped (font pack only)`. The legacy single-file form
is still available with `--out dist/glyphs.kgp`, but it writes one combined pack
and needs a token, `--chars` or `--tangocho`; it is not the two-file layout.

Install `uv` once from <https://docs.astral.sh/uv/> if needed. With plain
Python instead:

```sh
python3 -m pip install pillow cairosvg
WANIKANI_TOKEN=... python3 tools/glyph-pack/build.py --split dist
```

The generator reads `WANIKANI_TOKEN`; `--token` also works. With `--split`, the
font pack is always the fixed public set (kana + JIS X 0208) and the token is
used only to fetch your account's radical SVGs up to the subscription level.
The legacy `--out` build without `--chars` calls `GET /user` and
`GET /subjects` and renders your account characters into one combined pack. The
packs stay on your computer and card; nothing is uploaded to a KaniKey server.

By default the generator looks for Noto Sans JP in Tangocho's cache at
`~/code/tangocho/tools/pack-builder/cache/noto/NotoSansJP-Regular.otf`. If that
file is not there, download Noto Sans JP (OFL) from
<https://fonts.google.com/noto/specimen/Noto+Sans+JP> and add
`--font /path/to/NotoSansJP-Regular.otf`.

### 3.3 Copy them to the card

If you downloaded or built packs on a computer, copy them with the card in a
card reader. (If you installed both files with the setup page, skip this
section.) Copy the files to the lowercase `kanikey` folder in the card's root.
The firmware looks for exactly `/kanikey/glyphs.kgp` and
`/kanikey/radicals.kgp`; do not rename them.

macOS, if the card is mounted as `KANIKEY`:

```sh
mkdir -p /Volumes/KANIKEY/kanikey
cp dist/glyphs.kgp /Volumes/KANIKEY/kanikey/glyphs.kgp
```

If you built a radical pack, copy it too:

```sh
cp dist/radicals.kgp /Volumes/KANIKEY/kanikey/radicals.kgp
```

Replace `KANIKEY` with the name the card actually mounts under. On Windows,
create `kanikey` in the card's root and copy the files you have as
`D:\kanikey\glyphs.kgp` and `D:\kanikey\radicals.kgp` (use the card's drive
letter). On Linux, the path is usually
`/media/<user>/<card>/kanikey/`. Eject the card safely before removing it.

### 3.4 Check the packs on the device

The setup page and **About** show both status lines, for example:

```
Font: 19683 glyphs, 64/32/20 px
Rad:  498 radicals
```

The counts come from the packs; your radical count may differ.

`Pack missing` on boot means `glyphs.kgp` is missing or did not open; the
device cannot reach Home until a font pack opens. The status line says `no
pack` when no card or pack is open. A missing `radicals.kgp` leaves all text
working and only changes image-only radical prompts to `no glyph`; the radical
status says `pack missing` (or `pack: no radicals` for a pack with no radical
entries).

In the setup page press **Check pack again**. If a pack is missing or did not
open, the device re-opens both after the page responds; the full font-pack CRC
runs then and can take several seconds, so the new result can appear on the next
page load. On boot, `Pack missing` with `Press Enter to download it` opens the
setup portal; save home Wi-Fi if the setup network appears, then use **Download
pack** in §3.1. A file that does not open reports a `pack: ...` error at boot
(for example `pack: truncated`); a CRC mismatch (`pack: bad crc`) can appear
when **Check pack again** runs or when a pack is replaced from a computer
(§3.6). Copy or download it again. A v1 combined file with radical entries still
works for both roles; one built without radicals serves the font only.

You can configure Wi-Fi and the token before the pack is ready, but the first
sync needs the font pack; reviews and lessons also need it for Japanese text.
Pack sizes, CRC timing and the measurements behind these checks are in
[docs/hardware.md](hardware.md).

### 3.5 When to rebuild it

- A prompt shows `no glyph`: the character is not in the font pack, or the
  radical is not in the radical pack.
- You changed subscription and the radical pack was built for fewer levels than
  your account now allows.
- You changed the font with `--font`.

Normal level-ups do not require a rebuild when the radical pack was built with
your current `max_level_granted`: the generator included every level your
subscription allows. The public font pack already covers the fixed JIS X 0208
set.

### 3.6 Pack updates from a computer (optional)

You do not need this section for normal use: use **Download pack** or **Upload
radical pack** in §3.1. If you prefer to copy a new pack from a computer without
removing the card, you can upload it over Wi-Fi. The device must be on Home,
connected to the computer by USB and on the same network. Close the setup portal
first; `push` waits for Home and otherwise restarts the device.

```sh
uv run --with pyserial python3 tools/device.py push \
  dist/glyphs.kgp /kanikey/glyphs.kgp

uv run --with pyserial python3 tools/device.py push \
  dist/radicals.kgp /kanikey/radicals.kgp
```

`push` uses `/dev/cu.usbmodem1101` by default; add
`--port /dev/cu.usbmodemXXXX` if your device appears on another port. For the
first font pack, use **Download pack** in §3.1 or the card reader in §3.3: a
device without a font pack does not reach Home, and `push` waits for it. An
interrupted upload writes a temporary file and leaves the previous pack in
place. A completed pack is CRC-checked before it replaces the old one.

---

## 4. First boot

### 4.1 The setup network

1. Insert the card and turn the device on.
2. On a first boot the device needs the pack, Wi-Fi and a token. With no Wi-Fi
   and no token saved it starts the setup network below. If Wi-Fi is saved but
   the token is missing, it tries that network and opens the station page when
   it connects, otherwise it falls back to the setup network. If the screen
   shows `Token needed` or `Pack missing`, press `Enter` to open the portal. If
   it shows `Wi-Fi needed` (token saved, Wi-Fi missing), press `Enter` to pick
   the network on the device, or `P` for the phone portal.
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

The page has the **Visual pack** status, **Check pack again**, the **Upload
radical pack** form, the **Time** line and a **Home Wi-Fi** form. Enter your
network name and password and press **Save and connect**. The name field
suggests nearby 2.4 GHz networks. **Download pack** is not offered on this page
because the setup network has no internet; save Wi-Fi first and use the station
page (§3.1).

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
- the token state, `Font: ...` and `Rad: ...`
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

- **Visual pack** — the current font and radical status, **Check pack again**,
  **Download pack** while the font pack is missing, and the **Upload radical
  pack** form. The download is size- and SHA-256-checked; the upload must open
  as a v2 kind-2 pack. Both write a `.tmp` first and `Esc` on the device
  cancels. See §3.1.
- **Time** — the clock and time zone are copied from the browser
  automatically; there is nothing to set. The page shows the zone name and the
  resulting clock.
- **WaniKani token** — paste the token and press **Validate and save**. On
  success it shows `Valid token for <username> (level N).` A 401/403 or a
  network/TLS error is shown and nothing is saved. If a token is already
  stored, the page offers to validate or replace it.
- **Change Wi-Fi** — an expandable form for a new network; see §8. Wi-Fi can
  also be changed on the device without a phone.
- **Finish** — **Start KaniKey**.

**Start KaniKey** validates the token once more if needed and closes the portal.
It does not require the font pack, but the first sync cannot start without it.
When a font pack is ready, the page shows `Setup complete. Starting KaniKey...`,
the device shows `Setup complete` / `Syncing...`, and the first sync runs:
Wi-Fi, Outbox, Account, Summary, Level, SRS and Critical rows, then Home. The
first sync can take several seconds. If the **Visual pack** section says `pack
missing` or `no pack`, use **Download pack** (or build and copy the file, §3.2
and §3.3) and press **Check pack again** before starting.

### 4.3 If setup stops

The portal closes after **15 minutes without a page request**. Any
`KaniKey-XXXX` setup network disappears and the device returns to its boot
screen. Nothing is corrupted. `Esc: cancel` on the device closes it immediately
as well.

Press `Enter` on `Token needed` or `Pack missing` to open setup again. On
`Wi-Fi needed`, `Enter` opens the on-device Wi-Fi list and `P` opens the phone
portal. If both Wi-Fi and the token are already saved and the device boots to
Home, use **Settings › Setup portal** instead. A new setup network gets a new
password, and every portal start gets a new `Page code`. Wi-Fi credentials are
saved when you press **Save and connect**, even if the join then fails; the
token is saved only after it validates. If setup stopped after Wi-Fi but before
the token, reopening goes straight to the station page when the saved network
is reachable, or back to the `KaniKey-XXXX` page when it is not.

Setup makes only a `GET /user` request to WaniKani; it does not start lessons
or submit reviews. The first sync after **Start KaniKey** may send reviews that
were already waiting in the outbox.

---

## 5. Daily use

- Turn the device on. A configured device normally syncs on boot or wake
  (unless **Settings › Auto sync on wake** is Off). `S` on Home or
  **Settings › Sync now** runs a sync manually.
- Home shows the review and lesson counts. `Enter` starts reviews, `L` opens
  lessons, `T` opens stats, `P` opens extra study and `Esc` opens Settings.
  `O` opens the Outbox when a review needs attention; see §6.
- Type the meaning; press `Enter`. Type the reading in romaji; the field shows
  kana; press `Enter`.
- A wrong answer shows the correct answer; press `Enter` to continue. The item
  comes back later in the session.
- `L` opens the lesson list. `Space` selects or deselects a lesson; `Enter`
  opens the selected cards. `Enter` or `Space` turns the card pages; `Enter` on
  the last page moves on, and `Esc` returns to the list. After the last card,
  the batch quiz runs (default **Required**, **Settings › Lesson quiz**) and
  then the start screen appears. `Enter` there starts the whole selected batch
  in WaniKani. Browsing and the quiz never start a lesson. `Q` on a card opens
  an optional local single-item quiz.
- `Esc` in a review opens Pause: Continue, Wrap up, End session or Settings. At
  the end of a session the summary appears; `Enter` returns to Home.
- On battery the screen dims after 30 s, blanks after 60 s and deep-sleeps after
  2 minutes when no review, lesson or menu screen is open. A key wakes it; the
  first key on a blank screen only wakes the panel. USB power prevents deep
  sleep. The timings are under **Settings › Dim after / Blank after / Sleep
  after**.
- Below 20% battery, starting reviews or lessons asks for confirmation. USB
  power skips that prompt.

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
2. Press `Esc` from Home, open **Settings** and choose **Firmware**.
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
running. Leave it running for about 10 minutes after the restart, or until it
completes a sync. A failed check or install stays on the screen with its reason
and keeps the current image.

### 7.2 One-time USB flash for v0.1.0

A device still running v0.1.0 cannot follow GitHub's redirects, so its update
check fails. Flash it once over USB; later releases update over the air.

Download `kanikey-cardputer-adv.bin` from
<https://github.com/dcarnicer/kanikey/releases/latest>, then write the released
app-only image to the app slot at `0x10000`:

```sh
python3 -m pip install esptool
python3 -m esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX \
  write_flash 0x10000 kanikey-cardputer-adv.bin
```

Replace `/dev/cu.usbmodemXXXX` with the port the device appears on (on macOS,
`ls /dev/cu.usbmodem*`; on Windows, `COMx`). If you prefer to build from a
checkout and have PlatformIO Core installed:

```sh
cd firmware
pio run -e cardputer-adv -t upload --upload-port /dev/cu.usbmodemXXXX
```

This USB path is for a v0.1.0 device, whose active slot is `app0`. Do not use it
to move a device that has already updated itself.

---

## 8. Changing Wi-Fi

You can change Wi-Fi on the device or from the setup portal. Both are disabled
during a review; end the session first.

### 8.1 On the device (no phone)

1. Press `Esc` from Home and open **Settings › Account › Wi-Fi**. The boot
   `Wi-Fi needed` screen opens the same list with `Enter`; `P` there opens the
   phone portal instead.
2. The device scans 2.4 GHz networks and lists each name with its signal and
   security. Up/Down selects, `Enter` chooses and `R` rescans. For a hidden
   network choose `+ Enter network name`, type the name and press `Enter`.
3. On the password screen type the password; `Tab` shows or hides it. Press
   `Enter` to connect (8–63 characters; leave it empty for an open network).
   `Esc` goes back.
4. If the join succeeds, the credentials are saved; from Settings the note
   `Wi-Fi: <name>` appears, and from the boot screen the device continues
   booting. If it fails, the reason stays on screen; check the password and
   that the network is 2.4 GHz. A 5 GHz-only network does not appear in the
   list.

### 8.2 From the setup portal

1. Press `Esc` from Home and open **Settings › Setup portal**. It is disabled
   during a review; end the session first.
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
| `Pack missing` / setup page `pack missing` or `no pack` | `/kanikey/glyphs.kgp` is missing or could not be opened | Press `Enter` to open setup; on the station page use **Download pack** (§3.1). If the release has no pack, build it (§3.2) and copy it (§3.3), then press **Check pack again** |
| `pack: bad crc` or another `pack: ...` error | The pack file is damaged, truncated or from another format | Copy the correct pack again (§3.3) or download it again; if it keeps failing, rebuild it (§3.2) |
| **Download pack** fails | The button needs the station page and a release manifest with a `pack font` record; an offline, TLS, HTTP, size or hash error stops the transfer | Join home Wi-Fi and try again. If the page says `The release manifest has no font pack`, build a pack (§3.2) and copy it (§3.3). A transfer failure leaves the old file in place |
| **Upload radical pack** says `Not a radical pack: ...` / `pack: wrong kind` | The file is not a KKGP v2 kind-2 radical pack, is truncated, or is a v1 combined file | Use `dist/radicals.kgp` from `build.py --split` with a token (§3.2) and upload it again. The old radical pack is kept |
| Pack transfer stops when you press `Esc` on the device | The download or upload was cancelled; the `.tmp` is deleted | Start the transfer again; the old file is unchanged |
| The Wi-Fi list is empty, or a network is missing or cannot be joined | No 2.4 GHz network was found, the network is 5 GHz-only, or its security type (WEP, 802.1X, WAPI) is not supported | Press `R` to rescan, move closer, or choose `+ Enter network name` for a hidden SSID. Use a WPA2/WPA3 network; the phone portal uses the same Wi-Fi stack |
| `no glyph` on an item | The character is not in the font pack, or the radical is not in the radical pack | For a radical, build a radical pack from your account (§3.2) and upload it (§3.1). For a character, build and copy a font pack that includes it (§3.2, §3.3) |
| `Token rejected` / `Auth: token rejected` | HTTP 401: the token was revoked, deleted or mistyped | Create a new token, then **Settings › Setup portal** and paste it |
| `Missing permissions` / `Auth: missing permissions` | HTTP 403: the token lacks a permission | Create a new token with the missing permission and paste it in **Setup portal**; only the affected features stop |
| `Rate limited; retry in Ns` / `Retry in Ns` | HTTP 429 | Wait for the countdown. The shared gate pauses every request until then; do not power-cycle the device repeatedly |
| `Offline - ...` / `Stale cache - ...` | No Wi-Fi or DNS, or an old cache | Cached reviews still work. Check the 2.4 GHz network and press `S` to sync |
| `Sync failed` / `Check Wi-Fi and token` | The first sync could not finish | Check the 2.4 GHz network and the token, then press `Enter: retry`; use **Setup portal** if the token is wrong |
| `Could not join "<ssid>"` | Wrong password, a 5 GHz-only network, or the network is out of range | Re-enter the password on the setup page or **Settings › Account › Wi-Fi**; use a 2.4 GHz network |
| Setup network disappeared, or the page no longer loads | The portal closed after 15 idle minutes, or the phone is on mobile data | Press `Enter` on `Token needed`/`Pack missing`; on `Wi-Fi needed` press `Enter` for the on-device list or `P` for the portal. Once Home, use **Settings › Setup portal**. Reconnect the phone to the device network or home Wi-Fi |
| `Unlock` asks for a code; `Wrong code` / `Too many tries` | The station page is locked | Type the 6-digit `Page code` from the screen. After five wrong codes, use the new code on screen |
| The clock or time zone is wrong | The browser's clock or zone was wrong, or the clock has not synced | Reopen **Setup portal** from a phone with the correct time and zone. A sync sets the clock from the network but does not change the time zone |
| `Outbox: N pending` | Reviews are queued and not sent yet | They will send on the next sync; press `S` when Wi-Fi is back |
| `Outbox: N need attention` / `HTTP 422` / `HTTP 409` / `gave up` | A review was rejected or could not be sent | Open `O`, read the reason, then discard it if it is no longer useful. Discarding does not change WaniKani |
| `Outbox full` | All 128 outbox slots are used | Sync to flush, or dismiss rejected/stuck items in `O` |
| `Storage unreadable` | The internal storage for the cache/outbox cannot be read | `Enter` formats it, which loses pending reviews and the cache; `Esc` continues without storage (no outbox or cache) |
| Firmware check fails | No Wi-Fi/TLS, no release is available, or the device is still on v0.1.0 | Check the network and try **Check now** again; if this is a v0.1.0 device, use the USB flash in §7.2. The current image is unchanged |
| The screen is black | The device blanked or deep-slept to save power | Press a key. The first press only wakes the screen |
| Low-battery prompt | The battery is below 20% | Charge it, or connect USB power |

---

## 10. Privacy

- The WaniKani token stays in NVS on the Cardputer. It is not sent to any
  KaniKey server, is never logged and is never shown back.
- There is no KaniKey account, cloud sync or telemetry. The device contacts
  `api.wanikani.com`, its NTP servers and GitHub (firmware updates and the
  public font pack).
- The visual pack, cache and outbox stay on your card and device. WaniKani
  content is fetched with your token and is not shipped in the repository. The
  public font pack contains only glyphs from an OFL font and no WaniKani
  content; the radical pack is built from your account and is personal. The
  font download involves no token, and the radical upload stays on your local
  network.
- The setup access point is local and WPA2-protected. The station page is
  locked with the 6-digit code because it is reachable from the whole home
  network. The setup pages themselves use HTTP on the local network, so set up
  on a network you trust.
- **Sign out** removes the token and all cached account data: cache, journal,
  outbox, audio cache and practice data. It keeps Wi-Fi and the visual pack on
  the card.

---

## 11. What KaniKey does not do

- It does not replace WaniKani. The SRS schedule lives in WaniKani; the device
  keeps only a local copy of what it has shown and submitted.
- It does not implement a local SRS for reviews. Extra study and the lesson quiz
  are local practice; they never send reviews or start lessons.
- It does not have its own login, password or cloud service. Access is the
  WaniKani API token, which you can revoke and replace in WaniKani.
- It does not ship WaniKani content. Meanings, readings, mnemonics and radical
  images are requested with your token and stored on your card. The public font
  pack contains no WaniKani content; a radical pack built from your account is
  personal and must not be published or shared. Audio is not played yet (see
  §12).
- It does not support phones, watches or other boards. This firmware is for the
  Cardputer ADV only.
- It does not provide a forum, community, push notifications, an app store
  listing or a mobile app.
- It is an unofficial third-party app. It is not affiliated with, endorsed by or
  sponsored by WaniKani or Tofugu LLC.

---

## 12. Planned, not in this firmware

These are future plans, not features you can use today:

- A pack generator in the phone's browser that renders new glyphs or radical
  images. The setup page can download the published public font pack and accept
  a prepared radical pack, but it does not build either from a character list.
- On-device rendering of characters that are not in the pack.
- Vocabulary pronunciation playback and stroke order. The firmware can parse
  pronunciation metadata, but playback is not enabled yet.

---

## Appendix A — Custom pack options

The no-token split command builds the fixed public font set with Noto Sans JP.
Extra options:

```sh
# Test pack from a character list: no token, no radical bitmaps
uv run --with pillow python3 tools/glyph-pack/build.py \
  --chars "食休日本語" --out dist/glyphs-test.kgp

# Custom font, public character set: font pack only, no token
uv run --with pillow python3 tools/glyph-pack/build.py \
  --split dist --font /path/to/font.otf

# Legacy combined pack from your account, with radicals
WANIKANI_TOKEN=... uv run --with pillow --with cairosvg \
  python3 tools/glyph-pack/build.py --out dist/glyphs.kgp
```

| Option | Effect |
|---|---|
| `--split DIR` | Write `DIR/glyphs.kgp` (font) and, with a token, `DIR/radicals.kgp` (radical); without a token, font only |
| `--out PATH` | Write one combined v2 kind-0 pack (legacy); default `dist/glyphs.kgp`. Needs a token, `--chars` or `--tangocho`; cannot be combined with `--split` |
| `--font PATH` | Use another OFL font file |
| `--chars "食休日本語"` | Build the font from this character list; without a token, no API and no radicals |
| `--no-kana` | Omit the kana block in a `--chars`, `--tangocho` or `--split` build |
| `--tangocho` | Build a combined pack from Tangocho's local kanji cache; no token, no radicals; not with `--split` |
| `--token TOKEN` | Same as the `WANIKANI_TOKEN` environment variable |

A pack built without a token has no radical bitmaps; image-only radical prompts
then show `no glyph`.

# KaniKey firmware

KaniKey firmware for the M5Stack Cardputer ADV.

## What it does

- Starts the board layer: display, TCA8418 keyboard, battery and heap report.
- Shows user-facing boot states for a missing SD card, visual pack, token or
  Wi-Fi, and for sync failures. `Enter` retries; no debug UI is shown by
  default.
- Mounts a FAT32 microSD on SPI3 and prints its size.
- Shows every key event on screen and over serial: TCA8418 code, grid position,
  action and character on the main/Shift layer.
- `Esc` from Home opens Settings: brightness, dim/blank/sleep timings, review
  batch size/order, lesson batch size, Setup portal, Sync now, Firmware
  (check/install an OTA release), About (version, slot, pack, cache, heap, last
  sync, zone), Clear cache and Sign out. Values persist in NVS. In a review,
  `Esc` opens Pause (Continue / End session / Settings) instead of quitting
  immediately. Below 20% on battery, starting reviews or lessons asks for
  confirmation.
- Firmware updates use the second 1.75 MiB app slot: an HTTPS manifest with the
  version, size and SHA-256, `Update` streaming, and bootloader rollback if the
  new image does not reach the loop. See [docs/ota.md](../docs/ota.md).
- On first boot, or when Wi-Fi/token are missing, starts a WPA2 setup access
  point `KaniKey-XXXX` (password on screen). The phone saves the home Wi-Fi,
  the device joins it and serves the second setup page at its station IP.
- On that page the WaniKani token is validated with `GET /user`, the browser
  sets the clock/time zone, and the visual pack can be checked. Setup only
  reads `/user`; it never starts lessons. The first sync after Finish may flush
  the existing outbox.
- Joins the saved network, syncs time with SNTP and calls `GET /v2/user` and
  `GET /summary`.
- With a token, the Home screen shows the account level, due reviews, lessons,
  next review and last sync.
- Shows distinct `Token rejected`, `Missing permissions`, `Rate limited` and
  `WaniKani error` states; `Enter` retries or opens setup.
- Measures internal heap before and after each TLS request.
- Draws Japanese prompts and readings from the microSD visual pack
  (`/kanikey/glyphs.kgp`); the token pack on the card has 7,269 glyphs at
  64/32/20 px and 498 radical bitmaps.
- Draws radical prompts from the pack's 96 px radical bitmaps and includes
  radical-only lessons/reviews. Lesson cards append component meanings, hints
  and the user's study-material notes to the paged mnemonic.
- Fetches `/v2/study_materials` for review and lesson subjects: meaning
  synonyms are accepted answers and meaning/reading notes appear on the lesson
  card and review feedback. A token without that permission still works without
  notes.
- Review sessions follow WaniKani `reviews_presentation_order` (`shuffled` or
  `lower_levels_first`). `Q` on a lesson card opens an optional local
  meaning/reading quiz with feedback, retries and a completion screen; the quiz
  itself makes no `/start` call.
- Reads `current_vacation_started_at` and shows a non-blocking `Vacation mode`
  notice on Home when no more urgent status is present.
- Shows the current SRS stage and lifetime accuracy in the review header, and
  the new stage returned by `POST /v2/reviews` plus the last item's review
  history in the session summary (`SRS 3 -> 4`, `History 18 reviews, 89%`).
- Fetches `/v2/level_progressions` on sync and shows the current level's
  unlock date in the Home status line (`L8 since 03 Feb`).
- `Tab` on review feedback opens the item detail card (meanings, readings,
  mnemonics, notes, components, related subjects; vocabulary adds parts of
  speech and context); `Space` pages and `Tab`/`Esc` returns. Review batches
  keep mnemonics/hints and fetch component subjects for it.
- Writes the pack to the card over the USB serial link or a one-shot Wi-Fi
  upload server.

The Home screen shows live `/user` and `/summary` data. Enter fetches a
20-item due batch, runs a typed meaning/reading session and submits completed
items with `POST /v2/reviews`. Results are persisted in `/state/outbox.bin`
before the request and retried after a reboot. The offline path is verified.
The Home snapshot (summary, user and batch assignments) is persisted in
`/state/cache.bin`, and the batch subjects in `/kanikey/cache/subjects.bin` on
the SD card. After a reboot without network the device restores Home, shows
`Offline` and can run the cached batch; completed items are removed from the
cache and kept in the outbox. Both files use a version, token fingerprint,
CRC32 and `.tmp`/`.bak` writes.
The panel dims after 30 s, blanks after 60 s and deep-sleeps after 2 min when
no review is active. Hardware measurements are in
[docs/hardware.md](../docs/hardware.md).

The full-screen canvas is **8-bit RGB332** (32,400 B). A 16-bit canvas is
64,800 B and could not be reallocated after the subject batches fragmented the
heap; 8 bits always reallocate. `efontJA_16` is no longer linked: Japanese text
comes from the pack, and a missing glyph shows an explicit `no glyph` state.
Removing the font freed ~468 KB of flash (1,741,705 B → 1,273,425 B before the
cache work).

## Visual pack

Run these from the repository root. Generate the full kanji/kana pack locally
(the generated file is ignored by git; it is not WaniKani content shipped with
the app):

```sh
uv run --with pillow python3 tools/glyph-pack/build.py \
    --tangocho --out dist/glyphs-full.kgp
```

Write it to the card. The Wi-Fi path is the reliable one for multi-megabyte
files:

```sh
uv run --with pyserial python3 tools/device.py push \
    dist/glyphs-full.kgp /kanikey/glyphs.kgp
```

`push` opens the serial port, waits for Home, sends `S` to start a one-shot
HTTP server on port 8080 and runs `curl -T`. For a small file the USB path
also works:

```sh
uv run --with pyserial python3 tools/device.py upload \
    dist/glyphs-test.kgp /kanikey/glyphs.kgp
```

The serial uploader uses 2 KiB chunks with an ACK per chunk (the USB RX ring is
4 KiB). It is useful for small files but was not reliable enough for the 2.9 MB
full pack; use `push` for that.

The serial `k` token helper exists only in the development image. The release
image configures the token through the setup portal and stores it in NVS; it is
masked on screen and never logged.

## Build

```sh
cd firmware
pio run -e cardputer-adv
```

For the M0 hardware/debug screen and the `Tab: debug` shortcut, build the
development image instead:

```sh
pio run -e cardputer-adv-dev
```

The build uses two 1.75 MiB OTA slots and 4.44 MiB LittleFS, the same layout as
Tangocho's Cardputer ADV image. `Version.h` is the release version and must
match the `vX.Y.Z` tag; `platformio.ini` sets `KANIKEY_ENV="cardputer-adv"` for
the OTA row. The release image is about 1.46 MB (79.6% of the slot); the dev
image adds the serial remote and debug screens and is about 1.474 MB.

The release image keeps a small serial console (`h`, `i`, `p`, `S`, `up`, `r`);
build `cardputer-adv-dev` for the full remote. The loop task uses a 24 KiB
stack, enough for the lesson fetch and OTA install paths.

## Flash

```sh
pio run -e cardputer-adv -t upload --upload-port /dev/cu.usbmodemXXXX
pio device monitor -p /dev/cu.usbmodemXXXX -b 115200
```

The Cardputer's USB Serial/JTAG should appear as a `usbmodem` port. On macOS,
`pio device list` shows it; if only Bluetooth/debug ports appear, the board is
off, the cable is charge-only, or the USB port is not enumerating.

## Live screenshots

The firmware's `p` command dumps the 240×135 canvas as RGB332 bytes. Capture it
and save a PNG:

```sh
uv run --with pyserial --with pillow python3 tools/shot.py --out tools/screenshots/out/home.png
```

The board may reset when the port opens; the script waits for the Home screen
before asking for the dump.

## Serial helper

Use the host helper rather than a raw monitor. On this board, DTR/RTS must be
deasserted or the USB CDC port stays silent:

```sh
uv run --with pyserial python3 tools/device.py monitor
uv run --with pyserial python3 tools/device.py wifi
uv run --with pyserial python3 tools/device.py token
```

`wifi` prompts for the SSID and password, sends `w <ssid>|<password>` and never
prints or stores the password. `token` prompts for the WaniKani token, sends
`k <token>` and runs the authenticated `/user` probe. Both commands exist only
in the development image; a release device uses the setup portal.

## Serial commands

The release image keeps only field diagnostics and the asset path. The full
remote is compiled into `cardputer-adv-dev` (`KANIKEY_DEV`): token/Wi-Fi
helpers, synthetic data and the `gDirectReading` answer bypass are not in a
release binary.

| Key | Image | Action |
|---|---|---|
| `h` | both | Print free/largest/minimum heap and the loop stack high-water mark |
| `i` | both | Print the visual pack status |
| `p` | both | Dump the 240×135 canvas for `tools/shot.py` |
| `S` | both | Start the one-shot Wi-Fi asset upload server |
| `up <bytes> <path>` | both | Write a file to the card over USB (2 KiB ACKed chunks) |
| `r` | both | Restart |
| `w` | dev | Join the saved Wi-Fi network |
| `w <ssid>\|<pass>` | dev | Save Wi-Fi credentials and join |
| `t` | dev | Join if needed, then run the TLS probe |
| `u` | dev | Run the authenticated `GET /user` probe |
| `k <token>` | dev | Save a token in NVS and probe `/user` |
| `c` | dev | Clear the saved token |
| `s` | dev | Scan Wi-Fi networks (when not connected) |
| `d` | dev | Disconnect Wi-Fi (test offline) |
| `o` | dev | Print the outbox |
| `f` | dev | Reconnect and flush the outbox |
| `x` | dev | Queue a synthetic review (test persistence) |
| `z` | dev | Clear the outbox (test only) |
| `J` | dev | Seed synthetic rejected/stuck outbox items |
| `C` | dev | Seed a synthetic offline cache (needs a token) |
| `X` | dev | Clear the synthetic cache |
| `R <id>` | dev | Render a radical bitmap by subject id |
| `P` | dev | Open the setup portal (Wi-Fi/token/time/pack) |
| `v` | dev | Fetch (if needed) and start a review session |
| `l` | dev | Fetch and show the lesson list/picker |
| `g` | dev | Next mnemonic page in a lesson, or next review-detail page |
| `D` | dev | Toggle the review detail card from feedback |
| `Q` | dev | Open the optional lesson quiz for the current card |
| `y <text>` | dev | Type text into the current review or lesson-quiz answer |
| `e` | dev | Open the selected lesson / check the answer / advance / start |
| `q` | dev | Quit a review/lesson or close Settings/Firmware |
| `b` | dev | Fetch a 20-item review batch (logs only) |
| `m` | dev | Fetch the summary (logs only) |
| `U <url>` | dev | Point the OTA check at a local manifest (`http://` allowed) |
| `F` | dev | Run a firmware manifest check |
| `I` | dev | Install the offered firmware |
| `V` | dev | Open the Firmware screen |
| `Y` | dev | Clear the remembered rollback version |

## Setup portal

On first boot, or from **Settings › Setup portal** (the dev image also has
`P`), the device starts a WPA2 access point:

1. The screen shows the network (`KaniKey-XXXX`), its password and
   `http://192.168.4.1/`.
2. The phone joins it and saves the home Wi-Fi. The AP then closes; the device
   joins the home network and shows its station IP.
3. The phone moves to the same home network and opens that IP. The page stores
   the WaniKani token only after `GET /user` succeeds, sets the clock and POSIX
   time zone from the browser, and can re-check `/kanikey/glyphs.kgp`.
4. **Start KaniKey** closes the portal and runs the first sync.

The token and Wi-Fi password are never logged, never shown back on the page and
never written to the repository. The portal does not call
`PUT /assignments/{id}/start`, so setup does not move lessons in WaniKani. The
normal first sync after **Start KaniKey** may flush the existing outbox. There
is no deck catalog or progress restore; those Tangocho routes were dropped.

## OTA

The release environment publishes `kanikey-cardputer-adv.bin` and
`kanikey-firmware.txt` from a `vX.Y.Z` tag. The device checks the manifest after
a successful sync (once a day) and from Settings › Firmware. The install keeps
settings, the outbox and the cache; a failed first boot rolls back to the
previous slot. See [docs/ota.md](../docs/ota.md) for the format, trust model and
release steps.

The serial `U <url>`, `F`, `I`, `V` and `Y` commands exist only in the
development image. They point the device at a local manifest, run a check,
install, open the Firmware screen and clear a rollback record.

The default GitHub release URL requires a public repository and release. A
private fork must publish the manifest and image elsewhere and change the
default URL, or test with the development image and a local manifest.

## Notes

- The board layer and keyboard map come from
  [Tangocho](https://github.com/dcarnicer/tamagotcho) (MIT).
- The root CA bundle is Tangocho's `platform/RootCerts.h`.
- If the TLS probe fails with a certificate error, add the missing root to
  `src/platform/RootCerts.h` and retry. Do not disable verification.

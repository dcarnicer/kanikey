# KaniKey firmware

First hardware milestone for the M5Stack Cardputer ADV.

## What it does

- Starts the board layer: display, TCA8418 keyboard, battery and heap report.
- Mounts a FAT32 microSD on SPI3 and prints its size.
- Shows every key event on screen and over serial: TCA8418 code, grid position,
  action and character on the main/Shift layer.
- If Tangocho already saved Wi-Fi credentials in NVS namespace `net`, joins that
  network, syncs time with SNTP and calls `GET /v2/user` and `GET /summary`.
- With a token, the Home screen shows the account level, due reviews, lessons,
  next review and last sync.
- Measures internal heap before and after each TLS request.
- Draws Japanese prompts and readings from the microSD visual pack
  (`/kanikey/glyphs.kgp`); the first full pack has 6,621 glyphs at 64/32/20 px.
- Writes the pack to the card over the USB serial link or a one-shot Wi-Fi
  upload server.

The Home screen shows live `/user` and `/summary` data. Enter fetches a
20-item due batch, runs a typed meaning/reading session and submits completed
items with `POST /v2/reviews`. Results are persisted in `/state/outbox.bin`
before the request and retried after a reboot. The offline path is verified.
The panel dims after 30 s, blanks after 60 s and deep-sleeps after 2 min when
no review is active. Hardware measurements are in
[docs/hardware.md](../docs/hardware.md).

The full-screen canvas is **8-bit RGB332** (32,400 B). A 16-bit canvas is
64,800 B and could not be reallocated after the subject batches fragmented the
heap; 8 bits always reallocate. `efontJA_16` is now only the fallback for
characters missing from the pack (principally radicals). The font reference
still costs ~468 KB of flash; removing it needs radical images and a
"visual pack missing" screen.

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

The serial uploader uses 8 KiB chunks with an ACK per chunk. It is useful for
small files but was not reliable enough for the 2.9 MB full pack; use `push`
for that.

The token is a development convenience. Production KaniKey will use the setup
portal and the same NVS storage, with the token masked on screen and never
logged.

## Build

```sh
cd firmware
pio run -e cardputer-adv
```

The build uses two 1.75 MiB OTA slots and 4.44 MiB LittleFS, the same layout as
Tangocho's Cardputer ADV image.

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
`k <token>` and runs the authenticated `/user` probe.

## Serial commands

| Key | Action |
|---|---|
| `w` | Join the saved Wi-Fi network |
| `t` | Join if needed, then run the TLS probe (with the saved token, if any) |
| `u` | Run the authenticated `GET /user` probe |
| `k <token>` | Save a token in NVS and probe `/user` |
| `c` | Clear the saved token |
| `s` | Scan Wi-Fi networks (when not connected) |
| `h` | Print free/largest/minimum internal heap |
| `i` | Print the visual pack status |
| `S` | Start the one-shot Wi-Fi asset upload server |
| `up <bytes> <path>` | Write a file to the card over USB (8 KiB ACKed chunks) |
| `d` | Disconnect Wi-Fi (test offline) |
| `o` | Print the outbox |
| `f` | Reconnect and flush the outbox |
| `x` | Queue a synthetic review (test persistence) |
| `z` | Clear the outbox (test only) |
| `b` | Fetch a 20-item review batch (logs only) |
| `v` | Fetch (if needed) and start a review session |
| `l` | Fetch and show the first lesson card |
| `g` | Next mnemonic page in a lesson |
| `y <text>` | Type text into the current answer (test helper) |
| `e` | Check the answer / advance after feedback / start the lesson |
| `q` | Quit the review session |
| `r` | Restart |

## Wi-Fi for this milestone

The setup portal is not built yet. This build reads the credentials Tangocho
already stores in NVS (`net` / `ssid` / `pass`). Flashing an app-only image keeps
NVS, so a Cardputer previously configured with Tangocho should join the same
network. A production KaniKey will use its own setup flow.

## Notes

- The board layer and keyboard map come from
  [Tangocho](https://github.com/dcarnicer/tamagotcho) (MIT).
- The root CA bundle is Tangocho's `platform/RootCerts.h`.
- If the TLS probe fails with a certificate error, add the missing root to
  `src/platform/RootCerts.h` and retry. Do not disable verification.

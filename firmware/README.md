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

It does not sync WaniKani data yet. The Home screen shows live `/user` and
`/summary` data. Hardware measurements are in
[docs/hardware.md](../docs/hardware.md).

M1 uses M5GFX's `efontJA_16` as a temporary kanji source; the final visual pack
is still the plan. The font reference adds ~468 KB of flash and keeps the app
at 88.6% of its slot.

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

The firmware's `p` command dumps the 240×135 canvas as RGB565 rows. Capture it
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

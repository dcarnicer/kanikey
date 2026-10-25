# KaniKey firmware

First hardware milestone for the M5Stack Cardputer ADV.

## What it does

- Starts the board layer: display, TCA8418 keyboard, battery and heap report.
- Mounts a FAT32 microSD on SPI3 and prints its size.
- Shows every key event on screen and over serial: TCA8418 code, grid position,
  action and character on the main/Shift layer.
- If Tangocho already saved Wi-Fi credentials in NVS namespace `net`, joins that
  network and runs a TLS `GET /v2/user` against `api.wanikani.com` without a
  token. A `401` proves DNS, TLS, the certificate chain and the endpoint.
- Measures internal heap before and after the TLS client.

It does not sync WaniKani data yet.

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

## Serial commands

| Key | Action |
|---|---|
| `w` | Join the saved Wi-Fi network |
| `t` | Join if needed, then run the TLS probe |
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

# KaniKey — developer tools

This page is for people who build the firmware and test it on a device. For
the user's view, see [setup.md](setup.md). The full serial table is in
[firmware/README.md](../firmware/README.md); this page covers the build
environments and the hooks added since then.

---

## 1. Build environments

Run these from `firmware/`. Host tests run with `pio test -e native`.

| Environment | Build | What it is |
|---|---|---|
| `cardputer-adv` | `pio run -e cardputer-adv` | Release image for the Cardputer ADV. Keeps only the small serial console. |
| `cardputer-adv-dev` | `pio run -e cardputer-adv-dev` | Adds `KANIKEY_DEV`: the full serial remote, the M0 debug screen and the `Tab` shortcut, token and Wi-Fi helpers, synthetic data, plain-`http://` OTA, and the `rem`, `cap` and `devhost` hooks. Never ship it. |
| `cardputer-adv-badapi` | `pio run -e cardputer-adv-badapi` | Test image from `cardputer-adv-dev` with `KANIKEY_DEV_FAIL_API=1`, version 0.1.2. Every WaniKani request fails before a socket opens, so it never reports an API success. Only for the FIX-20 rollback test (§4.3). |
| `paperdiy` | `pio run -e paperdiy` | M5Stack PaperDIY (ESP32-S3, 16 MB flash, 8 MB octal PSRAM, 4.7-inch e-paper used at 960×540). **Build only:** nothing has run on a PaperDIY yet. |
| `paperdiy-dev` | `pio run -e paperdiy-dev` | The PaperDIY build with `KANIKEY_DEV`. The board has two buttons and no keyboard, so most dev keys cannot be reached until PD-03 (see [`spec/10-bluetooth-paperdiy.md`](spec/10-bluetooth-paperdiy.md)). |

The Cardputer images use `partitions/kanikey_8MB.csv`. The PaperDIY images use
`partitions/kanikey_16MB.csv` and M5GFX 0.3.0.

Flash and monitor a Cardputer image:

```sh
pio run -e cardputer-adv-dev -t upload --upload-port /dev/cu.usbmodemXXXX
pio device monitor -p /dev/cu.usbmodemXXXX -b 115200
```

Use `tools/device.py monitor` rather than a raw monitor, because DTR/RTS must be
deasserted or the USB port stays silent (see firmware/README.md).

---

## 2. Serial console on the development image

Type a command and press Enter over the USB serial port. Unknown input prints
the command list. Commands are case-sensitive.

**Opening the USB port resets the board.** `tools/shot.py` and `tools/session.py`
wait for the boot after opening it. Separate `tools/device.py` calls cannot keep
UI state, so use `tools/session.py` for a sequence:

```sh
uv run --with pyserial python3 tools/session.py --wait 4 "T" "C"
```

Use `tools/shot.py` for a PNG of the 240×135 canvas.

### 2.1 `rem` — deep-sleep timer probe (REM-01)

| Command | Effect |
|---|---|
| `rem <s> [n]` | Arm a timer for `s` seconds (1–86400) to wake `n` times (1–32, default 1), then deep-sleep now. The key wake stays armed. Runs on battery or USB. |
| `rem` | Print the record and the wake history of the last run. |
| `rem tone` | Play one 880 Hz tone and log the heap before and after it. |

Each probe wake goes back to sleep before storage, Wi-Fi or sync start, so no
probe wake joins Wi-Fi. A key wake, a reset or a power-on ends the run and boots
normally. The record survives deep sleep in RTC memory, and `seq` counts up
across runs until power is cut. Read `[rem] hist` lines after the run: USB drops
while the chip sleeps. The log lines and the 2026-11-20 results are in
[`spec/08-reminders-audio.md`](spec/08-reminders-audio.md) (REM-01).

### 2.2 `cap <bytes>` — body cap (FIX-14)

`cap <bytes>` makes WkClient treat a response body as truncated once it passes
`<bytes>`. It can never raise the 32 KB buffer.

- `cap 0` turns it off; `cap` alone prints the value.
- The setting is in RAM; a reboot clears it.
- While it is set, each request logs `[http] dev cap <bytes>, <path> effective <n>`.
- Run `cap 0` when you are done.

The procedure for the subject-page fallback is in
[`spec/01-fixes.md`](spec/01-fixes.md) (FIX-14, "Dev hooks").

### 2.3 `devhost <host> <port>` — send requests elsewhere (FIX-06)

| Command | Effect |
|---|---|
| `devhost <host> <port>` | Send every WkClient request to `<host>:<port>`, with certificate checks **off**. |
| `devhost off` | Restore `api.wanikani.com:443`. |
| `devhost` | Print the current target. |

- The `Host` header stays `api.wanikani.com`.
- The host name is 1–63 letters, digits, `.` or `-`. A name containing
  `wanikani` is refused. The port is 1–65535.
- The setting is in RAM; a reboot clears it.
- Requests keep their `Authorization: Bearer` header, so **the host you name
  receives your WaniKani token**. Use a throwaway token, and only a test server
  you run yourself on your own network.
- **Never use `devhost` for real writes.** A test server can answer 200 to a
  review or a lesson start that never reached WaniKani, and the device can then
  remove the review from its outbox as sent. Turn it off before you submit or
  start anything real.
- At connect, the log shows `[http] dev host <ip>:<port>, certificate check off`.

The test server is in §3.1; the procedure is in
[`spec/01-fixes.md`](spec/01-fixes.md) (FIX-06, "Dev hooks").

### 2.4 Other hooks

The firmware README serial table covers the rest: `h`, `i`, `p`, `S`, `up`, `w`,
`t`, `u`, `k`, `o`, `f`, `J`, `C`, `U`, `F`, `I`, `V`, `Y` and more.

---

---

## 3. Local test servers

Both tools use only the Python standard library. Neither touches the repository.

### 3.1 `tools/slow_server.py` — stalled and slow peers (FIX-06)

It stands in for `api.wanikani.com` so you can see how WkClient behaves on a
stalled or slow peer. Nothing here talks to WaniKani.

| Mode | Behaviour |
|---|---|
| `stall` | Accepts TLS, reads the request and never answers. A GET should give `httpCode 0` after its `timeoutMs` (about 20 s), not after 1 s. |
| `slow` | Waits `--delay` seconds (default 3), sends the headers, then trickles the body at `--rate` bytes/s (default 1) for `--body-len` bytes (default 200). A body longer than the timeout shows truncation at the deadline. |
| `log` | Answers every request at once with 200 and `{}`. A resent POST shows as a second line. |

```sh
python3 tools/slow_server.py stall --port 8443
```

Options: `--port` (8443), `--ip` (the LAN IP to print and certify), `--cert` and
`--key` (your own PEM files), `--certdir` (default `/tmp/kanikey-devhost`).
Without `openssl` on the PATH, make the certificate by hand and pass `--cert` and
`--key`; the script prints the command.

The script prints the LAN IP and the exact `devhost` line, then one line per
request: time, client, method, path, body length and a body preview of up to 300
bytes. It never logs headers. The certificate is self-signed and the device
accepts it only while `devhost` is active.

On the device, `devhost <ip> 8443`, trigger the request under test (`t` for the
probe, or a review submit), then `devhost off`.

### 3.2 `tools/ota_local.py` — firmware over plain HTTP (FIX-20)

A dev build accepts `http://` for the manifest and the image; a release build
does not. The tool serves one firmware image and a release manifest on the LAN
so the dev console can install it:

```sh
python3 tools/ota_local.py --bin firmware/.pio/build/cardputer-adv-badapi/firmware.bin \
    --version v0.1.2 --port 8080
```

`--version` must match the version compiled into the image; the tool checks this
before it serves. `--env` defaults to `cardputer-adv`. The files are copied to a
temporary directory. The printed URL is what you type on the device:

```
U http://<mac-lan-ip>:8080/kanikey-firmware.txt
F
I
```

### 3.3 The FIX-20 two-strike rollback test

With `cardputer-adv-badapi` served by `ota_local.py`, the device installs
0.1.2, starts it and resets before the image is marked valid. After one reset
the log reads `rollback count 1: still offered`; after a second, `blacklisted`
and `F` shows `Previous update failed`. Clear the record with `Y` before you
retest. The full steps and expected log lines are in
[`spec/01-fixes.md`](spec/01-fixes.md) (FIX-20, "Dev hooks").

---

## 4. Known gaps

- The in-device `[kk] commands:` line does not list `cap` or `devhost` yet, and
  [`firmware/README.md`](../firmware/README.md)'s serial table does not list
  `rem`, `cap` or `devhost`. This page is the reference for them.

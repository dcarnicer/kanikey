#!/usr/bin/env python3
"""Small serial helper for KaniKey development.

The ESP32-S3 USB Serial/JTAG needs DTR and RTS deasserted to deliver data; a
plain `pio device monitor` can leave the port silent. This script opens the
port correctly, prints device output and can send the development commands.

    uv run --with pyserial python3 tools/device.py monitor
    uv run --with pyserial python3 tools/device.py wifi
    uv run --with pyserial python3 tools/device.py token
    uv run --with pyserial python3 tools/device.py cmd t

The Wi-Fi password and the WaniKani token are read with getpass, sent to the
device and never written to disk or printed back.
"""
from __future__ import annotations

import argparse
import getpass
import sys
import time

import serial

DEFAULT_PORT = "/dev/cu.usbmodem1101"
BAUD = 115200


def open_port(port: str) -> serial.Serial:
    s = serial.Serial(port, BAUD, timeout=0.3)
    # Important: asserting DTR/RTS holds this board in reset / silences CDC.
    s.dtr = False
    s.rts = False
    return s


def pump(s: serial.Serial, seconds: float) -> None:
    end = time.time() + seconds
    while time.time() < end:
        data = s.read(4096)
        if data:
            sys.stdout.write(data.decode("utf-8", "replace"))
            sys.stdout.flush()


def send(s: serial.Serial, line: str, wait: float) -> None:
    s.write((line + "\n").encode("utf-8"))
    s.flush()
    pump(s, wait)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", default=DEFAULT_PORT)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("monitor", help="print device output")
    p.add_argument("--seconds", type=float, default=0, help="0 = until Ctrl+C")

    p = sub.add_parser("wifi", help="save Wi-Fi and join it")
    p.add_argument("--ssid", help="SSID (prompted when omitted)")
    p.add_argument("--wait", type=float, default=25)

    p = sub.add_parser("token", help="save a WaniKani token and probe /user")
    p.add_argument("--wait", type=float, default=25)

    p = sub.add_parser("cmd", help="send a raw development command")
    p.add_argument("command")
    p.add_argument("--wait", type=float, default=3)

    args = ap.parse_args()
    with open_port(args.port) as s:
        if args.cmd == "monitor":
            seconds = args.seconds if args.seconds > 0 else 10**9
            try:
                pump(s, seconds)
            except KeyboardInterrupt:
                pass
            return 0

        if args.cmd == "wifi":
            ssid = args.ssid or input("SSID: ").strip()
            password = getpass.getpass("Wi-Fi password: ")
            if "|" in ssid or "|" in password:
                print("SSID and password must not contain '|'", file=sys.stderr)
                return 2
            print(f"[host] sending Wi-Fi credentials for {ssid!r} (password hidden)")
            send(s, f"w {ssid}|{password}", args.wait)
            return 0

        if args.cmd == "token":
            token = getpass.getpass("WaniKani API token: ").strip()
            if not token:
                print("empty token", file=sys.stderr)
                return 2
            print(f"[host] sending token ({len(token)} chars, hidden)")
            send(s, f"k {token}", args.wait)
            return 0

        if args.cmd == "cmd":
            send(s, args.command, args.wait)
            return 0

    return 1


if __name__ == "__main__":
    raise SystemExit(main())

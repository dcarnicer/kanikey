#!/usr/bin/env python3
r"""Run a short command sequence on one open serial session.

Opening the Cardputer's USB Serial/JTAG port resets the board, so separate
`device.py cmd` calls cannot keep UI state. This helper opens the port once,
waits for a boot marker, sends each command with a pause in between and prints
the device log.

    uv run --with pyserial python3 tools/session.py --wait 4 "T" "C"

Commands are sent verbatim followed by a newline. Use `\n`, `\e`, `\t` and
`\b` inside a command for Enter, Esc, Tab and Backspace (the firmware's `K`
command understands them).
"""
from __future__ import annotations

import argparse
import sys
import time

import serial

DEFAULT_PORT = "/dev/cu.usbmodem1101"


def pump(s: serial.Serial, seconds: float) -> None:
    end = time.time() + seconds
    while time.time() < end:
        data = s.read(4096)
        if data:
            sys.stdout.write(data.decode("utf-8", "replace"))
            sys.stdout.flush()


def wait_for(s: serial.Serial, needle: str, seconds: float) -> bool:
    marker = needle.encode()
    buf = b""
    end = time.time() + seconds
    while time.time() < end:
        data = s.read(4096)
        if data:
            buf += data
            sys.stdout.write(data.decode("utf-8", "replace"))
            sys.stdout.flush()
            if marker in buf:
                return True
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", default=DEFAULT_PORT)
    ap.add_argument("--marker", default="[kk] first draw done",
                    help="wait for this log line before the first command")
    ap.add_argument("--wait", type=float, default=30.0,
                    help="seconds to wait for the marker (0 = do not wait)")
    ap.add_argument("--between", type=float, default=1.0,
                    help="seconds to pump after each command")
    ap.add_argument("--hold", type=float, default=0.0,
                    help="seconds to keep the session open after the last command")
    ap.add_argument("commands", nargs="+")
    args = ap.parse_args()

    with serial.Serial(args.port, 115200, timeout=0.3) as s:
        s.dtr = False
        s.rts = False
        if args.wait > 0 and args.marker:
            if not wait_for(s, args.marker, args.wait):
                print(f"[host] marker not seen: {args.marker}", file=sys.stderr)
                return 2
        else:
            pump(s, 1.0)
        for command in args.commands:
            s.write((command + "\n").encode("utf-8"))
            s.flush()
            print(f"\n[host] >>> {command}")
            pump(s, args.between)
        if args.hold > 0:
            print(f"\n[host] holding {args.hold:.0f}s")
            pump(s, args.hold)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

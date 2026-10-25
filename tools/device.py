#!/usr/bin/env python3
"""Small serial helper for KaniKey development.

The ESP32-S3 USB Serial/JTAG needs DTR and RTS deasserted to deliver data; a
plain `pio device monitor` can leave the port silent. This script opens the
port correctly, prints device output and can send the development commands.

    uv run --with pyserial python3 tools/device.py monitor
    uv run --with pyserial python3 tools/device.py wifi
    uv run --with pyserial python3 tools/device.py token
    uv run --with pyserial python3 tools/device.py cmd t
    uv run --with pyserial python3 tools/device.py upload dist/glyphs.kgp /kanikey/glyphs.kgp

The Wi-Fi password and the WaniKani token are read with getpass, sent to the
device and never written to disk or printed back.
"""
from __future__ import annotations

import argparse
import getpass
import re
import subprocess
import sys
import time
from pathlib import Path

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


def write_all(s: serial.Serial, data: bytes) -> None:
    """pyserial may write only part of a large buffer; keep going until done."""
    view = memoryview(data)
    while len(view):
        n = s.write(view)
        if not n:
            time.sleep(0.005)
            continue
        view = view[n:]


def upload(s: serial.Serial, local: str, remote: str, timeout: float) -> int:
    data = Path(local).read_bytes()
    if not data:
        print("[host] empty file", file=sys.stderr)
        return 2
    if not remote.startswith("/") or ".." in remote or len(remote) > 60:
        print("[host] remote path must be absolute, short and without '..'", file=sys.stderr)
        return 2
    print(f"[host] {len(data)} bytes -> {remote}")
    # Opening the port resets the board; wait until SD is up before queueing the
    # upload command. The command stays in the USB buffer while setup finishes.
    if not wait_for(s, "[sd] mounted", 45):
        print("[host] no SD mount log (card missing?)", file=sys.stderr)
        return 3
    s.write(f"up {len(data)} {remote}\n".encode())
    s.flush()
    if not wait_for(s, "[up] ready", 90):
        print("[host] device did not acknowledge the upload", file=sys.stderr)
        return 4
    sent = 0
    chunk = 8192
    while sent < len(data):
        part = data[sent : sent + chunk]
        write_all(s, part)
        s.flush()
        sent += len(part)
        if not wait_for(s, f"[up] ack {sent}", 30):
            print(f"[host] no ack for offset {sent}", file=sys.stderr)
            return 5
        if sent == len(data) or sent % (chunk * 16) == 0:
            print(f"[host] sent {sent}/{len(data)}", flush=True)
    # The final ack is printed after the last write; "done" usually arrives in
    # the same read, so just drain briefly instead of requiring a second match.
    pump(s, 0.5)
    return 0


def push(s: serial.Serial, local: str, remote: str, timeout: float) -> int:
    data = Path(local)
    if not data.is_file():
        print(f"[host] no such file: {data}", file=sys.stderr)
        return 2
    if not remote.startswith("/kanikey/") or ".." in remote or len(remote) > 60:
        print("[host] remote must be under /kanikey/", file=sys.stderr)
        return 2
    print(f"[host] {data.stat().st_size} bytes -> {remote} over Wi-Fi")
    if not wait_for(s, "home drawn", 45):
        s.write(b"r\n")
        s.flush()
        if not wait_for(s, "home drawn", 45):
            print("[host] device did not reach Home", file=sys.stderr)
            return 3
    s.write(b"S\n")
    s.flush()
    buf = b""
    ip = None
    end = time.time() + 60
    while time.time() < end:
        chunk = s.read(4096)
        if not chunk:
            continue
        buf += chunk
        sys.stdout.write(chunk.decode("utf-8", "replace"))
        sys.stdout.flush()
        m = re.search(rb"\[asset\] ready http://([0-9.]+):8080", buf)
        if m:
            ip = m.group(1).decode()
            break
    if not ip:
        print("[host] asset server did not start", file=sys.stderr)
        return 4
    url = f"http://{ip}:8080{remote}"
    print(f"[host] curl -T {data} {url}")
    result = subprocess.run(
        ["curl", "--fail", "--silent", "--show-error", "-H", "Expect:", "-T", str(data), url],
        timeout=timeout,
    )
    if result.returncode != 0:
        print(f"[host] curl failed with {result.returncode}", file=sys.stderr)
        return 5
    if not wait_for(s, "[asset] done", 20):
        print("[host] no device confirmation", file=sys.stderr)
        return 6
    return 0


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

    p = sub.add_parser("upload", help="write a local file to the microSD card over USB")
    p.add_argument("local")
    p.add_argument("remote")
    p.add_argument("--timeout", type=float, default=180)

    p = sub.add_parser("push", help="write a local file to the microSD card over Wi-Fi")
    p.add_argument("local")
    p.add_argument("remote")
    p.add_argument("--timeout", type=float, default=300)

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

        if args.cmd == "upload":
            return upload(s, args.local, args.remote, args.timeout)

        if args.cmd == "push":
            return push(s, args.local, args.remote, args.timeout)

    return 1


if __name__ == "__main__":
    raise SystemExit(main())

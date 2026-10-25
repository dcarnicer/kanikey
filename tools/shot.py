#!/usr/bin/env python3
"""Capture the KaniKey canvas over USB as a PNG.

The firmware's `p` command dumps the full 240x135 RGB565 canvas using the same
`#shot` / `#r` lines as Tangocho's screenshot remote. This script opens the
port, waits for the Home screen (the board may reset when the port opens),
sends `p`, parses the dump and writes a PNG.

    uv run --with pyserial --with pillow python3 tools/shot.py
    uv run --with pyserial --with pillow python3 tools/shot.py --out /tmp/kk.png
"""
from __future__ import annotations

import argparse
import re
import sys
import time
from pathlib import Path

import serial
from PIL import Image

DEFAULT_PORT = "/dev/cu.usbmodem1101"
SHOT_RE = re.compile(r"#shot (\d+) (\d+)")
ROW_RE = re.compile(r"#r (\d+) ([0-9a-f]+)$")


def rgb565_table() -> list[bytes]:
    out = []
    for v in range(65536):
        r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
        out.append(bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))))
    return out


def parse_shot(text: str, table: list[bytes]) -> Image.Image:
    width = height = 0
    rows: dict[int, str] = {}
    for line in text.splitlines():
        if line.startswith("#shot "):
            m = SHOT_RE.match(line)
            if m:
                width, height = int(m.group(1)), int(m.group(2))
                rows.clear()
        elif line.startswith("#r "):
            m = ROW_RE.match(line)
            if m:
                rows[int(m.group(1))] = m.group(2)
    if not width or not height or len(rows) != height:
        raise RuntimeError(f"incomplete shot: {len(rows)}/{height} rows, {width}x{height}")
    pixels = bytearray()
    for y in range(height):
        raw = bytes.fromhex(rows[y])
        pixels += b"".join(
            table[(raw[i] << 8) | raw[i + 1]] for i in range(0, 2 * width, 2)
        )
    return Image.frombytes("RGB", (width, height), bytes(pixels))


def capture(port: str, timeout: float = 60.0) -> Image.Image:
    table = rgb565_table()
    with serial.Serial(port, 115200, timeout=0.3) as s:
        s.dtr = False
        s.rts = False
        started = time.time()
        sent = False
        log = b""
        shot_buf = b""
        while time.time() - started < timeout:
            chunk = s.read(4096)
            if chunk:
                log += chunk
                if not sent:
                    text = log.decode("utf-8", "replace")
                    if "home drawn" in text or "alive heap" in text:
                        time.sleep(0.5)
                        s.write(b"p\n")
                        s.flush()
                        sent = True
                        shot_buf = b""
                else:
                    shot_buf += chunk
                # Device log lines (also the lines before the dump) to stderr.
                if b"#shot" not in chunk:
                    for line in chunk.decode("utf-8", "replace").splitlines():
                        if line.strip():
                            print(f"| {line}", file=sys.stderr)
            if sent and b"#end\n" in shot_buf:
                text = shot_buf.decode("ascii", "replace")
                return parse_shot(text, table)
            if not sent and time.time() - started > 30:
                s.write(b"p\n")
                s.flush()
                sent = True
                shot_buf = b""
        raise TimeoutError("no complete #shot before the timeout")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", default=DEFAULT_PORT)
    ap.add_argument("--out", type=Path, default=Path("tools/screenshots/out/home.png"))
    ap.add_argument("--scale", type=int, default=4, help="also save NAME@Nx.png")
    args = ap.parse_args()

    img = capture(args.port)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    img.save(args.out)
    print(args.out)
    if args.scale > 1:
        big = args.out.with_name(f"{args.out.stem}@{args.scale}x.png")
        img.resize((img.width * args.scale, img.height * args.scale), Image.Resampling.NEAREST).save(big)
        print(big)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

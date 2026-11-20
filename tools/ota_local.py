#!/usr/bin/env python3
"""Serve one firmware image and a release manifest over plain HTTP (dev only).

It stands in for the GitHub release so the parent can install a local image
with the dev console:

  U http://<mac-lan-ip>:<port>/kanikey-firmware.txt
  F                      check: prints the offer
  I                      install into the next slot, then reset

A dev build accepts http:// for the manifest and the image (Ota.cpp,
validHttpUrl()). A release build does not, and the OTA client has no
self-signed path, so this tool serves plain HTTP only.

The manifest is the text format in src/ota/Manifest.h (not JSON). Its app
record names this env (cardputer-adv) and the image's exact size and SHA-256,
so the device checks both while it writes. The version must be above the
running one (0.1.0 at the moment) and must match the version string compiled
into the image; the tool checks that before serving.

Usage:
  python3 tools/ota_local.py --bin firmware/.pio/build/cardputer-adv-badapi/firmware.bin \
      --version v0.1.2 --port 8080

Files are copied into a fresh temporary directory, so nothing in the repo changes.
Stdlib only. The printed URL is what to type on the device.
"""
import argparse
import hashlib
import http.server
import os
import shutil
import socket
import sys
import tempfile
import functools

MANIFEST_NAME = "kanikey-firmware.txt"
IMAGE_NAME = "firmware.bin"
ESP_IMAGE_MAGIC = 0xE9


def lan_ip(override=None):
    if override:
        return override
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(("10.255.255.255", 1))  # sends nothing; picks the route's source IP
        return probe.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        probe.close()


def check_image(path, version):
    with open(path, "rb") as f:
        data = f.read()
    if not data:
        sys.exit("empty image: %s" % path)
    if data[0] != ESP_IMAGE_MAGIC:
        sys.exit("not an ESP32 app image (first byte %#x): %s" % (data[0], path))
    bare = version[1:] if version.startswith("v") else version
    if bare.encode("ascii") not in data:
        sys.exit(
            "version %s is not in the image: build the env that matches --version "
            "(Version.h) or pass the right --version" % bare
        )
    return data


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    default_bin = os.path.join(here, "..", "firmware", ".pio", "build", "cardputer-adv-badapi", "firmware.bin")
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--bin", default=default_bin, help="app image (firmware.bin)")
    parser.add_argument("--version", default="v0.1.2", help="manifest version, vX.Y.Z; must match the image")
    parser.add_argument("--env", default="cardputer-adv", help="KANIKEY_ENV the image was built for")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--ip", help="LAN IP to print (default: auto)")
    args = parser.parse_args()

    if not args.version.lstrip("v").replace(".", "").isdigit():
        sys.exit("--version must look like v0.1.2")
    if "\t" in args.env or " " in args.env or not args.env:
        sys.exit("--env must be a single token")

    data = check_image(args.bin, args.version)
    digest = hashlib.sha256(data).hexdigest()
    size = len(data)

    serve_dir = tempfile.mkdtemp(prefix="kanikey-ota-")
    shutil.copyfile(args.bin, os.path.join(serve_dir, IMAGE_NAME))
    manifest = (
        "#kanikey-firmware\t1\n"
        "version\t%s\n"
        "app\t%s\t%s\t%d\t%s\n" % (args.version, args.env, IMAGE_NAME, size, digest)
    )
    with open(os.path.join(serve_dir, MANIFEST_NAME), "w", encoding="ascii", newline="\n") as f:
        f.write(manifest)

    ip = lan_ip(args.ip)
    print("serving %s (%d bytes, sha256 %s)" % (IMAGE_NAME, size, digest), flush=True)
    print("manifest:\n" + manifest, end="", flush=True)
    print("from %s on http://%s:%d/" % (serve_dir, ip, args.port), flush=True)
    print("device console:", flush=True)
    print("  U http://%s:%d/%s" % (ip, args.port, MANIFEST_NAME), flush=True)
    print("  F", flush=True)
    print("  I", flush=True)

    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=serve_dir)
    http.server.ThreadingHTTPServer.allow_reuse_address = True
    server = http.server.ThreadingHTTPServer((args.bind, args.port), handler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("stopped", flush=True)
    finally:
        server.server_close()
        shutil.rmtree(serve_dir, ignore_errors=True)


if __name__ == "__main__":
    main()

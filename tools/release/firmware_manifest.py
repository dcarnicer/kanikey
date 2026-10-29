#!/usr/bin/env python3
"""Build the KaniKey OTA manifest for a release.

The device reads `kanikey-firmware.txt` from the latest GitHub release:

    #kanikey-firmware<TAB>1
    version<TAB>v0.1.0
    app<TAB>cardputer-adv<TAB>kanikey-cardputer-adv.bin<TAB>1460581<TAB><sha256>

The image must fit the OTA slot from the partition CSV. `--check-version`
also refuses a tag that does not match `firmware/src/Version.h`.

    python3 tools/release/firmware_manifest.py \
        --version v0.1.0 \
        --image dist/kanikey-cardputer-adv.bin \
        --out dist/kanikey-firmware.txt \
        --check-version firmware/src/Version.h
"""
from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path

HEADER = "#kanikey-firmware"
FORMAT = 1
VERSION_RE = re.compile(r"^v(\d+)\.(\d+)\.(\d+)$")
ENV_RE = re.compile(r"^[a-z0-9-]{1,31}$")


class ManifestError(ValueError):
    pass


def parse_slot_size(partitions: Path) -> int:
    """The app slot size from the first `ota_0`/`ota_1` row; both must match."""
    rows = []
    for raw in partitions.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [field.strip() for field in line.split(",")]
        if len(fields) < 5:
            continue
        name, kind, subtype, _offset, size = fields[:5]
        if kind != "app" or subtype not in ("ota_0", "ota_1"):
            continue
        rows.append((name, subtype, int(size, 0)))
    if len(rows) < 2:
        raise ManifestError("partition table has no two OTA app slots")
    sizes = {row[2] for row in rows}
    if len(sizes) != 1:
        raise ManifestError("the two OTA slots have different sizes")
    return rows[0][2]


def version_from_header(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    match = re.search(r'kFirmwareVersion\s*=\s*"([^"]+)"', text)
    if not match:
        raise ManifestError(f"no kFirmwareVersion in {path}")
    return match.group(1)


def validate_version(version: str) -> tuple[int, int, int]:
    match = VERSION_RE.match(version)
    if not match:
        raise ManifestError("version must be vMAJOR.MINOR.PATCH")
    return tuple(int(part) for part in match.groups())


def build_manifest(
    *,
    version: str,
    env: str,
    image: Path,
    partition_table: Path,
    check_version: Path | None = None,
    asset_name: str | None = None,
) -> str:
    if not ENV_RE.match(env):
        raise ManifestError("environment must match [a-z0-9-]{1,31}")
    validate_version(version)
    if check_version is not None:
        header = version_from_header(check_version)
        if header != version[1:]:
            raise ManifestError(
                f"tag {version} does not match Version.h {header}; bump it first"
            )
    if not image.is_file():
        raise ManifestError(f"no image: {image}")
    data = image.read_bytes()
    size = len(data)
    if size == 0:
        raise ManifestError("image is empty")
    slot = parse_slot_size(partition_table)
    if size > slot:
        raise ManifestError(
            f"image is {size} bytes, larger than the {slot}-byte OTA slot"
        )
    if data[0] != 0xE9:
        raise ManifestError("not an ESP app image (magic byte 0xE9)")
    digest = hashlib.sha256(data).hexdigest()
    name = asset_name or image.name
    if not name or "/" in name or "\\" in name or ".." in name:
        raise ManifestError("asset name must be a plain file name")
    return (
        f"{HEADER}\t{FORMAT}\n"
        f"version\t{version}\n"
        f"app\t{env}\t{name}\t{size}\t{digest}\n"
    )


def main(argv: list[str] | None = None) -> int:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True, help="release tag, e.g. v0.1.0")
    parser.add_argument("--env", default="cardputer-adv")
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument(
        "--partitions",
        type=Path,
        default=root / "firmware" / "partitions" / "kanikey_8MB.csv",
    )
    parser.add_argument("--check-version", type=Path)
    parser.add_argument("--asset-name")
    args = parser.parse_args(argv)

    try:
        manifest = build_manifest(
            version=args.version,
            env=args.env,
            image=args.image,
            partition_table=args.partitions,
            check_version=args.check_version,
            asset_name=args.asset_name,
        )
    except (ManifestError, OSError) as exc:
        print(f"firmware_manifest: {exc}", file=sys.stderr)
        return 1
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(manifest, encoding="utf-8")
    print(f"wrote {args.out} ({len(manifest)} bytes)")
    print(manifest, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Tests for the release manifest builder. Run with:

    python3 tools/release/test_firmware_manifest.py
"""
from __future__ import annotations

import hashlib
import tempfile
import unittest
from pathlib import Path

from firmware_manifest import ManifestError, build_manifest, parse_slot_size

ROOT = Path(__file__).resolve().parents[2]
PARTITIONS = ROOT / "firmware" / "partitions" / "kanikey_8MB.csv"
VERSION_H = ROOT / "firmware" / "src" / "Version.h"


def app_image(size: int = 1024) -> bytes:
    return bytes([0xE9]) + bytes(size - 1)


class FirmwareManifestTest(unittest.TestCase):
    def test_slot_size_from_partition_table(self) -> None:
        self.assertEqual(parse_slot_size(PARTITIONS), 0x1C0000)

    def test_manifest_has_version_size_and_hash(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / "firmware.bin"
            data = app_image(2048)
            image.write_bytes(data)
            text = build_manifest(
                version="v0.1.0",
                env="cardputer-adv",
                image=image,
                partition_table=PARTITIONS,
                check_version=VERSION_H,
            )
        self.assertIn("#kanikey-firmware\t1\n", text)
        self.assertIn("version\tv0.1.0\n", text)
        self.assertIn("app\tcardputer-adv\tfirmware.bin\t2048\t", text)
        self.assertIn(hashlib.sha256(data).hexdigest(), text)

    def test_version_must_match_header(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / "firmware.bin"
            image.write_bytes(app_image())
            with self.assertRaises(ManifestError):
                build_manifest(
                    version="v0.2.0",
                    env="cardputer-adv",
                    image=image,
                    partition_table=PARTITIONS,
                    check_version=VERSION_H,
                )

    def test_rejects_bad_version_env_and_empty_image(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / "firmware.bin"
            image.write_bytes(app_image())
            for version in ("0.1.0", "v1.2", "v1.2.3-rc1", "v1.2.3.4"):
                with self.assertRaises(ManifestError):
                    build_manifest(
                        version=version,
                        env="cardputer-adv",
                        image=image,
                        partition_table=PARTITIONS,
                    )
            with self.assertRaises(ManifestError):
                build_manifest(
                    version="v0.1.0",
                    env="Cardputer ADV",
                    image=image,
                    partition_table=PARTITIONS,
                )
            empty = Path(tmp) / "empty.bin"
            empty.write_bytes(b"")
            with self.assertRaises(ManifestError):
                build_manifest(
                    version="v0.1.0",
                    env="cardputer-adv",
                    image=empty,
                    partition_table=PARTITIONS,
                )

    def test_rejects_image_larger_than_slot(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / "firmware.bin"
            image.write_bytes(app_image(0x1C0001))
            with self.assertRaises(ManifestError):
                build_manifest(
                    version="v0.1.0",
                    env="cardputer-adv",
                    image=image,
                    partition_table=PARTITIONS,
                )

    def test_rejects_non_esp_image(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / "firmware.bin"
            image.write_bytes(b"not an app")
            with self.assertRaises(ManifestError):
                build_manifest(
                    version="v0.1.0",
                    env="cardputer-adv",
                    image=image,
                    partition_table=PARTITIONS,
                )


if __name__ == "__main__":
    unittest.main()

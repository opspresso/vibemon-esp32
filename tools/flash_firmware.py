#!/usr/bin/env python3
"""Upload verified individual images, preserving the device's NVS configuration."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def verify_images(directory):
    manifest = json.loads((directory / "manifest.json").read_text())
    if manifest.get("board") != "ESP32-4848S040":
        raise ValueError("Use an ESP32-4848S040 build")
    expected = {"vibemon-esp32.ino.bootloader.bin": 0, "vibemon-esp32.ino.partitions.bin": 0x8000, "boot_app0.bin": 0xe000, "vibemon-esp32.ino.bin": 0x10000}
    if set(manifest.get("images", {})) != set(expected):
        raise ValueError("The build must contain all four upload images")
    ranges = []
    for filename, offset in expected.items():
        image = manifest["images"][filename]
        data = (directory / filename).read_bytes()
        if image["offset"] != offset or image["size"] != len(data) or image["sha256"] != hashlib.sha256(data).hexdigest():
            raise ValueError("Build image failed integrity verification: " + filename)
        if not data or (offset < 0xe000 and offset + len(data) > 0x9000):
            raise ValueError("Build image overlaps NVS or is empty")
        ranges.append((offset, offset + len(data)))
    ranges.sort()
    if any(previous[1] > following[0] for previous, following in zip(ranges, ranges[1:])) or ranges[-1][1] > 0x810000:
        raise ValueError("Upload image ranges overlap or exceed the application partition")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--arduino-cli", default=shutil.which("arduino-cli"))
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()
    if not args.arduino_cli:
        parser.error("Install arduino-cli, or provide its path with --arduino-cli")
    if args.baud not in (115200, 230400, 460800, 921600):
        parser.error("Choose a supported upload baud rate")
    directory = ROOT / "build/firmware"
    manifest = verify_images(directory)
    platforms = json.loads(subprocess.check_output([args.arduino_cli, "core", "list", "--format", "json"], text=True))["platforms"]
    if not any(platform["id"] == "esp32:esp32" and platform.get("installed_version") == manifest["toolchain"]["esp32_core"] for platform in platforms):
        parser.error("Install the ESP32 core recorded in the build manifest")
    fqbn = manifest["fqbn"] + ",UploadSpeed=" + str(args.baud)
    subprocess.run([args.arduino_cli, "--config-file", str(ROOT / "build/arduino-cli.yaml"), "upload", "--port", args.port,
                    "--fqbn", fqbn, "--input-dir", str(directory)], check=True)


if __name__ == "__main__":
    main()

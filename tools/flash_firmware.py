#!/usr/bin/env python3
"""Upload verified individual images, preserving the device's NVS configuration."""
import argparse
import hashlib
import json
from pathlib import Path
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
    if any(previous[1] > following[0] for previous, following in zip(ranges, ranges[1:])) or ranges[-1][1] > 0x710000:
        raise ValueError("Upload image ranges overlap or exceed the application partition")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()
    if args.baud not in (115200, 230400, 460800, 921600):
        parser.error("Choose a supported upload baud rate")
    directory = ROOT / "build/firmware"
    manifest = verify_images(directory)
    if manifest.get("framework") != "esp-idf":
        parser.error("Rebuild with the RGB-safe ESP-IDF toolchain")
    from sdk_toolchain import load
    environment, python = load(manifest["toolchain"])
    command = [python, "-m", "esptool", "--chip", "esp32s3", "--port", args.port, "--baud", str(args.baud),
               "write_flash", "--flash_mode", "dio", "--flash_size", "16MB", "--flash_freq", "80m"]
    for filename, image in sorted(manifest["images"].items(), key=lambda pair: pair[1]["offset"]):
        command.extend([hex(image["offset"]), str(directory / filename)])
    subprocess.run(command, env=environment, check=True)


if __name__ == "__main__":
    main()

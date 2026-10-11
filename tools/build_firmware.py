#!/usr/bin/env python3
"""Compile the exact ESP32-4848S040 profile; never upload or erase a device."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
FQBN = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,FlashMode=qio,CDCOnBoot=default"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arduino-cli", default=shutil.which("arduino-cli"))
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if not args.arduino_cli:
        parser.error("Install arduino-cli, or provide its path with --arduino-cli")
    lock = json.loads((ROOT / "toolchain.lock.json").read_text())
    cli_version = json.loads(subprocess.check_output([args.arduino_cli, "version", "--format", "json"], text=True))["VersionString"]
    if cli_version != lock["arduino_cli"]:
        parser.error("Use arduino-cli " + lock["arduino_cli"])
    platforms = json.loads(subprocess.check_output([args.arduino_cli, "core", "list", "--format", "json"], text=True))["platforms"]
    if not any(platform["id"] == "esp32:esp32" and platform.get("installed_version") == lock["esp32_core"] for platform in platforms):
        parser.error("Install esp32:esp32@" + lock["esp32_core"] + " with tools/setup.py --install-core")
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    libraries = ROOT / "build/arduino/libraries"
    for name, version in lock["libraries"].items():
        properties = libraries / name / "library.properties"
        if not properties.exists() or ("version=" + version) not in properties.read_text().splitlines():
            parser.error("Run tools/setup.py to install " + name + "@" + version)
    subprocess.run([sys.executable, str(ROOT / "tools/generate_assets.py")], check=True)
    common = " ".join([
        '"-I' + str(ROOT) + '"', '"-I' + str(ROOT / "build/generated") + '"',
        "-DLV_CONF_INCLUDE_SIMPLE=1", "-DLV_KCONFIG_IGNORE=1",
        "-DESP_PANEL_BOARD_FILE_SKIP=1", "-DESP_PANEL_BOARD_DEFAULT_USE_SUPPORTED=1",
        "-DBOARD_JINGCAI_ESP32_4848S040C_I_Y_3=1",
    ])
    output = ROOT / "build/firmware"
    command = [args.arduino_cli, "--config-file", str(ROOT / "build/arduino-cli.yaml"), "compile",
               "--fqbn", FQBN, "--warnings", "all", "--jobs", str(args.jobs), "--build-path", str(ROOT / "build/firmware-obj"),
               "--output-dir", str(output), "--build-property", "upload.maximum_size=8388608",
               "--build-property", "compiler.cpp.extra_flags=" + common,
               "--build-property", "compiler.c.extra_flags=" + common, str(ROOT)]
    subprocess.run(command, check=True)
    binary = output / "vibemon-esp32.ino.bin"
    if not binary.exists() or binary.stat().st_size > 8 * 1024 * 1024:
        raise ValueError("Firmware is missing or exceeds the 8 MB application partition")
    shutil.copyfile(ROOT / "build/firmware-obj/boot_app0.bin", output / "boot_app0.bin")
    shutil.copyfile(ROOT / "LICENSE", output / "LICENSE")
    shutil.copyfile(ROOT / "assets/OFL-NanumGothic.txt", output / "OFL-NanumGothic.txt")
    images = {}
    for offset, filename in [(0, "vibemon-esp32.ino.bootloader.bin"), (0x8000, "vibemon-esp32.ino.partitions.bin"), (0xe000, "boot_app0.bin"), (0x10000, binary.name)]:
        data = (output / filename).read_bytes()
        if offset < 0xe000 and offset + len(data) > 0x9000:
            raise ValueError("An upload image overlaps the NVS settings partition")
        images[filename] = {"offset": offset, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip())
    manifest = {"board": "ESP32-4848S040", "fqbn": FQBN, "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(), "dirty": dirty,
                "sha256": hashlib.sha256(binary.read_bytes()).hexdigest(), "firmwareBytes": binary.stat().st_size, "images": images, "toolchain": lock}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("Built ESP32-4848S040 firmware:", binary.stat().st_size, "bytes")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Install pinned firmware libraries in build/ without changing sketchbook libraries."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arduino-cli", default=shutil.which("arduino-cli"))
    parser.add_argument("--install-core", action="store_true", help="Install the pinned ESP32 core in Arduino's data directory")
    args = parser.parse_args()
    if not args.arduino_cli:
        parser.error("Install arduino-cli, or provide its path with --arduino-cli")
    lock = json.loads((ROOT / "toolchain.lock.json").read_text())
    version = json.loads(subprocess.check_output([args.arduino_cli, "version", "--format", "json"], text=True))
    if version["VersionString"] != lock["arduino_cli"]:
        parser.error("Use arduino-cli " + lock["arduino_cli"])
    config = ROOT / "build/arduino-cli.yaml"
    config.parent.mkdir(exist_ok=True)
    config.write_text("directories:\n  user: " + json.dumps(str(ROOT / "build/arduino")) + "\n")
    cli = [args.arduino_cli, "--config-file", str(config)]
    if args.install_core:
        subprocess.run(cli + ["core", "update-index", "--additional-urls", "https://espressif.github.io/arduino-esp32/package_esp32_index.json"], check=True)
        subprocess.run(cli + ["core", "install", "esp32:esp32@" + lock["esp32_core"], "--additional-urls", "https://espressif.github.io/arduino-esp32/package_esp32_index.json"], check=True)
    subprocess.run(cli + ["lib", "update-index"], check=True)
    for name, release in lock["libraries"].items():
        subprocess.run(cli + ["lib", "install", "--no-deps", name + "@" + release], check=True)
    print("Pinned libraries installed in build/arduino/libraries")


if __name__ == "__main__":
    main()

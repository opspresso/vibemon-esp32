#!/usr/bin/env python3
"""Install pinned firmware libraries in build/ without changing sketchbook libraries."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arduino-cli", default=shutil.which("arduino-cli"))
    parser.add_argument("--firmware", action="store_true", help="Install the pinned Arduino sources and ESP-IDF build tools")
    parser.add_argument("--idf-python", default=sys.executable, help="Python used to create the isolated ESP-IDF environment")
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
    if args.firmware:
        subprocess.run(cli + ["core", "update-index", "--additional-urls", "https://espressif.github.io/arduino-esp32/package_esp32_index.json"], check=True)
        subprocess.run(cli + ["core", "install", "esp32:esp32@" + lock["esp32_core"], "--additional-urls", "https://espressif.github.io/arduino-esp32/package_esp32_index.json"], check=True)
    subprocess.run(cli + ["lib", "update-index"], check=True)
    for name, release in lock["libraries"].items():
        subprocess.run(cli + ["lib", "install", "--no-deps", name + "@" + release], check=True)
    print("Pinned libraries installed in build/arduino/libraries")
    if args.firmware:
        from sdk_toolchain import install
        details = json.loads(subprocess.check_output(cli + ["board", "details", "--fqbn", "esp32:esp32:esp32s3", "--format", "json"], text=True))
        properties = dict(value.split("=", 1) for value in details["build_properties"] if "=" in value)
        install(lock, Path(properties["runtime.platform.path"]), Path(properties["runtime.tools.esp32s3-libs.path"]), args.idf_python)


if __name__ == "__main__":
    main()

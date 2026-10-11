#!/usr/bin/env python3
"""Run the native sanitizer tests, USB framing checks and real LVGL screen renders."""
from pathlib import Path
import subprocess
import sys

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent


def main():
    subprocess.run([sys.executable, str(ROOT / "tools/generate_assets.py")], check=True)
    subprocess.run(["cmake", "-S", str(ROOT), "-B", str(ROOT / "build/native"), "-G", "Ninja"], check=True)
    subprocess.run(["cmake", "--build", str(ROOT / "build/native"), "-j", "4"], check=True)
    subprocess.run(["ctest", "--test-dir", str(ROOT / "build/native"), "--output-on-failure"], check=True)
    subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", "tests", "-p", "*_test.py"], cwd=ROOT, check=True)
    for image in (ROOT / "build/native/screenshots").glob("*.ppm"):
        Image.open(image).save(image.with_suffix(".png"))
    print("PASS native validation; rendered screens are in build/native/screenshots/")


if __name__ == "__main__":
    main()

"""Describe a firmware build for devices that install updates over Wi-Fi."""
import re

BOARD = "ESP32-4848S040"
DOWNLOAD = "https://github.com/opspresso/vibemon-esp32/releases/download/"
APPLICATION = "vibemon-esp32.ino.bin"
# One application slot in partitions.csv.
SLOT_BYTES = 0x700000


def update_description(version, image):
    """Return ota.json for a release tagged v<version>.

    `image` is the manifest entry of the application image: its size and SHA-256.
    """
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("Firmware version must be MAJOR.MINOR.PATCH: " + version)
    if not 0 < image["size"] <= SLOT_BYTES:
        raise ValueError("The application image does not fit one update slot")
    return {"board": BOARD, "version": version, "url": DOWNLOAD + "v" + version + "/" + APPLICATION,
            "size": image["size"], "sha256": image["sha256"]}

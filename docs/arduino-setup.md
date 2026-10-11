# Build, upload and connect

## Prepare the toolchain

Use the versions in [toolchain.lock.json](../toolchain.lock.json). The build script verifies Arduino CLI, the ESP32 core and library versions. Libraries are installed under `build/arduino/libraries`; existing sketchbook libraries are not changed.

```sh
python3 -m venv build/venv
build/venv/bin/python -m pip install -r requirements-dev.txt
python3 tools/setup.py --install-core
build/venv/bin/python tools/check.py
build/venv/bin/python tools/build_firmware.py
```

`--install-core` installs the pinned ESP32 core in Arduino's data directory. Omit it when that version is already installed. On macOS, an Arduino IDE installation may provide the CLI at:

```text
/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli
```

Pass that path with `--arduino-cli`. On Windows use `build\venv\Scripts\python.exe` for the virtual environment. The supported build path is the CLI script; it supplies the shared LVGL configuration, verified assets and the exact board profile.

## Upload to ESP32-4848S040

1. Connect the board with a USB **data** cable.
2. Run `arduino-cli board list` and identify its serial port.
3. Preserve a factory backup before replacing existing firmware. Keep backups private: they may contain saved settings.
4. Upload the verified build:

```sh
build/venv/bin/python tools/flash_firmware.py --port /dev/cu.usbserial-110
```

Replace the example port with the discovered port, such as `COM5` on Windows. The default upload speed is 115200. A higher `--baud` is optional; use it only when sustained reads/writes are reliable on that cable and adapter.

The helper verifies all four images, their hashes, offsets and non-overlap with NVS. It uploads the individual bootloader, partition table, boot metadata and application images. **Do not flash the merged image when preserving settings**: it also contains blank data over the NVS area. The board profile enables 16 MB flash and 8 MB octal PSRAM; the application partition is 8 MB.

If automatic bootloader entry fails, use the board's documented BOOT/RESET sequence, then retry the same port. Do not change LCD pins or reduce display timing to work around a serial transport error.

## Enter Wi-Fi and account settings

On first boot the device shows nearby networks. Choose a 2.4 GHz network, enter its password and press **Connect Wi-Fi**. An open network uses an empty password. **Hidden network** accepts a manually entered SSID. A failed connection leaves the saved configuration unchanged and shows a recovery path.

Next enter the server origin, normally `https://vibemon.io`, and a token with read permission from [Web Account](https://vibemon.io/account). The token keypad provides digits, `a`–`f` and a `vm_` prefix key. The device verifies the connection before saving it. In connection settings, an empty key field retains a saved key only when the server is unchanged.

Tap a dashboard card to select its resource or account. **Leave empty** clears only that card. Settings and panel selections are saved together and survive ordinary firmware uploads and power cycles.

## Optional USB setup

USB setup invokes the same application checks as the touchscreen. Passwords and keys are prompted without echo and are not passed as process arguments.

```sh
build/venv/bin/python tools/device.py --port /dev/cu.usbserial-110 wifi
build/venv/bin/python tools/device.py --port /dev/cu.usbserial-110 cloud --origin https://vibemon.io
build/venv/bin/python tools/device.py --port /dev/cu.usbserial-110 info
```

Opening the USB port may reset a CH340 board. The helper waits for a ready response before submitting settings, so startup cannot silently discard the command. A queued response means verification is in progress. Inspect the screen or `info` for the final state. Close other serial monitors before using these commands.

## Recover a connection

| Screen state | Action |
| --- | --- |
| No networks | Scan again, or enter a hidden SSID. Check that the network supports 2.4 GHz. |
| Wi-Fi unavailable | Check the password and signal, then choose Wi-Fi in device settings. |
| Setting the clock / network time unavailable | Allow the network to reach an NTP server; HTTPS verification requires valid time. |
| Token rejected | Retrieve or replace the token in Web Account, then enter it again. |
| Read permission required | Use a read token. A write-only collector token cannot display the workspace. |
| Server unavailable | Check the origin and connectivity. The last readings remain visible. |
| Item unavailable | The selected source may have been removed. Tap the card to choose another; its selection is not silently replaced. |
| Settings could not be saved | Retry the explicit save. The device does not report persistence success when NVS fails. |

For a black screen or missing touch input, read the 115200-baud startup diagnostics and verify the exact [hardware profile](hardware.md). A successful upload alone does not prove LCD or touch operation.

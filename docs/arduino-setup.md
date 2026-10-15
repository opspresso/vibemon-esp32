# Build, upload and connect

## Prepare the toolchain

Use the versions in [toolchain.lock.json](../toolchain.lock.json). Setup verifies Arduino CLI and installs the pinned Arduino source package, libraries and ESP-IDF tools. The build script verifies the SDK revision, library versions and required RGB memory settings. Libraries are installed under `build/arduino/libraries`; existing sketchbook libraries are not changed.

```sh
python3 -m venv build/venv
build/venv/bin/python -m pip install -r requirements-dev.txt
python3 tools/setup.py --firmware
build/venv/bin/python tools/check.py
build/venv/bin/python tools/build_firmware.py
```

`--firmware` installs the pinned Arduino package in Arduino’s data directory and prepares ESP-IDF and its Python environment under `build/`. Omit it when installing only the native-test libraries. Existing Arduino source files are copied into `build/sdk-components`; they are not edited. On macOS, an Arduino IDE installation may provide the CLI at:

```text
/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli
```

Pass that path to setup with `--arduino-cli`. If needed, `--idf-python PATH` selects the Python used to create the isolated SDK environment. On Windows use `build\venv\Scripts\python.exe` for the virtual environment. Use `tools/build_firmware.py` to build. It supplies the shared LVGL configuration, verified assets and the exact board profile. It rebuilds ESP-IDF with the RGB cache and flash-write settings in `firmware/sdkconfig.defaults`. Arduino IDE/CLI compilation against its stock prebuilt SDK is rejected because those settings cannot be changed by sketch compiler flags.

## Upload to ESP32-4848S040

1. Connect the board with a USB **data** cable.
2. Run `arduino-cli board list` and identify its serial port.
3. Preserve a factory backup before replacing existing firmware. Keep backups private: they may contain saved settings.
4. Upload the verified build:

```sh
build/venv/bin/python tools/flash_firmware.py --port /dev/cu.usbserial-110
```

Replace the example port with the discovered port, such as `COM5` on Windows. The default upload speed is 115200. A higher `--baud` is optional; use it only when sustained reads/writes are reliable on that cable and adapter.

The helper verifies all four images, their hashes, offsets and non-overlap with NVS. It uploads the individual bootloader, partition table, boot metadata and application images. **Do not flash the merged image when preserving settings**: it also contains blank data over the NVS area. The board profile enables 16 MB flash and 8 MB octal PSRAM; each of the two application slots is 7 MB.

If automatic bootloader entry fails, use the board's documented BOOT/RESET sequence, then retry the same port. Do not change LCD pins or reduce display timing to work around a serial transport error.

## Update over Wi-Fi

After one USB upload of version 3.4.0 or later, the device installs newer releases by itself. See [Firmware updates](architecture.md#firmware-updates) for the schedule and checks. Device settings show the installed version and the update state; tap that row to check now. The device restarts after an install. Wi-Fi, server, key and panel settings stay in NVS and are kept.

Use USB again only when a release changes the partition table or bootloader, or when the device cannot reach Wi-Fi.

## Publish a release

1. Set `VIBEMON_VERSION` in `src/version.h` to the new `MAJOR.MINOR.PATCH` and merge it.
2. Create an annotated tag on that commit. Its message becomes the release notes: `git tag -a v3.4.1 -m "..."`, then `git push origin v3.4.1`.
3. The **Release firmware** workflow first runs **Verify firmware** (native checks and a firmware build), then builds the firmware, checks that the tag matches the built version and publishes the bundle, `SHA256SUMS`, `manifest.json`, `vibemon-esp32.ino.bin` and `ota.json`.

Devices install the release within 6 hours, or at their next restart. A tag that does not match `src/version.h` fails without publishing.

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
| Wi-Fi unavailable · retrying, or Offline · last readings | The device retries the saved network every 30 seconds and stays on the dashboard. If it does not reconnect, check the password and signal, then choose Wi-Fi in device settings. |
| Setting the clock / network time unavailable | Allow the network to reach an NTP server; HTTPS verification requires valid time. |
| Token rejected | Retrieve or replace the token in Web Account, then enter it again. |
| Read permission required | Use a read token. A write-only collector token cannot display the workspace. |
| Server unavailable | Check the origin and connectivity. The last readings remain visible. |
| Item unavailable | The selected source may have been removed. Tap the card to choose another; its selection is not silently replaced. |
| Settings could not be saved | Retry the explicit save. The device does not report persistence success when NVS fails. |

For a black screen or missing touch input, read the 115200-baud startup diagnostics and verify the exact [hardware profile](hardware.md). A successful upload alone does not prove LCD or touch operation.

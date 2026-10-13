# VibeMon Touch

A four-panel resource and coding-account monitor for the **GUITION ESP32-4848S040** touchscreen.

The 480×480 display follows your VibeMon workspace over HTTPS. Each card can show a resource or a coding account. Tap a card to change it. Wi-Fi, server address, read token and panel selections persist across restarts.

## Set up the device

1. Build and upload the firmware using the [setup guide](docs/arduino-setup.md).
2. On the screen, select a **2.4 GHz Wi-Fi** network and enter its password. Use **Hidden network** to enter a name manually.
3. Enter `https://vibemon.io` or your own HTTPS VibeMon origin.
4. Enter a token with **read** permission from [Account](https://vibemon.io/account). The device checks it before saving.
5. Tap any of the four cards and choose a resource or coding account. Use the category buttons to narrow the list.

The first successful non-empty response fills up to four cards, alternating resources and accounts. Your selections then remain fixed until you change them. Missing measurements display `—`; lost connections retain the last readings with an explicit connection status. Resources without recent reports become `Stale`. Coding accounts without recent events become neutral `Inactive`.

## What it supports

- Custom resources, Kubernetes clusters, DGX Spark and other host metrics.
- Multiple Codex, Claude, Kiro or other provider accounts, grouped by provider and account ID.
- Resource metric labels, units, ranges and order from Web. Each card shows the first three metrics.
- On-screen Wi-Fi scanning, password input and a large read-token keypad.
- Korean and Latin names, without downloading fonts at runtime.
- Optional USB setup for pasting a long key; secrets are prompted and never echoed.

## Develop and verify

Requirements: Arduino CLI **1.5.1**, ESP32 Arduino core **3.3.12**, ESP-IDF **5.5.5**, Python **3.10+**, CMake **3.22+**, Ninja, Git and a C++17 compiler. Library versions are in [toolchain.lock.json](toolchain.lock.json).

```sh
python3 -m venv build/venv
build/venv/bin/python -m pip install -r requirements-dev.txt
python3 tools/setup.py --firmware
build/venv/bin/python tools/check.py
build/venv/bin/python tools/build_firmware.py
```

On Windows, use `build\venv\Scripts\python.exe` in place of `build/venv/bin/python`. Pass `--arduino-cli PATH` to the setup command if the CLI is not on PATH. The firmware uses Arduino APIs inside a source-built ESP-IDF SDK; compiling with the Arduino IDE’s prebuilt SDK is not supported.

Native tests run the same LVGL screen code as the firmware. PNG renders appear in `build/native/screenshots/`. The compiled binary and its image manifest appear in `build/firmware/`. CI builds the firmware and uploads these artifacts.

## Migration

Version 3 targets the four-inch ESP32-S3 board. It replaces the C6 single-character sketch, local status-push endpoints, WebSocket client and shared-password captive portal with touch setup and authenticated cloud polling. Existing C6 devices must continue using the [previous firmware](https://github.com/opspresso/vibemon-esp32/tree/e6c272c); do not flash this build to them. Private legacy `credentials.h` files are ignored and are not read by the new firmware.

## Documentation

- [Build, upload and setup](docs/arduino-setup.md)
- [Board and pin assignments](docs/hardware.md)
- [Data, security and USB contracts](docs/api.md)
- [Architecture and verification](docs/architecture.md)
- [VibeMon Desktop](https://github.com/opspresso/vibemon-app) · [VibeMon Web](https://vibemon.io)

MIT. The bundled Nanum Gothic font has its own [SIL Open Font License](assets/OFL-NanumGothic.txt).

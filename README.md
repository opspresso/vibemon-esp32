# VibeMon Touch

A four-panel agent and resource monitor for the **GUITION ESP32-4848S040** touchscreen.

The 480×480 display follows your VibeMon workspace over HTTPS. Each card can show an agent (a coding account) or a resource. Tap it to read details, then use **Change item** to choose another source. Wi-Fi, server address, read token and panel selections persist across restarts.

## Set up the device

1. Build and upload the firmware using the [setup guide](docs/arduino-setup.md). Later releases install over Wi-Fi.
2. On the screen, select a **2.4 GHz Wi-Fi** network and enter its password. Use **Hidden network** to enter a name manually.
3. Scan the QR code on the screen, or open `vibemon.io/device` and enter the code shown, such as `BCDF-GHJK`. Sign in and select **Allow**. The device receives its own read-only token and checks it before saving.
4. To use another server or an existing token, select **Enter a token**: enter your HTTPS VibeMon origin and a token with **read** permission from [Account](https://vibemon.io/account). **Use a code** on that form signs in to the server you entered.
5. Tap an empty card, or open a populated card and select **Change item**. Choose an agent or a resource; the **Agents** and **Resources** buttons narrow the list, which lists agents first.

The first successful non-empty response fills up to four cards, alternating agents and resources and starting with an agent. Your selections then remain fixed until you change them. Missing measurements display `—`; lost connections retain the last readings with an explicit connection status. Items that are not connected or not active show neutral `Standby`: a resource without a report for 120 seconds, or an agent whose projects reached sleep through the shared state timeouts. Hooks repeat a running turn's state every 60 seconds, so a long task stays active.

## What it supports

- Custom resources, Kubernetes clusters, DGX Spark and other host metrics.
- Multiple Codex, Claude, Kiro or other provider accounts, grouped by provider and account ID.
- Session, weekly and model-specific weekly plan usage with the time until each limit resets, for example `38% · 2h5m` on a card and `Resets in 2h5m` in details. The countdown continues between polls.
- Resource metric labels, units, ranges and order from VibeMon. Cards show the first three metrics; scrollable details show all readings, full names, units, status messages and reporting projects.
- On-screen Wi-Fi scanning, password input and a large read-token keypad.
- Korean and Latin names, without downloading fonts at runtime.
- Optional USB setup for pasting a long key; secrets are prompted and never echoed.
- Firmware updates over Wi-Fi from this repository's releases. A new image that cannot reach the network rolls back on restart.

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

Native tests run the same LVGL screen code as the firmware. PNG renders appear in `build/native/screenshots/`. The compiled binary and its image manifest appear in `build/firmware/`. Run the **Verify firmware** workflow for the pull request branch to build and upload these artifacts; pull requests do not start it automatically. A version tag runs the checks, builds and publishes the release.

## Migration

Version 3 targets the four-inch ESP32-S3 board. It replaces the C6 single-character sketch, local status-push endpoints, WebSocket client and shared-password captive portal with touch setup and authenticated cloud polling. Existing C6 devices must continue using the [previous firmware](https://github.com/opspresso/vibemon-esp32/tree/e6c272c); do not flash this build to them. Private legacy `credentials.h` files are ignored and are not read by the new firmware.

## Documentation

- [Build, upload and setup](docs/arduino-setup.md)
- [Board and pin assignments](docs/hardware.md)
- [Data, security and USB contracts](docs/api.md)
- [Architecture and verification](docs/architecture.md)
- [VibeMon Desktop](https://github.com/opspresso/vibemon-app) · [VibeMon](https://vibemon.io)

MIT. The bundled Nanum Gothic font has its own [SIL Open Font License](assets/OFL-NanumGothic.txt).

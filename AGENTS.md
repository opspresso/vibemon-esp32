# Repository guidelines

## Scope and architecture

VibeMon Touch targets the GUITION ESP32-4848S040: ESP32-S3, 480×480 ST7701 LCD and GT911 touch. Read `docs/hardware.md` before changing GPIOs, display timing, PSRAM or flash layout. The firmware polls VibeMon's authenticated HTTPS API. It does not host the old status HTTP server or use WebSocket.

- `src/domain/`: portable data rules, validation and formatting. No Arduino or LVGL dependencies.
- `src/application/`: setup, persistence and polling orchestration behind ports. No hardware dependencies.
- `src/adapters/`: bounded JSON contracts for cloud observations and stored settings.
- `src/platform/`: ESP32 display, Wi-Fi, HTTPS worker, NVS and USB adapters.
- `src/presentation/`: LVGL screens shared by firmware and host tests.
- `tests/`: portable regression tests and actual LVGL pointer/render tests.
- `assets/`: product artwork and licensed font sources. `assets/sources.json` records provenance.

Use C++17-compatible application code, two-space indentation, `snake_case` file names and the `vibemon` namespace. Keep LVGL calls on the UI task. Network workers must not access UI objects. Keep secrets out of logs and public view models.

## Toolchain and checks

Versions are pinned in `toolchain.lock.json`; development Python packages are pinned in `requirements-dev.txt`. `tools/setup.py` installs libraries in ignored `build/arduino/libraries`. Firmware builds use Arduino as an ESP-IDF component. `firmware/sdkconfig.defaults` controls RGB cache, DMA and flash-write behavior; do not replace this with Arduino’s prebuilt SDK. Keep the ESP-IDF checkout unmodified. Do not modify a user’s sketchbook libraries or embed credentials in the sketch.

After setup, use the virtual environment's Python:

- `python tools/check.py`: native C++ sanitizers, controller/settings tests, USB framing checks and LVGL screen renders.
- `python tools/build_firmware.py`: compile the ESP32-S3 firmware and record image hashes.
- `python tools/flash_firmware.py --port PORT`: upload verified individual images. It preserves the NVS partition; never replace this with a merged-image upload for an existing installation.
- `python tools/device.py --port PORT info`: query non-secret device diagnostics.

Review screenshots in `build/native/screenshots` after UI changes. Keep controls within 480×480, preserve touch targets and input focus, and test disconnected, invalid-token, unavailable-value and long-name states. A host render or successful compile does not prove physical LCD/touch operation.

## Changes and documentation

Use focused Conventional Commits. Keep README and `docs/` aligned with the implementation. Do not commit `build/`, private settings, firmware backups or device-specific credentials. Retain third-party asset licenses. Generated embedding output belongs in `build/generated`, not Git. Do not claim a hardware test passed without device evidence.

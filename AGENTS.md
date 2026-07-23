# Repository Guidelines

## Project Structure & Module Organization

This repository contains Arduino firmware for two ESP32-C6 LCD boards. `vibemon-esp32.ino` owns `setup()` and `loop()` and includes the header-based modules in dependency order. Core behavior is split across `state.h`, `input.h`, `project_lock.h`, `display.h`, and `wifi_manager.h`; display compatibility and board configuration live in `TFT_Compat.h` and `LGFX_ESP32C6.hpp`. Pixel-art sources are under `images/`, with generated RGB565 headers named `img_*.h`. Conversion utilities live in `tools/`, and user documentation is in `docs/`.

Runtime flow is Serial/WiFi input → JSON parsing → state update → LCD rendering. `credentials.h` selects the board at compile time; configure `LGFX_ESP32C6.hpp` before display initialization.

## Build, Test, and Development Commands

Create local configuration before compiling:

```bash
cp credentials.h.example credentials.h
arduino-cli lib install ArduinoJson LovyanGFX WebSockets
arduino-cli compile --fqbn esp32:esp32:esp32c6:PartitionScheme=huge_app .
```

Set `BOARD_TYPE` in `credentials.h` to `BOARD_1_47` or `BOARD_1_9`. CI compiles both variants; reproduce a specific variant with `--build-property "compiler.cpp.extra_flags=-DBOARD_TYPE=0"` (or `1`). Regenerate sprites with `python3 tools/png_to_rgb565.py codex`; use `rgb565_to_png.py` for reverse inspection.

Use the `ESP32C6 Dev Module` target and `Huge APP (3MB No OTA/1MB SPIFFS)` partition. Required libraries are `ArduinoJson`, `WebSockets`, and `LovyanGFX`; TFT_eSPI is not used.

## Coding Style & Naming Conventions

Match the existing Arduino C++ style: two-space indentation, opening braces on the same line, `camelCase` functions and variables, and `UPPER_SNAKE_CASE` constants/macros. Preserve header guards and the include order in the main sketch because modules depend on earlier declarations. Keep changes focused; do not reformat unrelated code or hand-edit generated `img_*.h` files when the PNG source can be updated.

## Testing Guidelines

There is no unit-test suite or coverage threshold. Every change must compile for both supported boards. For behavior changes, exercise the affected path on hardware using newline-terminated JSON over Serial or the HTTP API, for example:

```bash
curl -X POST http://DEVICE_IP/status -H "Content-Type: application/json" \
  -d '{"state":"working","tool":"Bash","project":"demo"}'
```

Document the board and manual checks performed in the pull request.

Serial JSON payloads must end with LF (`\n`). When changing status handling, verify project locking, state timeouts, and rendering dirty flags on the affected paths.

## Commit & Pull Request Guidelines

Recent history uses Conventional Commit subjects such as `fix: ...`, `docs: ...`, and `ci: ...`; keep subjects imperative and narrowly scoped. Pull requests should explain the user-visible effect, list compile/manual verification, link relevant issues, and include a photo or screenshot for display or captive-portal changes. Never commit `credentials.h`, WiFi passwords, or WebSocket tokens.

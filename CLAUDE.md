# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32 firmware for VibeMon — a real-time status monitor for AI assistants (Claude Code, Codex, Kiro, OpenClaw) with pixel art character on an LCD display. The desktop (Electron) app lives in the separate [vibemon-app](https://github.com/opspresso/vibemon-app) repository.

**Supported ESP32 boards (compile-time selection via `BOARD_TYPE` in `credentials.h`):**
- ESP32-C6-LCD-1.47 — ST7789V2, 172×320, GPIO22 PWM backlight
- ESP32-C6-LCD-1.9  — ST7789V2, 170×320, GPIO15 direct backlight active-low (touch optional)

## Development Environment

Arduino IDE 2.x with the ESP32 board package (ESP32C6 Dev Module) and libraries: `ArduinoJson`, `WebSockets`, `LovyanGFX`. Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS). See [docs/esp32-setup.md](docs/esp32-setup.md).

## Architecture

```
┌─────────────────────────────────────────────────────┐
│            Main Loop (vibemon-esp32.ino)            │
│  Serial/WiFi → JSON Parse → State Update → Render   │
└─────────────────────────────────────────────────────┘
```

### Key Files

- `vibemon-esp32.ino` (main orchestrator), `config.h` (constants), `TFT_Compat.h` (LovyanGFX wrapper, defines `TFT_eSPI`/`TFT_eSprite` aliases), `LGFX_ESP32C6.hpp` (dual-board display driver, `configure(boardType)` called before `init()`), `sprites.h` (rendering), `img_clawd.h`/`img_codex.h`/`img_kiro.h`/`img_claw.h` (per-character 128×128 RGB565 bitmaps used by `sprites.h`), `ui_elements.h` (status text, icons), `state.h` (globals, timers, `g_boardType`), `display.h` (screen drawing), `project_lock.h` (lock logic), `input.h` (JSON parsing), `wifi_manager.h` (WiFi/HTTP/WebSocket), `wifi_portal.h` (captive portal HTML), `credentials.h.example` (WiFi/WebSocket/board config template → copy to `credentials.h`)
- **Tools**: `tools/png_to_rgb565.py` (PNG → `img_*.h` RGB565 header; magenta `0xF81F` = transparent), `tools/rgb565_to_png.py` (reverse, regenerates `images/img_*.png`)
- **Documentation**: `README.md`, `CLAUDE.md`, `docs/*`

## Key Patterns

- **State-based rendering**: `state` → color, eyeType, text
- **Animation**: `animFrame % N` approach (100ms tick)
- **Floating**: Cosine/Sine wave offset (X: ±3px, Y: ±5px, ~3.2s cycle)
- **Working text**: Tool-based fixed text via `getWorkingText(tool)` (Bash→Running, Read→Reading, Edit→Editing, Write→Writing, Grep/WebSearch→Searching, Glob→Scanning, WebFetch→Fetching, Task→Tasking, default→Working)
- **JSON fields**: `{"state", "tool", "project", "model", "memory", "usage5h", "usageWeek", "usage5hResetsIn", "usageWeekResetsIn", "character"}` (`*ResetsIn` = minutes until the usage window resets)
- **Characters**: `clawd` (orange), `codex` (green), `kiro` (white ghost), `claw` (red) — `daangni` is desktop-only and silently ignored
- **Metric rows**: memory (🧠), 5h usage (⏱️), weekly usage (📅) each render as a single line `[icon] [bar] [NN% 24m]` at the bottom; `usage5h`/`usageWeek` are plan-usage % (0-100) from statusline's `usage.json`; `usage5hResetsIn`/`usageWeekResetsIn` (minutes) are appended next to the usage percentage in compact form (`24m`, `2h5m`, `3d4h`), and the bar fills the width the text leaves
- **Memory hidden on start**: Memory not displayed during `start` state
- **Project change resets**: Model/memory cleared when project changes (usage is account-global, not reset)
- **Sparkle effect (start, working)**: Animated 4-point star sparkle
- **Glasses (working)**: Frame-only glasses, lenses stay clear so the eyes underneath remain visible (EYE_GLASSES)
- **Loading dots speed**: Thinking/planning/packing states use 3x slower animation than working state
- **Alert light**: Optional GPIO output for physical alert light; define `ALERT_PIN` in `credentials.h` to enable; HIGH during `alert` state, LOW otherwise; use GPIO2 (safe for both boards) — GPIO4 conflicts with 1.9" board MOSI
- **Board selection**: Set `#define BOARD_TYPE BOARD_1_9` or `BOARD_1_47` in `credentials.h`; configures SPI pins, panel offset, and backlight at compile time; 1.9" backlight is GPIO15 direct (active-low: LOW=on, HIGH=off); 1.47" backlight is GPIO22 PWM

## API Endpoints

| Endpoint | Description |
|----------|-------------|
| `POST /status` | Update device status |
| `GET /status` | Returns current state |
| `GET /health` | Health check |
| `POST /lock` | Lock to project |
| `POST /unlock` | Unlock project |
| `GET /lock-mode` | Get current lock mode |
| `POST /lock-mode` | Set lock mode |
| `POST /reboot` | Reboot device |
| `POST /wifi-reset` | Clear WiFi credentials, enter provisioning mode |

## States

| State | Color | Description |
|-------|-------|-------------|
| `start` | Cyan | Session begins |
| `idle` | Green | Waiting for input |
| `thinking` | Purple | Processing prompt |
| `planning` | Teal | Plan mode active |
| `working` | Blue | Tool executing |
| `packing` | Gray | Context compacting |
| `notification` | Yellow | User input needed |
| `done` | Green | Tool completed |
| `sleep` | Navy | 5min inactivity |
| `alert` | Red | Critical error/failure |

## Testing

```bash
# Serial (macOS)
echo '{"state":"working","tool":"Bash","project":"my-project"}' > /dev/cu.usbmodem1101

# Serial (Raspberry Pi / Linux)
stty -F /dev/ttyACM0 115200  # Set baud rate first (required)
echo '{"state":"working","tool":"Bash","project":"my-project"}' > /dev/ttyACM0

# HTTP (WiFi mode)
curl -X POST http://DEVICE_IP/status \
  -H "Content-Type: application/json" \
  -d '{"state":"working","tool":"Bash","project":"my-project"}'
```

## Important Notes

- Uses LovyanGFX library with `LGFX_ESP32C6.hpp` configuration (TFT_eSPI not required)
- JSON payload must end with LF (`\n`)
- WiFi mode: Create `credentials.h` from example (WiFi and WebSocket are enabled by default)

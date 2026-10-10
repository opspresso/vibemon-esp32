# VibeMon ESP32

**Real-time status monitor for AI assistants on a dedicated ESP32 LCD display.**

See at a glance what your AI assistant is doing — thinking, working, or waiting for input. A cute pixel art character visually represents the current state on an always-on desk companion.

ESP32 firmware for VibeMon. For the desktop (Electron) app, see [vibemon-app](https://github.com/opspresso/vibemon-app).

The hosted WebSocket service is retired. This firmware receives status through local HTTP or USB serial. It does not poll the current Web monitoring API. Existing installations should disable `USE_WEBSOCKET` in their private `credentials.h` and reflash; the configuration example disables it by default.

## Supported Hardware

Two ESP32-C6 LCD boards are supported, selected at compile time via `BOARD_TYPE` in `credentials.h`:

| Board | LCD | Resolution | Backlight | SPI Freq | SPI Pins (MOSI/SCK/DC/CS/RST) |
|-------|-----|------------|-----------|----------|-------------------------------|
| ESP32-C6-LCD-1.47 | ST7789V2 | 172×320 | GPIO22 PWM | 40MHz | 6/7/15/14/21 |
| ESP32-C6-LCD-1.9  | ST7789V2 | 170×320 | GPIO15 direct (active-low) | 20MHz | 4/5/6/7/14 |

## Supported Tools

| Tool | Description |
|------|-------------|
| **[Claude Code](https://claude.ai/code)** | Anthropic's official AI coding assistant |
| **[Codex](https://openai.com/codex)** | OpenAI's AI coding agent |
| **[Kiro](https://kiro.dev/)** | AWS's AI coding assistant |
| **[OpenClaw](https://openclaw.ai/)** | Open-source computer use agent |

## Quick Start

1. Copy `credentials.h.example` to `credentials.h` and set `BOARD_TYPE` (ESP32-C6-LCD-1.47 or 1.9)
2. Flash the firmware (`vibemon-esp32.ino`) with Arduino IDE
3. Device creates WiFi AP: `VibeMon-Setup` (password: `vibemon123`)
4. Connect and configure WiFi via the web interface
5. Send status to the device over local HTTP or USB serial

See the [ESP32 Setup Guide](docs/arduino-setup.md) for Arduino IDE setup, provisioning, and troubleshooting.

## What It Monitors

| Field | Description | Example |
|-------|-------------|---------|
| **State** | Current activity state | `working`, `idle`, `notification` |
| **Project** | Active project directory | `vibemon-esp32` |
| **Tool** | Currently executing tool, shown as the working status text | `Bash` → `Running`, `Read` → `Reading` |
| **Model** | Active model | `Opus 4.5`, `Sonnet` |
| **Memory** | Context window usage | `45%` |
| **Usage** | 5-hour / weekly plan usage, with time until reset | `18% 24m`, `60% 3d` |

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
| `sleep` | Navy | After 5min in idle |
| `alert` | Red | Critical error/failure (triggers alert light if configured) |

Active states (`thinking`, `planning`, `working`, `notification`, `packing`, `alert`) auto-return to `idle` after 5 minutes without an update; `start` and `done` return to `idle` after 1 minute.

## Characters

| Character | Color | Auto-selected for |
|-----------|-------|-------------------|
| `vibemon` | Purple | Default; any bridge without its own character |
| `clawd` | Orange | Claude Code |
| `kiro` | White | Kiro |
| `claw` | Red | OpenClaw |
| `codex` | Navy | Codex |

## HTTP API

Default port: `80` (WiFi mode)

```bash
# Update status
curl -X POST http://DEVICE_IP/status \
  -H "Content-Type: application/json" \
  -d '{"state":"working","tool":"Bash","project":"my-project"}'

# Health check
curl http://DEVICE_IP/health

# Reboot
curl -X POST http://DEVICE_IP/reboot \
  -H "Content-Type: application/json" \
  -d '{"confirm":true}'
```

See the [API Reference](docs/api.md) for all endpoints.

## Serial Input

Status can also be sent over USB serial (115200 baud, JSON payload must end with LF):

```bash
# macOS
echo '{"state":"working","tool":"Bash","project":"my-project"}' > /dev/cu.usbmodem1101

# Raspberry Pi / Linux
stty -F /dev/ttyACM0 115200  # Set baud rate first (required)
echo '{"state":"working","tool":"Bash","project":"my-project"}' > /dev/ttyACM0
```

## Sprite Tools

Character bitmaps are 128×128 RGB565 C headers (`img_*.h`) generated from PNGs in `images/`:

```bash
# PNG → C header (magenta 0xF81F = transparent)
python3 tools/png_to_rgb565.py kiro    # images/img_kiro.png -> img_kiro.h

# C header → PNG (reverse)
python3 tools/rgb565_to_png.py kiro    # img_kiro.h -> images/img_kiro.png
```

## Documentation

- [ESP32 Setup Guide](docs/arduino-setup.md) - Arduino IDE setup, WiFi provisioning, and optional custom-server configuration
- [API Reference](docs/api.md) - Complete HTTP API documentation

For full documentation, visit **[vibemon.io/docs](https://vibemon.io/docs)**.

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Captive portal doesn't open | Navigate to `http://192.168.4.1` manually |
| WiFi connection fails | Check password, ensure 2.4GHz network |
| Device won't enter setup mode | Send `POST /wifi-reset` with body `{"confirm":true}` to clear credentials |

See the [ESP32 Setup Guide](docs/arduino-setup.md#troubleshooting) for more.

## Related Projects

- [vibemon-app](https://github.com/opspresso/vibemon-app) - Desktop app (Electron)
- [VibeMon Web](https://vibemon.io) - Cloud dashboard and API
- [Hook setup](https://vibemon.io/docs/setup) - Public installation guide
- [Shared assets](https://github.com/opspresso/vibemon-app/blob/main/docs/distribution.md) - App-owned source and distribution

## License

MIT

# HTTP API Reference

Default port: `80` (WiFi mode)

The HTTP server starts once the device connects to WiFi. In provisioning mode, a separate configuration server runs instead (see [Provisioning Endpoints](#provisioning-endpoints)).

## Security & Limits

The ESP32 HTTP server does not enforce payload size, rate limiting, or CORS restrictions. Security relies on local network isolation and SSID sanitization.

### Input Validation

| Field | Max Length | Format |
|-------|------------|--------|
| `state` | - | One of valid states (unrecognized values are silently treated as `idle`) |
| `project` | 100 chars | String |
| `tool` | 50 chars | String |
| `model` | 50 chars | String |
| `memory` | - | Integer 0-100 (context-window usage) |
| `usage5h` | - | Integer 0-100 (5-hour plan-usage window) |
| `usageWeek` | - | Integer 0-100 (weekly plan-usage window) |
| `usage5hResetsIn` | - | Integer ≥ 0 (minutes until the 5-hour window resets) |
| `usageWeekResetsIn` | - | Integer ≥ 0 (minutes until the weekly window resets) |
| `character` | - | `vibemon`, `clawd`, `kiro`, `claw`, or `codex` (unknown names like `daangni` are silently ignored) |

> `character` is a visual rendering choice, typically selected by the agent bridge. It is not a general agent identity field.

## Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/status` | POST | Update device status |
| `/status` | GET | Get current status |
| `/health` | GET | Health check |
| `/lock` | POST | Lock to a specific project |
| `/unlock` | POST | Unlock project |
| `/lock-mode` | GET | Get current lock mode |
| `/lock-mode` | POST | Set lock mode |
| `/reboot` | POST | Reboot device |
| `/wifi-reset` | POST | Clear WiFi credentials, restart provisioning |

## Status

### POST /status

Update monitor status.

```bash
curl -X POST http://DEVICE_IP/status \
  -H "Content-Type: application/json" \
  -d '{"state":"working","tool":"Bash","project":"my-project"}'
```

**Request Body:**

| Field | Type | Description |
|-------|------|-------------|
| `state` | string | `start`, `idle`, `thinking`, `planning`, `working`, `packing`, `notification`, `done`, `sleep`, `alert` |
| `tool` | string | Tool name (e.g., `Bash`, `Read`, `Edit`) |
| `project` | string | Project name |
| `model` | string | Model name (e.g., `opus`, `sonnet`) |
| `memory` | number | Context-window usage (0-100) |
| `usage5h` | number | 5-hour plan-usage window (0-100) |
| `usageWeek` | number | Weekly plan-usage window (0-100) |
| `usage5hResetsIn` | number | Minutes until the 5-hour window resets (shown next to `usage5h`) |
| `usageWeekResetsIn` | number | Minutes until the weekly window resets (shown next to `usageWeek`) |
| `character` | string | `vibemon`, `clawd`, `kiro`, or `claw` (unknown names like `daangni` are silently ignored) |

> An unrecognized `state` value is silently treated as `idle` — no error is returned.

Agent bridges usually set `character` automatically:
- `clawd` for Claude Code
- `codex` for Codex
- `kiro` for Kiro
- `claw` for OpenClaw

**Response:**
```json
{"success": true}
```

> If blocked by project lock: `{"success": false, "blocked": true}`

### GET /status

Get current status.

```bash
curl http://DEVICE_IP/status
```

**Response:**
```json
{
  "state": "working",
  "project": "my-project",
  "lockedProject": "my-project",
  "lockMode": "on-thinking",
  "projectCount": 1
}
```

---

## Project Lock

### POST /lock

Lock to a specific project.

```bash
curl -X POST http://DEVICE_IP/lock \
  -H "Content-Type: application/json" \
  -d '{"project":"my-project"}'
```

**Request Body:**

| Field | Type | Description |
|-------|------|-------------|
| `project` | string | Project name to lock. Defaults to current project if omitted. |

**Response:**
```json
{"success": true, "lockedProject": "my-project"}
```

> When locking a new project, the display transitions to `idle` state and clears `tool`, `model`, `memory`.

### POST /unlock

Unlock project.

```bash
curl -X POST http://DEVICE_IP/unlock
```

**Response:**
```json
{"success": true, "lockedProject": null}
```

### GET /lock-mode

Get current lock mode.

```bash
curl http://DEVICE_IP/lock-mode
```

**Response:**
```json
{
  "mode": "on-thinking",
  "modes": {"first-project": "First Project", "on-thinking": "On Thinking"},
  "lockedProject": null
}
```

### POST /lock-mode

Set lock mode (`first-project` or `on-thinking`).

```bash
curl -X POST http://DEVICE_IP/lock-mode \
  -H "Content-Type: application/json" \
  -d '{"mode":"first-project"}'
```

**Response:**
```json
{"success": true, "mode": "first-project", "lockedProject": null}
```

> Changing lock mode resets the current lock (`lockedProject` becomes null) and persists the new mode to Flash storage.
>
> On an invalid `mode`, the response is HTTP 400 with `{"error": "Invalid mode. Valid modes: first-project, on-thinking"}` (no `success` field).

---

## System

### GET /health

Health check endpoint.

```bash
curl http://DEVICE_IP/health
```

**Response:**
```json
{"status": "ok"}
```

### POST /reboot

Reboot the device.

```bash
curl -X POST http://DEVICE_IP/reboot \
  -H "Content-Type: application/json" \
  -d '{"confirm":true}'
```

**Response:**
```json
{"success": true, "rebooting": true}
```

### POST /wifi-reset

Clear saved WiFi credentials and return to provisioning mode.

```bash
curl -X POST http://DEVICE_IP/wifi-reset \
  -H "Content-Type: application/json" \
  -d '{"confirm":true}'
```

**Response:**
```json
{"success": true, "message": "WiFi credentials cleared. Rebooting..."}
```

**Behavior:**
- Clears `wifiSSID`, `wifiPassword` from NVS (WebSocket token is preserved)
- Device reboots automatically
- Enters provisioning mode (creates `VibeMon-Setup` AP)

See [ESP32 Setup Guide](esp32-setup.md#reset-wifi-settings) for details.

---

## Provisioning Endpoints

Available only while the device is in provisioning mode (AP `VibeMon-Setup`, IP `192.168.4.1`):

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/*` | Any (catch-all) | Configuration page (HTML) |
| `/scan` | GET | WiFi networks list (JSON) |
| `/save` | POST | Save WiFi + token, reboot |

---

## HTTP Status Codes

| Code | Description |
|------|-------------|
| `200` | Success (also used for some errors — check `success` field) |
| `400` | Bad request (missing body or invalid input) |
| `404` | Likely, via the `WebServer` library's default unmatched-route handler (normal mode registers no custom `onNotFound`) |

> The HTTP server always returns HTTP 200 for valid requests (including project-lock rejections). Check the `success` field in the response body to determine the outcome.

### Error Response Format

```json
{"error": "Error message description"}
```

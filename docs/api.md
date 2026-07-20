# HTTP API Reference

Default port: `80` (WiFi mode)

The HTTP server starts once the device connects to WiFi. In provisioning mode, a separate configuration server runs instead (see [Provisioning Endpoints](#provisioning-endpoints)).

## Security & Limits

The ESP32 HTTP server does not enforce payload size, rate limiting, or CORS restrictions. Security relies on local network isolation and SSID sanitization.

### Input Validation

| Field | Max Length | Format |
|-------|------------|--------|
| `state` | - | One of valid states (unrecognized values are silently treated as `idle`) |
| `project` | 31 chars | String (truncated if longer) |
| `tool` | 31 chars | String (truncated if longer) |
| `model` | 31 chars | String (truncated if longer) |
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
| `character` | string | `vibemon`, `clawd`, `kiro`, `claw`, or `codex` (unknown names like `daangni` are silently ignored) |

> An unrecognized `state` value is silently treated as `idle` — no error is returned.

> The model-scoped weekly usage fields (`usageWeekModel`, `usageWeekModelResetsIn`,
> `usageWeekModelLabel`) sent by newer bridges are ignored — the 172x320 screen
> already fills its three metric rows, so the device deliberately renders only
> `memory`, `usage5h`, and `usageWeek`.

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
>
> On invalid JSON: HTTP 400 with `{"error": "JSON parse error"}`
>
> On missing request body: HTTP 400 with `{"error": "no body"}`

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
>
> If no project is given and no project is currently active: HTTP 400 with `{"error": "No project to lock"}`

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

> Every valid mode change request resets the current lock (`lockedProject` becomes null) and persists the mode to Flash storage — even if the mode value is unchanged from the current one.
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

> Without `{"confirm":true}` in the body: HTTP 400 with `{"error": "Requires {\"confirm\":true}"}`

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

> Without `{"confirm":true}` in the body: HTTP 400 with `{"error": "Requires {\"confirm\":true}"}`

**Behavior:**
- Clears `wifiSSID`, `wifiPassword` from NVS (WebSocket token is preserved)
- Device reboots automatically
- Enters provisioning mode (creates `VibeMon-Setup` AP)

See [ESP32 Setup Guide](esp32-setup.md#reset-wifi-settings) for details.

---

## Serial / WebSocket Commands

The same actions available via HTTP can also be triggered over Serial (115200 baud, JSON payload ending in LF) or WebSocket, using a `command` field instead of a URL path.

| Command | Fields | Equivalent to |
|---------|--------|---------------|
| `lock` | `project` (optional, defaults to current project) | `POST /lock` |
| `unlock` | - | `POST /unlock` |
| `status` | - | `GET /status` |
| `lock-mode` | `mode` (optional; omitted = query current mode) | `GET`/`POST /lock-mode` |
| `reboot` | - | `POST /reboot` (no `{"confirm":true}` required) |

```bash
echo '{"command":"lock","project":"my-project"}' > /dev/cu.usbmodem1101
```

> Responses are always printed to Serial as JSON, regardless of which channel the command arrived on — a command sent over WebSocket gets no reply over the WebSocket connection.

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
